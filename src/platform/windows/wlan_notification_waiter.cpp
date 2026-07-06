#include "wlan_notification_waiter.h"

#include <chrono>
#include <condition_variable>
#include <cstring>

namespace wifigd {

namespace {

bool guid_equals(const GUID &a, const GUID &b) {
	return IsEqualGUID(a, b) != FALSE;
}

bool dot11_ssid_equals(const DOT11_SSID &ssid, const godot::String &expected) {
	if (expected.is_empty() || ssid.uSSIDLength == 0) {
		return false;
	}

	const godot::CharString utf8 = expected.utf8();
	const size_t expected_len = (size_t)utf8.length();
	if (expected_len > DOT11_SSID_MAX_LENGTH || ssid.uSSIDLength != expected_len) {
		return false;
	}

	return std::memcmp(ssid.ucSSID, utf8.get_data(), expected_len) == 0;
}

} // namespace

WlanNotificationWaiter::~WlanNotificationWaiter() {
	if (registered_handle != nullptr) {
		unregister(registered_handle);
	}
}

bool WlanNotificationWaiter::ensure_registered(HANDLE handle) {
	if (handle == nullptr) {
		return false;
	}
	if (registered_handle == handle) {
		return true;
	}
	if (registered_handle != nullptr) {
		unregister(registered_handle);
	}

	const DWORD result = WlanRegisterNotification(
			handle,
			WLAN_NOTIFICATION_SOURCE_ACM,
			TRUE,
			notification_callback,
			this,
			nullptr,
			nullptr);
	if (result != ERROR_SUCCESS) {
		return false;
	}

	registered_handle = handle;
	return true;
}

void WlanNotificationWaiter::unregister(HANDLE handle) {
	if (handle == nullptr || registered_handle != handle) {
		return;
	}

	WlanRegisterNotification(
			handle,
			WLAN_NOTIFICATION_SOURCE_NONE,
			TRUE,
			nullptr,
			nullptr,
			nullptr,
			nullptr);
	registered_handle = nullptr;
}

void WINAPI WlanNotificationWaiter::notification_callback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
	if (context == nullptr || data == nullptr) {
		return;
	}
	static_cast<WlanNotificationWaiter *>(context)->on_notification(data);
}

void WlanNotificationWaiter::on_notification(PWLAN_NOTIFICATION_DATA data) {
	if (data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);
	const auto code = static_cast<WLAN_NOTIFICATION_ACM>(data->NotificationCode);
	bool should_notify = false;

	if (waiting_scan && guid_equals(data->InterfaceGuid, scan_iface)) {
		if (code == wlan_notification_acm_scan_complete) {
			scan_complete = true;
			should_notify = true;
		} else if (code == wlan_notification_acm_scan_fail) {
			scan_complete = true;
			scan_failed = true;
			should_notify = true;
		}
	}

	if (waiting_connect && guid_equals(data->InterfaceGuid, connect_iface)) {
		if (code == wlan_notification_acm_connection_complete) {
			connect_complete = true;
			connect_success = true;
			connect_reason_code = WLAN_REASON_CODE_SUCCESS;

			if (data->pData != nullptr &&
					data->dwDataSize >= offsetof(WLAN_CONNECTION_NOTIFICATION_DATA, strProfileXml)) {
				const auto *conn = static_cast<const WLAN_CONNECTION_NOTIFICATION_DATA *>(data->pData);
				connect_reason_code = conn->wlanReasonCode;
				connect_success = conn->wlanReasonCode == WLAN_REASON_CODE_SUCCESS;

				if (!expected_ssid.is_empty() && conn->dot11Ssid.uSSIDLength > 0) {
					connect_success = connect_success && dot11_ssid_equals(conn->dot11Ssid, expected_ssid);
				}
			}
			should_notify = true;
		} else if (code == wlan_notification_acm_connection_attempt_fail) {
			connect_complete = true;
			connect_success = false;
			if (data->pData != nullptr &&
					data->dwDataSize >= offsetof(WLAN_CONNECTION_NOTIFICATION_DATA, strProfileXml)) {
				const auto *conn = static_cast<const WLAN_CONNECTION_NOTIFICATION_DATA *>(data->pData);
				connect_reason_code = conn->wlanReasonCode;
			}
			should_notify = true;
		}
	}

	if (should_notify) {
		cv.notify_all();
	}
}

void WlanNotificationWaiter::begin_scan_wait(const GUID &iface_guid) {
	std::lock_guard<std::mutex> lock(mutex);
	waiting_scan = true;
	scan_iface = iface_guid;
	scan_complete = false;
	scan_failed = false;
}

bool WlanNotificationWaiter::wait_for_scan_complete(DWORD timeout_ms) {
	std::unique_lock<std::mutex> lock(mutex);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

	cv.wait_until(lock, deadline, [this]() { return scan_complete; });

	const bool success = scan_complete && !scan_failed;
	waiting_scan = false;
	return success;
}

void WlanNotificationWaiter::begin_connect_wait(const GUID &iface_guid, const godot::String &ssid) {
	std::lock_guard<std::mutex> lock(mutex);
	waiting_connect = true;
	connect_iface = iface_guid;
	expected_ssid = ssid;
	connect_complete = false;
	connect_success = false;
	connect_reason_code = 0;
}

WlanConnectWaitResult WlanNotificationWaiter::wait_for_connect_complete(DWORD timeout_ms) {
	std::unique_lock<std::mutex> lock(mutex);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

	cv.wait_until(lock, deadline, [this]() { return connect_complete; });

	WlanConnectWaitResult result;
	if (connect_complete) {
		result.completed = true;
		result.success = connect_success;
		result.reason_code = connect_reason_code;
	}

	waiting_connect = false;
	return result;
}

} // namespace wifigd