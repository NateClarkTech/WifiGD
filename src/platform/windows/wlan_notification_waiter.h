#pragma once

#include <godot_cpp/variant/string.hpp>

#include <condition_variable>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wlanapi.h>

namespace wifigd {

struct WlanConnectWaitResult {
	bool completed = false;
	bool success = false;
	DWORD reason_code = 0;
};

// Thread-safe waiter for WLAN ACM notifications (scan/connection complete).
// One instance per WlanOpenHandle session.
class WlanNotificationWaiter {
private:
	std::mutex mutex;
	std::condition_variable cv;
	HANDLE registered_handle = nullptr;

	bool waiting_scan = false;
	GUID scan_iface{};
	bool scan_complete = false;
	bool scan_failed = false;

	bool waiting_connect = false;
	GUID connect_iface{};
	godot::String expected_ssid;
	bool connect_complete = false;
	bool connect_success = false;
	DWORD connect_reason_code = 0;

	static void WINAPI notification_callback(PWLAN_NOTIFICATION_DATA data, PVOID context);
	void on_notification(PWLAN_NOTIFICATION_DATA data);

public:
	~WlanNotificationWaiter();

	bool ensure_registered(HANDLE handle);
	void unregister(HANDLE handle);

	void begin_scan_wait(const GUID &iface_guid);
	bool wait_for_scan_complete(DWORD timeout_ms);

	void begin_connect_wait(const GUID &iface_guid, const godot::String &ssid);
	WlanConnectWaitResult wait_for_connect_complete(DWORD timeout_ms);
};

} // namespace wifigd