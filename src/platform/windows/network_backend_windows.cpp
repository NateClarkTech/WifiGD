#include "network_backend_windows.h"
#include "wlan_notification_waiter.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wlanapi.h>
#include <iphlpapi.h>

#include "../../console_log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace wifigd {

namespace {

constexpr DWORD kScanPollIntervalMs = 400;
constexpr DWORD kScanPollAttempts = 20;
constexpr DWORD kScanNotificationTimeoutMs = 5000;
constexpr DWORD kConnectNotificationTimeoutMs = 30000;
constexpr DWORD kConnectPollFallbackMs = 5000;
constexpr DWORD kRadioSetTimeoutMs = 30000;
constexpr DWORD kRadioPollIntervalMs = 200;
constexpr DWORD kWlanApiVersion = 2;
constexpr DWORD kAvailableNetworkFlags = WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES;

struct InterfaceRadioState {
	bool software_enabled = false;
	bool hardware_enabled = false;
};

godot::String wide_to_utf8(const wchar_t *wide_string) {
	if (wide_string == nullptr) {
		return godot::String();
	}
	const int size = WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 0) {
		return godot::String();
	}
	godot::CharString buffer;
	buffer.resize(size);
	WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, buffer.ptrw(), size, nullptr, nullptr);
	return godot::String::utf8(buffer.get_data());
}

godot::String windows_error_text(DWORD code) {
	if (code == 0) {
		return godot::String();
	}

	wchar_t *buffer = nullptr;
	const DWORD length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&buffer),
			0,
			nullptr);
	if (length == 0 || buffer == nullptr) {
		return godot::String();
	}

	const godot::String message = wide_to_utf8(buffer).strip_edges();
	LocalFree(buffer);
	return message;
}

godot::String wlan_reason_text(DWORD reason_code) {
	if (reason_code == 0) {
		return godot::String();
	}

	wchar_t reason_buffer[512] = {};
	if (WlanReasonCodeToString(reason_code, 512, reason_buffer, nullptr) != ERROR_SUCCESS) {
		return godot::String();
	}
	return wide_to_utf8(reason_buffer);
}

void log_windows_error_to_console(const godot::String &operation, DWORD code, DWORD wlan_reason_code = 0) {
	if (code == 0 && wlan_reason_code == 0) {
		return;
	}

	log_to_console("[WifiGD] " + operation);

	if (code != 0) {
		const godot::String system_message = windows_error_text(code);
		const godot::String code_text = godot::String::num_int64((int64_t)code);
		if (system_message.is_empty()) {
			log_to_console("  Win32 error " + code_text);
		} else {
			log_to_console("  Win32 error " + code_text + ": " + system_message);
		}
	}

	if (wlan_reason_code != 0) {
		const godot::String reason_code_text = godot::String::num_int64((int64_t)wlan_reason_code);
		const godot::String reason_message = wlan_reason_text(wlan_reason_code);
		if (reason_message.is_empty()) {
			log_to_console("  WLAN reason " + reason_code_text);
		} else {
			log_to_console("  WLAN reason " + reason_code_text + ": " + reason_message);
		}
	}
}

godot::String describe_win_error(DWORD code) {
	switch (code) {
		case ERROR_ACCESS_DENIED:
			return "Access denied. Active Wi-Fi scan may require running Godot as administrator, "
				   "or you can use cached results from the system scan.";
		case ERROR_INVALID_STATE:
			return "The Wi-Fi interface is not ready. Ensure Wi-Fi is enabled.";
		case ERROR_NOT_FOUND:
			return "The Wi-Fi interface was not found.";
		case ERROR_NOT_SUPPORTED:
			return "The operation is not supported by this adapter or driver.";
		case ERROR_ALREADY_EXISTS:
			return "The Wi-Fi profile already exists.";
		default:
			return "Windows reported an error.";
	}
}

godot::String friendly_win_error(const godot::String &operation, DWORD code) {
	if (operation == "WlanScan") {
		return "Active Wi-Fi scan unavailable. " + describe_win_error(code);
	}
	if (operation == "WlanSetProfile") {
		return "Could not save the Wi-Fi profile. " + describe_win_error(code);
	}
	if (operation == "WlanConnect") {
		return "Could not connect to the network. " + describe_win_error(code);
	}
	if (operation == "WlanDisconnect") {
		return "Could not disconnect from Wi-Fi. " + describe_win_error(code);
	}
	if (operation == "WlanOpenHandle") {
		return "Could not open the Wi-Fi service. " + describe_win_error(code);
	}
	if (operation == "WlanEnumInterfaces") {
		return "Could not list Wi-Fi adapters. " + describe_win_error(code);
	}
	if (operation == "WlanQueryInterface radio_state") {
		return "Could not read the Wi-Fi radio state. " + describe_win_error(code);
	}
	if (operation == "WlanSetInterface radio_state") {
		if (code == ERROR_ACCESS_DENIED) {
			return "Wi-Fi radio changes require administrator privileges.";
		}
		return "Could not change the Wi-Fi radio state. " + describe_win_error(code);
	}

	return operation + " failed. " + describe_win_error(code);
}

godot::String report_win_failure(const godot::String &operation, DWORD code, DWORD wlan_reason_code = 0) {
	log_windows_error_to_console(operation, code, wlan_reason_code);
	return friendly_win_error(operation, code);
}

bool phy_radio_is_on(DOT11_RADIO_STATE state) {
	return state == dot11_radio_state_on;
}

bool is_process_elevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}

	TOKEN_ELEVATION elevation{};
	DWORD size = 0;
	const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
	CloseHandle(token);
	return ok != FALSE && elevation.TokenIsElevated != 0;
}

godot::String wifi_radio_permission_hint() {
	return is_process_elevated() ? "yes" : "no";
}

bool read_interface_radio_state(
		HANDLE wlan_handle,
		const GUID &iface_guid,
		InterfaceRadioState &state,
		godot::String &error_message) {
	WLAN_RADIO_STATE *radio_state = nullptr;
	DWORD data_size = 0;
	const DWORD result = WlanQueryInterface(
			wlan_handle,
			&iface_guid,
			wlan_intf_opcode_radio_state,
			nullptr,
			&data_size,
			reinterpret_cast<PVOID *>(&radio_state),
			nullptr);
	if (result != ERROR_SUCCESS || radio_state == nullptr) {
		error_message = report_win_failure("WlanQueryInterface radio_state", result);
		return false;
	}

	state = InterfaceRadioState{};
	for (DWORD i = 0; i < radio_state->dwNumberOfPhys; i++) {
		if (phy_radio_is_on(radio_state->PhyRadioState[i].dot11SoftwareRadioState)) {
			state.software_enabled = true;
		}
		if (phy_radio_is_on(radio_state->PhyRadioState[i].dot11HardwareRadioState)) {
			state.hardware_enabled = true;
		}
	}

	WlanFreeMemory(radio_state);
	return true;
}

WifiRadioState aggregate_radio_state(const std::vector<InterfaceRadioState> &interface_states) {
	WifiRadioState state;
	for (const InterfaceRadioState &iface_state : interface_states) {
		if (iface_state.software_enabled) {
			state.software_enabled = true;
		}
		if (iface_state.hardware_enabled) {
			state.hardware_enabled = true;
		}
	}

	state.enabled = state.software_enabled && state.hardware_enabled;
	state.permission = wifi_radio_permission_hint();
	state.can_toggle = state.hardware_enabled;
	return state;
}

bool radio_reached_requested_state(const WifiRadioState &state, bool enabled) {
	if (enabled) {
		return state.enabled;
	}
	return !state.software_enabled;
}

godot::String describe_radio_set_failure(const WifiRadioState &state, bool enabled) {
	if (!is_process_elevated()) {
		return "Wi-Fi radio did not change. Administrator privileges are required on Windows.";
	}
	if (enabled && !state.hardware_enabled) {
		return "Wi-Fi is blocked by hardware (check physical switch or airplane mode).";
	}
	return enabled
			? "Wi-Fi radio did not turn on in time. Check hardware switch or try again."
			: "Wi-Fi radio did not turn off in time.";
}

bool collect_wifi_radio_state(HANDLE wlan_handle, WifiRadioState &state, godot::String &error_message) {
	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	const DWORD result = WlanEnumInterfaces(wlan_handle, nullptr, &interfaces);
	if (result != ERROR_SUCCESS) {
		error_message = report_win_failure("WlanEnumInterfaces", result);
		return false;
	}
	if (interfaces == nullptr || interfaces->dwNumberOfItems == 0) {
		if (interfaces != nullptr) {
			WlanFreeMemory(interfaces);
		}
		error_message = "No Wi-Fi interfaces available.";
		return false;
	}

	std::vector<InterfaceRadioState> interface_states;
	interface_states.reserve(interfaces->dwNumberOfItems);
	for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
		InterfaceRadioState iface_state;
		godot::String iface_error;
		if (!read_interface_radio_state(
					wlan_handle,
					interfaces->InterfaceInfo[i].InterfaceGuid,
					iface_state,
					iface_error)) {
			WlanFreeMemory(interfaces);
			error_message = iface_error;
			return false;
		}
		interface_states.push_back(iface_state);
	}

	WlanFreeMemory(interfaces);
	state = aggregate_radio_state(interface_states);
	return true;
}

bool wait_for_wifi_radio_state(HANDLE wlan_handle, bool enabled, DWORD timeout_ms) {
	const DWORD start_tick = GetTickCount();
	while (true) {
		WifiRadioState state;
		godot::String error_message;
		if (!collect_wifi_radio_state(wlan_handle, state, error_message)) {
			return false;
		}
		if (radio_reached_requested_state(state, enabled)) {
			return true;
		}

		const DWORD elapsed = GetTickCount() - start_tick;
		if (elapsed >= timeout_ms) {
			break;
		}
		Sleep(kRadioPollIntervalMs);
	}

	WifiRadioState state;
	godot::String error_message;
	if (!collect_wifi_radio_state(wlan_handle, state, error_message)) {
		return false;
	}
	return radio_reached_requested_state(state, enabled);
}

godot::String format_scan_error(DWORD code) {
	log_windows_error_to_console("WlanScan", code);
	return friendly_win_error("WlanScan", code);
}

godot::String guid_to_string(const GUID &guid) {
	char buffer[64] = {};
	snprintf(
			buffer,
			sizeof(buffer),
			"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
			static_cast<unsigned long>(guid.Data1),
			guid.Data2,
			guid.Data3,
			guid.Data4[0],
			guid.Data4[1],
			guid.Data4[2],
			guid.Data4[3],
			guid.Data4[4],
			guid.Data4[5],
			guid.Data4[6],
			guid.Data4[7]);
	return godot::String(buffer);
}

bool guid_strings_equal(const godot::String &a, const godot::String &b) {
	return a.to_lower() == b.to_lower();
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

bool string_to_guid(const godot::String &text, GUID &guid) {
	godot::String normalized = text.strip_edges();
	if (normalized.begins_with("{")) {
		normalized = normalized.substr(1);
	}
	if (normalized.ends_with("}")) {
		normalized = normalized.substr(0, normalized.length() - 1);
	}

	unsigned int data1 = 0;
	unsigned int data2 = 0;
	unsigned int data3 = 0;
	unsigned int b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0;
	godot::CharString utf8 = normalized.utf8();
	const int matched = sscanf(
			utf8.get_data(),
			"%x-%x-%x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			&data1,
			&data2,
			&data3,
			&b0,
			&b1,
			&b2,
			&b3,
			&b4,
			&b5,
			&b6,
			&b7);
	if (matched != 11) {
		return false;
	}

	guid = {};
	guid.Data1 = data1;
	guid.Data2 = static_cast<WORD>(data2);
	guid.Data3 = static_cast<WORD>(data3);
	guid.Data4[0] = static_cast<BYTE>(b0);
	guid.Data4[1] = static_cast<BYTE>(b1);
	guid.Data4[2] = static_cast<BYTE>(b2);
	guid.Data4[3] = static_cast<BYTE>(b3);
	guid.Data4[4] = static_cast<BYTE>(b4);
	guid.Data4[5] = static_cast<BYTE>(b5);
	guid.Data4[6] = static_cast<BYTE>(b6);
	guid.Data4[7] = static_cast<BYTE>(b7);
	return true;
}

godot::String format_mac_address(const UCHAR *bytes, ULONG length) {
	if (bytes == nullptr || length == 0) {
		return godot::String();
	}
	godot::String mac;
	for (ULONG i = 0; i < length; i++) {
		if (i > 0) {
			mac += ":";
		}
		mac += godot::String::num_uint64(bytes[i], 16).pad_zeros(2).to_upper();
	}
	return mac;
}

godot::String format_dot11_bssid(const DOT11_MAC_ADDRESS &bssid) {
	return format_mac_address(bssid, 6);
}

godot::String sockaddr_to_ipv4_string(const SOCKET_ADDRESS &address) {
	if (address.lpSockaddr == nullptr || address.lpSockaddr->sa_family != AF_INET) {
		return godot::String();
	}
	char ip_buffer[INET_ADDRSTRLEN] = {};
	const auto *addr = reinterpret_cast<const sockaddr_in *>(address.lpSockaddr);
	if (inet_ntop(AF_INET, &addr->sin_addr, ip_buffer, sizeof(ip_buffer)) == nullptr) {
		return godot::String();
	}
	return godot::String(ip_buffer);
}

godot::String sockaddr_to_ipv6_string(const SOCKET_ADDRESS &address) {
	if (address.lpSockaddr == nullptr || address.lpSockaddr->sa_family != AF_INET6) {
		return godot::String();
	}
	char ip_buffer[INET6_ADDRSTRLEN] = {};
	const auto *addr = reinterpret_cast<const sockaddr_in6 *>(address.lpSockaddr);
	if (inet_ntop(AF_INET6, &addr->sin6_addr, ip_buffer, sizeof(ip_buffer)) == nullptr) {
		return godot::String();
	}
	return godot::String(ip_buffer);
}

godot::String xml_escape(const godot::String &text) {
	godot::String result = text;
	result = result.replace("&", "&amp;");
	result = result.replace("<", "&lt;");
	result = result.replace(">", "&gt;");
	result = result.replace("\"", "&quot;");
	result = result.replace("'", "&apos;");
	return result;
}

godot::String sanitize_profile_name(const godot::String &ssid) {
	godot::String profile = ssid.strip_edges();
	if (profile.is_empty()) {
		profile = "WifiGD_Hidden";
	}
	// Windows profile names cannot contain certain characters; keep SSID readable when possible.
	const godot::String forbidden = "\\/:*?\"<>|";
	for (int i = 0; i < forbidden.length(); i++) {
		profile = profile.replace(forbidden.substr(i, 1), "_");
	}
	return profile;
}

godot::String map_security_type(const WLAN_AVAILABLE_NETWORK &entry) {
	if (!entry.bSecurityEnabled) {
		return "open";
	}

	switch (entry.dot11DefaultAuthAlgorithm) {
		case DOT11_AUTH_ALGO_RSNA:
			return "wpa2";
		case DOT11_AUTH_ALGO_RSNA_PSK:
			return "wpa2-psk";
		case DOT11_AUTH_ALGO_WPA:
			return "wpa";
		case DOT11_AUTH_ALGO_WPA_PSK:
			return "wpa-psk";
		case DOT11_AUTH_ALGO_80211_SHARED_KEY:
			return "wep";
		default:
			break;
	}
	return "secured";
}

int frequency_khz_to_mhz(ULONG frequency_khz) {
	if (frequency_khz == 0) {
		return 0;
	}
	return (int)((frequency_khz + 500) / 1000);
}

int channel_from_frequency_mhz(int frequency_mhz) {
	if (frequency_mhz >= 2412 && frequency_mhz <= 2484) {
		if (frequency_mhz == 2484) {
			return 14;
		}
		return (frequency_mhz - 2407) / 5;
	}
	if (frequency_mhz >= 5170 && frequency_mhz <= 5825) {
		return (frequency_mhz - 5000) / 5;
	}
	if (frequency_mhz >= 5955 && frequency_mhz <= 7115) {
		return (frequency_mhz - 5950) / 5;
	}
	return 0;
}

godot::String band_from_frequency_mhz(int frequency_mhz) {
	if (frequency_mhz >= 5925) {
		return "6";
	}
	if (frequency_mhz >= 5000) {
		return "5";
	}
	if (frequency_mhz >= 2400) {
		return "2.4";
	}
	return "";
}

godot::String build_wlan_profile_xml(const godot::String &profile_name, const godot::String &ssid, const godot::String &password, bool is_secured) {
	const godot::String escaped_profile = xml_escape(profile_name);
	const godot::String escaped_ssid = xml_escape(ssid);

	godot::String xml = "<?xml version=\"1.0\"?>\n"
						"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
						"    <name>" +
			escaped_profile + "</name>\n"
							  "    <SSIDConfig>\n"
							  "        <SSID>\n"
							  "            <name>" +
			escaped_ssid + "</name>\n"
						   "        </SSID>\n"
						   "    </SSIDConfig>\n"
						   "    <connectionType>ESS</connectionType>\n"
						   "    <connectionMode>auto</connectionMode>\n"
						   "    <MSM>\n"
						   "        <security>\n"
						   "            <authEncryption>\n";

	if (is_secured && !password.is_empty()) {
		xml += "                <authentication>WPA2PSK</authentication>\n"
			   "                <encryption>AES</encryption>\n"
			   "                <useOneX>false</useOneX>\n"
			   "            </authEncryption>\n"
			   "            <sharedKey>\n"
			   "                <keyType>passPhrase</keyType>\n"
			   "                <protected>false</protected>\n"
			   "                <keyMaterial>" +
				xml_escape(password) + "</keyMaterial>\n"
									   "            </sharedKey>\n";
	} else {
		xml += "                <authentication>open</authentication>\n"
			   "                <encryption>none</encryption>\n"
			   "                <useOneX>false</useOneX>\n"
			   "            </authEncryption>\n";
	}

	xml += "        </security>\n"
		   "    </MSM>\n"
		   "</WLANProfile>";
	return xml;
}

ConnectionState map_interface_state(WLAN_INTERFACE_STATE state) {
	switch (state) {
		case wlan_interface_state_connected:
			return ConnectionState::CONNECTED;
		case wlan_interface_state_associating:
		case wlan_interface_state_discovering:
		case wlan_interface_state_authenticating:
			return ConnectionState::CONNECTING;
		default:
			return ConnectionState::DISCONNECTED;
	}
}

struct IpAdapterSnapshot {
	godot::String friendly_name;
	godot::String description;
	godot::String mac_address;
	godot::String ip_address;
	godot::String gateway;
	godot::String dns_primary;
	GUID network_guid{};
	ULONG if_type = 0;
	IF_OPER_STATUS oper_status = IfOperStatusDown;
	bool has_network_guid = false;
};

std::vector<IpAdapterSnapshot> query_ip_adapters() {
	std::vector<IpAdapterSnapshot> snapshots;
	ULONG buffer_size = 15000;
	PIP_ADAPTER_ADDRESSES addresses = static_cast<PIP_ADAPTER_ADDRESSES>(malloc(buffer_size));
	if (addresses == nullptr) {
		return snapshots;
	}

	DWORD result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, addresses, &buffer_size);
	if (result == ERROR_BUFFER_OVERFLOW) {
		free(addresses);
		addresses = static_cast<PIP_ADAPTER_ADDRESSES>(malloc(buffer_size));
		if (addresses == nullptr) {
			return snapshots;
		}
		result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, addresses, &buffer_size);
	}

	if (result != NO_ERROR) {
		free(addresses);
		return snapshots;
	}

	for (PIP_ADAPTER_ADDRESSES current = addresses; current != nullptr; current = current->Next) {
		IpAdapterSnapshot snapshot;
		snapshot.friendly_name = wide_to_utf8(current->FriendlyName);
		snapshot.description = wide_to_utf8(current->Description);
		snapshot.if_type = current->IfType;
		snapshot.oper_status = current->OperStatus;
		snapshot.mac_address = format_mac_address(current->PhysicalAddress, current->PhysicalAddressLength);

		if (current->NetworkGuid != GUID{}) {
			snapshot.network_guid = current->NetworkGuid;
			snapshot.has_network_guid = true;
		}

		godot::String ipv4_address;
		godot::String ipv6_address;
		for (PIP_ADAPTER_UNICAST_ADDRESS unicast = current->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr == nullptr) {
				continue;
			}
			if (unicast->Address.lpSockaddr->sa_family == AF_INET && ipv4_address.is_empty()) {
				ipv4_address = sockaddr_to_ipv4_string(unicast->Address);
			} else if (unicast->Address.lpSockaddr->sa_family == AF_INET6 && ipv6_address.is_empty()) {
				ipv6_address = sockaddr_to_ipv6_string(unicast->Address);
			}
		}
		snapshot.ip_address = !ipv4_address.is_empty() ? ipv4_address : ipv6_address;

		for (PIP_ADAPTER_GATEWAY_ADDRESS gateway = current->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
			godot::String ip = sockaddr_to_ipv4_string(gateway->Address);
			if (!ip.is_empty()) {
				snapshot.gateway = ip;
				break;
			}
		}

		for (PIP_ADAPTER_DNS_SERVER_ADDRESS dns = current->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
			godot::String ip = sockaddr_to_ipv4_string(dns->Address);
			if (!ip.is_empty()) {
				snapshot.dns_primary = ip;
				break;
			}
		}

		snapshots.push_back(snapshot);
	}

	free(addresses);
	return snapshots;
}

const IpAdapterSnapshot *find_ip_adapter_for_guid(const std::vector<IpAdapterSnapshot> &snapshots, const GUID &guid) {
	for (const IpAdapterSnapshot &snapshot : snapshots) {
		if (snapshot.has_network_guid && IsEqualGUID(snapshot.network_guid, guid)) {
			return &snapshot;
		}
	}
	return nullptr;
}

const IpAdapterSnapshot *find_ip_adapter_for_wlan(
		const std::vector<IpAdapterSnapshot> &snapshots,
		const GUID &iface_guid,
		const godot::String &interface_description) {
	if (const IpAdapterSnapshot *by_guid = find_ip_adapter_for_guid(snapshots, iface_guid)) {
		return by_guid;
	}

	const godot::String desc_lower = interface_description.to_lower();
	const IpAdapterSnapshot *description_match = nullptr;
	const IpAdapterSnapshot *wifi_with_ip = nullptr;

	for (const IpAdapterSnapshot &snapshot : snapshots) {
		if (snapshot.if_type != IF_TYPE_IEEE80211) {
			continue;
		}

		if (!snapshot.ip_address.is_empty() && snapshot.oper_status == IfOperStatusUp) {
			wifi_with_ip = &snapshot;
		}

		if (!desc_lower.is_empty()) {
			const godot::String snapshot_desc = snapshot.description.to_lower();
			const godot::String snapshot_name = snapshot.friendly_name.to_lower();
			if (snapshot_desc == desc_lower || snapshot_name == desc_lower ||
					snapshot_desc.contains(desc_lower) || desc_lower.contains(snapshot_desc)) {
				description_match = &snapshot;
			}
		}
	}

	if (description_match != nullptr) {
		return description_match;
	}
	return wifi_with_ip;
}

bool resolve_interface_guid(
		HANDLE wlan_handle,
		const godot::String &adapter_id,
		GUID &resolved_guid,
		godot::String &resolved_id,
		godot::String &error_message) {
	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	const DWORD result = WlanEnumInterfaces(wlan_handle, nullptr, &interfaces);
	if (result != ERROR_SUCCESS) {
		error_message = report_win_failure("WlanEnumInterfaces", result);
		return false;
	}
	if (interfaces == nullptr || interfaces->dwNumberOfItems == 0) {
		if (interfaces != nullptr) {
			WlanFreeMemory(interfaces);
		}
		error_message = "No Wi-Fi interfaces available.";
		return false;
	}

	if (!adapter_id.is_empty()) {
		GUID requested_guid{};
		if (!string_to_guid(adapter_id, requested_guid)) {
			WlanFreeMemory(interfaces);
			error_message = "Invalid adapter_id. Expected a Windows interface GUID string.";
			return false;
		}

		for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
			if (IsEqualGUID(interfaces->InterfaceInfo[i].InterfaceGuid, requested_guid)) {
				resolved_guid = requested_guid;
				resolved_id = guid_to_string(requested_guid);
				WlanFreeMemory(interfaces);
				return true;
			}
		}

		WlanFreeMemory(interfaces);
		error_message = "Wi-Fi adapter not found: " + adapter_id;
		return false;
	}

	resolved_guid = interfaces->InterfaceInfo[0].InterfaceGuid;
	resolved_id = guid_to_string(resolved_guid);
	WlanFreeMemory(interfaces);
	return true;
}

bool set_interface_radio(HANDLE wlan_handle, const GUID &iface_guid, bool enabled, godot::String &error_message) {
	WLAN_RADIO_STATE *radio_state = nullptr;
	DWORD data_size = 0;
	DWORD result = WlanQueryInterface(
			wlan_handle,
			&iface_guid,
			wlan_intf_opcode_radio_state,
			nullptr,
			&data_size,
			reinterpret_cast<PVOID *>(&radio_state),
			nullptr);
	if (result != ERROR_SUCCESS || radio_state == nullptr) {
		error_message = report_win_failure("WlanQueryInterface radio_state", result);
		return false;
	}

	const DOT11_RADIO_STATE desired_state = enabled ? dot11_radio_state_on : dot11_radio_state_off;
	for (DWORD i = 0; i < radio_state->dwNumberOfPhys; i++) {
		radio_state->PhyRadioState[i].dot11SoftwareRadioState = desired_state;
	}

	result = WlanSetInterface(
			wlan_handle,
			&iface_guid,
			wlan_intf_opcode_radio_state,
			data_size,
			radio_state,
			nullptr);
	WlanFreeMemory(radio_state);

	if (result != ERROR_SUCCESS) {
		if (result == ERROR_ACCESS_DENIED) {
			log_windows_error_to_console("WlanSetInterface radio_state", result);
			error_message = friendly_win_error("WlanSetInterface radio_state", result);
		} else {
			error_message = report_win_failure("WlanSetInterface radio_state", result);
		}
		return false;
	}
	return true;
}

bool get_available_networks(HANDLE handle, const GUID &iface_guid, PWLAN_AVAILABLE_NETWORK_LIST &out_list) {
	out_list = nullptr;
	DWORD result = WlanGetAvailableNetworkList(handle, &iface_guid, kAvailableNetworkFlags, nullptr, &out_list);
	if (result == ERROR_SUCCESS && out_list != nullptr) {
		return true;
	}
	if (out_list != nullptr) {
		WlanFreeMemory(out_list);
		out_list = nullptr;
	}
	result = WlanGetAvailableNetworkList(handle, &iface_guid, 0, nullptr, &out_list);
	return result == ERROR_SUCCESS && out_list != nullptr;
}

void enrich_network_from_bss(WifiNetwork &network, const WLAN_BSS_ENTRY &bss, const godot::String &adapter_id) {
	network.bssid = format_dot11_bssid(bss.dot11Bssid);
	network.signal_strength = (int)bss.uLinkQuality;
	network.frequency_mhz = frequency_khz_to_mhz(bss.ulChCenterFrequency);
	network.channel = channel_from_frequency_mhz(network.frequency_mhz);
	network.band = band_from_frequency_mhz(network.frequency_mhz);
	network.adapter_id = adapter_id;
}

void collect_networks_from_list(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &iface_id,
		PWLAN_AVAILABLE_NETWORK_LIST available,
		std::map<godot::String, WifiNetwork> &deduped) {
	for (DWORD j = 0; j < available->dwNumberOfItems; j++) {
		const WLAN_AVAILABLE_NETWORK &entry = available->Network[j];
		if (entry.dot11Ssid.uSSIDLength == 0) {
			continue;
		}

		WifiNetwork network;
		network.ssid = godot::String::utf8(reinterpret_cast<const char *>(entry.dot11Ssid.ucSSID), entry.dot11Ssid.uSSIDLength);
		network.signal_strength = (int)entry.wlanSignalQuality;
		network.is_secured = entry.bSecurityEnabled != FALSE;
		network.is_connected = (entry.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
		network.security_type = map_security_type(entry);
		network.adapter_id = iface_id;

		PWLAN_BSS_LIST bss_list = nullptr;
		DOT11_SSID scan_ssid = entry.dot11Ssid;
		const DWORD bss_result = WlanGetNetworkBssList(
				handle,
				&iface_guid,
				&scan_ssid,
				dot11_BSS_type_infrastructure,
				entry.bSecurityEnabled,
				nullptr,
				&bss_list);
		if (bss_result == ERROR_SUCCESS && bss_list != nullptr && bss_list->dwNumberOfItems > 0) {
			const WLAN_BSS_ENTRY &best_bss = bss_list->wlanBssEntries[0];
			enrich_network_from_bss(network, best_bss, iface_id);
			WlanFreeMemory(bss_list);
		}

		const godot::String dedupe_key = network.ssid + "|" + iface_id;
		auto existing = deduped.find(dedupe_key);
		if (existing == deduped.end() || network.signal_strength > existing->second.signal_strength) {
			deduped[dedupe_key] = network;
		}
	}
}

void collect_networks_from_bss_list(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &iface_id,
		std::map<godot::String, WifiNetwork> &deduped) {
	PWLAN_BSS_LIST bss_list = nullptr;
	const DWORD result = WlanGetNetworkBssList(
			handle,
			&iface_guid,
			nullptr,
			dot11_BSS_type_infrastructure,
			FALSE,
			nullptr,
			&bss_list);
	if (result != ERROR_SUCCESS || bss_list == nullptr) {
		return;
	}

	for (DWORD i = 0; i < bss_list->dwNumberOfItems; i++) {
		const WLAN_BSS_ENTRY &bss = bss_list->wlanBssEntries[i];
		if (bss.dot11Ssid.uSSIDLength == 0) {
			continue;
		}

		WifiNetwork network;
		network.ssid = godot::String::utf8(
				reinterpret_cast<const char *>(bss.dot11Ssid.ucSSID),
				bss.dot11Ssid.uSSIDLength);
		network.is_secured = (bss.usCapabilityInformation & 0x0010) != 0;
		network.security_type = network.is_secured ? "secured" : "open";
		enrich_network_from_bss(network, bss, iface_id);

		const godot::String dedupe_key = network.ssid + "|" + network.bssid + "|" + iface_id;
		auto existing = deduped.find(dedupe_key);
		if (existing == deduped.end() || network.signal_strength > existing->second.signal_strength) {
			deduped[dedupe_key] = network;
		}
	}

	WlanFreeMemory(bss_list);
}

void poll_cached_networks(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &iface_id,
		std::map<godot::String, WifiNetwork> &deduped) {
	for (DWORD attempt = 0; attempt < kScanPollAttempts; attempt++) {
		PWLAN_AVAILABLE_NETWORK_LIST available = nullptr;
		if (get_available_networks(handle, iface_guid, available)) {
			collect_networks_from_list(handle, iface_guid, iface_id, available, deduped);
			WlanFreeMemory(available);
		}
		collect_networks_from_bss_list(handle, iface_guid, iface_id, deduped);
		if (!deduped.empty()) {
			return;
		}
		Sleep(kScanPollIntervalMs);
	}
}

void append_connected_network(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &iface_id,
		std::map<godot::String, WifiNetwork> &deduped) {
	PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
	DWORD attr_size = 0;
	const DWORD result = WlanQueryInterface(
			handle,
			&iface_guid,
			wlan_intf_opcode_current_connection,
			nullptr,
			&attr_size,
			reinterpret_cast<PVOID *>(&attrs),
			nullptr);
	if (result != ERROR_SUCCESS || attrs == nullptr) {
		return;
	}

	if (attrs->wlanAssociationAttributes.dot11Ssid.uSSIDLength > 0) {
		WifiNetwork network;
		network.ssid = godot::String::utf8(
				reinterpret_cast<const char *>(attrs->wlanAssociationAttributes.dot11Ssid.ucSSID),
				attrs->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
		network.bssid = format_dot11_bssid(attrs->wlanAssociationAttributes.dot11Bssid);
		network.is_connected = true;
		network.is_secured = true;
		network.security_type = "connected";
		network.signal_strength = 100;
		network.adapter_id = iface_id;

		const godot::String dedupe_key = network.ssid + "|" + iface_id;
		auto existing = deduped.find(dedupe_key);
		if (existing == deduped.end() || network.is_connected) {
			deduped[dedupe_key] = network;
		}
	}

	WlanFreeMemory(attrs);
}

void collect_scan_results(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &iface_id,
		std::map<godot::String, WifiNetwork> &deduped) {
	PWLAN_AVAILABLE_NETWORK_LIST available = nullptr;
	if (get_available_networks(handle, iface_guid, available)) {
		collect_networks_from_list(handle, iface_guid, iface_id, available, deduped);
		WlanFreeMemory(available);
	}
	collect_networks_from_bss_list(handle, iface_guid, iface_id, deduped);
}

bool is_connected_to_ssid(HANDLE handle, const GUID &iface_guid, const godot::String &ssid) {
	PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
	DWORD attr_size = 0;
	const DWORD result = WlanQueryInterface(
			handle,
			&iface_guid,
			wlan_intf_opcode_current_connection,
			nullptr,
			&attr_size,
			reinterpret_cast<PVOID *>(&attrs),
			nullptr);
	if (result != ERROR_SUCCESS || attrs == nullptr) {
		return false;
	}

	const bool connected = attrs->isState == wlan_interface_state_connected;
	const bool ssid_matches = dot11_ssid_equals(attrs->wlanAssociationAttributes.dot11Ssid, ssid);
	WlanFreeMemory(attrs);
	return connected && ssid_matches;
}

bool poll_connection_established(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &ssid,
		DWORD timeout_ms) {
	for (DWORD elapsed = 0; elapsed < timeout_ms; elapsed += kScanPollIntervalMs) {
		if (is_connected_to_ssid(handle, iface_guid, ssid)) {
			return true;
		}
		Sleep(kScanPollIntervalMs);
	}
	return is_connected_to_ssid(handle, iface_guid, ssid);
}

bool try_active_scan(
		HANDLE handle,
		const GUID &iface_guid,
		const godot::String &iface_id,
		std::map<godot::String, WifiNetwork> &deduped,
		godot::String &scan_error,
		WlanNotificationWaiter *waiter) {
	if (waiter != nullptr) {
		waiter->begin_scan_wait(iface_guid);
	}

	const DWORD result = WlanScan(handle, &iface_guid, nullptr, nullptr, nullptr);
	if (result != ERROR_SUCCESS) {
		scan_error = format_scan_error(result);
		return false;
	}

	if (waiter != nullptr) {
		waiter->wait_for_scan_complete(kScanNotificationTimeoutMs);
	} else {
		Sleep(kScanPollIntervalMs * 3);
	}

	collect_scan_results(handle, iface_guid, iface_id, deduped);
	return true;
}

} // namespace

NetworkBackendWindows::NetworkBackendWindows() {
	notification_waiter = std::make_unique<WlanNotificationWaiter>();
	ensure_wlan_handle();
}

NetworkBackendWindows::~NetworkBackendWindows() {
	if (wlan_handle != nullptr) {
		if (notification_waiter) {
			notification_waiter->unregister(static_cast<HANDLE>(wlan_handle));
		}
		WlanCloseHandle(static_cast<HANDLE>(wlan_handle), nullptr);
		wlan_handle = nullptr;
	}
	notification_waiter.reset();
}

void NetworkBackendWindows::set_error(const godot::String &message) {
	last_error = message;
}

bool NetworkBackendWindows::ensure_wlan_handle() {
	if (wlan_handle != nullptr) {
		return true;
	}

	HANDLE handle = nullptr;
	DWORD negotiated_version = 0;
	const DWORD result = WlanOpenHandle(kWlanApiVersion, nullptr, &negotiated_version, &handle);
	if (result != ERROR_SUCCESS) {
		set_error(report_win_failure("WlanOpenHandle", result));
		return false;
	}

	wlan_handle = handle;
	if (notification_waiter) {
		notification_waiter->ensure_registered(handle);
	}
	return true;
}

bool NetworkBackendWindows::is_wifi_enabled() {
	if (!ensure_wlan_handle()) {
		return false;
	}

	WifiRadioState state;
	godot::String error_message;
	if (!collect_wifi_radio_state(static_cast<HANDLE>(wlan_handle), state, error_message)) {
		set_error(error_message);
		return false;
	}

	last_error = "";
	return state.enabled;
}

bool NetworkBackendWindows::set_wifi_enabled(bool enabled) {
	if (!ensure_wlan_handle()) {
		return false;
	}

	HANDLE handle = static_cast<HANDLE>(wlan_handle);
	WifiRadioState current_state;
	godot::String error_message;
	if (!collect_wifi_radio_state(handle, current_state, error_message)) {
		set_error(error_message);
		return false;
	}

	if (enabled && !current_state.hardware_enabled) {
		set_error("Wi-Fi is blocked by hardware (check physical switch or airplane mode).");
		return false;
	}

	if (radio_reached_requested_state(current_state, enabled)) {
		last_error = "";
		return true;
	}

	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	DWORD result = WlanEnumInterfaces(handle, nullptr, &interfaces);
	if (result != ERROR_SUCCESS || interfaces == nullptr || interfaces->dwNumberOfItems == 0) {
		if (interfaces != nullptr) {
			WlanFreeMemory(interfaces);
		}
		set_error("No Wi-Fi interfaces available.");
		return false;
	}

	bool success = false;
	for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
		godot::String iface_error;
		if (set_interface_radio(handle, interfaces->InterfaceInfo[i].InterfaceGuid, enabled, iface_error)) {
			success = true;
		} else if (!success) {
			set_error(iface_error);
		}
	}

	WlanFreeMemory(interfaces);
	if (!success) {
		return false;
	}

	if (wait_for_wifi_radio_state(handle, enabled, kRadioSetTimeoutMs)) {
		last_error = "";
		return true;
	}

	if (!collect_wifi_radio_state(handle, current_state, error_message)) {
		set_error(error_message);
		return false;
	}
	if (radio_reached_requested_state(current_state, enabled)) {
		last_error = "";
		return true;
	}

	set_error(describe_radio_set_failure(current_state, enabled));
	return false;
}

WifiRadioState NetworkBackendWindows::get_wifi_radio_state() {
	WifiRadioState state;
	if (!ensure_wlan_handle()) {
		return state;
	}

	godot::String error_message;
	if (!collect_wifi_radio_state(static_cast<HANDLE>(wlan_handle), state, error_message)) {
		set_error(error_message);
		return WifiRadioState();
	}

	last_error = "";
	return state;
}

std::vector<WifiNetwork> NetworkBackendWindows::scan_wifi_networks(const godot::String &adapter_id) {
	std::vector<WifiNetwork> networks;
	if (!ensure_wlan_handle()) {
		return networks;
	}

	HANDLE handle = static_cast<HANDLE>(wlan_handle);
	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	DWORD result = WlanEnumInterfaces(handle, nullptr, &interfaces);
	if (result != ERROR_SUCCESS) {
		set_error(report_win_failure("WlanEnumInterfaces", result));
		return networks;
	}
	if (interfaces == nullptr || interfaces->dwNumberOfItems == 0) {
		if (interfaces != nullptr) {
			WlanFreeMemory(interfaces);
		}
		set_error("No Wi-Fi interfaces available.");
		return networks;
	}

	if (!adapter_id.is_empty()) {
		bool adapter_found = false;
		for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
			if (guid_strings_equal(guid_to_string(interfaces->InterfaceInfo[i].InterfaceGuid), adapter_id)) {
				adapter_found = true;
				break;
			}
		}
		if (!adapter_found) {
			WlanFreeMemory(interfaces);
			set_error("Selected adapter is not a Wi-Fi interface. Choose a Wi-Fi adapter from the list.");
			return networks;
		}
	}

	std::map<godot::String, WifiNetwork> deduped;
	godot::String last_scan_error;

	for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
		const WLAN_INTERFACE_INFO &iface = interfaces->InterfaceInfo[i];
		const godot::String iface_id = guid_to_string(iface.InterfaceGuid);

		if (!adapter_id.is_empty() && !guid_strings_equal(iface_id, adapter_id)) {
			continue;
		}

		PWLAN_AVAILABLE_NETWORK_LIST available = nullptr;
		if (get_available_networks(handle, iface.InterfaceGuid, available)) {
			collect_networks_from_list(handle, iface.InterfaceGuid, iface_id, available, deduped);
			WlanFreeMemory(available);
		}
		collect_networks_from_bss_list(handle, iface.InterfaceGuid, iface_id, deduped);
		append_connected_network(handle, iface.InterfaceGuid, iface_id, deduped);

		if (deduped.empty()) {
			godot::String scan_error;
			WlanNotificationWaiter *waiter = notification_waiter.get();
			if (!try_active_scan(handle, iface.InterfaceGuid, iface_id, deduped, scan_error, waiter) && !scan_error.is_empty()) {
				last_scan_error = scan_error;
			}
		}

		if (deduped.empty()) {
			poll_cached_networks(handle, iface.InterfaceGuid, iface_id, deduped);
			append_connected_network(handle, iface.InterfaceGuid, iface_id, deduped);
		}
	}

	WlanFreeMemory(interfaces);

	for (const auto &pair : deduped) {
		networks.push_back(pair.second);
	}

	std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
		return a.signal_strength > b.signal_strength;
	});

	if (networks.empty() && !last_scan_error.is_empty()) {
		set_error(last_scan_error);
	} else if (!networks.empty() && !last_scan_error.is_empty()) {
		last_error = "Using cached networks. Active scan was unavailable.";
	} else {
		last_error = "";
	}
	return networks;
}

bool NetworkBackendWindows::connect_to_wifi(const godot::String &ssid, const godot::String &password, const godot::String &adapter_id) {
	if (ssid.strip_edges().is_empty()) {
		set_error("SSID cannot be empty.");
		return false;
	}
	if (!ensure_wlan_handle()) {
		return false;
	}

	HANDLE handle = static_cast<HANDLE>(wlan_handle);
	GUID iface_guid{};
	godot::String resolved_id;
	godot::String error_message;
	if (!resolve_interface_guid(handle, adapter_id, iface_guid, resolved_id, error_message)) {
		set_error(error_message);
		return false;
	}

	const godot::String profile_name = sanitize_profile_name(ssid);
	const bool is_secured = !password.is_empty();
	const godot::String profile_xml = build_wlan_profile_xml(profile_name, ssid, password, is_secured);

	godot::CharString profile_utf8 = profile_xml.utf8();
	const int wide_size = MultiByteToWideChar(CP_UTF8, 0, profile_utf8.get_data(), -1, nullptr, 0);
	if (wide_size <= 0) {
		set_error("Failed to convert WLAN profile XML to wide string.");
		return false;
	}
	std::vector<wchar_t> wide_profile((size_t)wide_size);
	MultiByteToWideChar(CP_UTF8, 0, profile_utf8.get_data(), -1, wide_profile.data(), wide_size);

	DWORD flags = WLAN_PROFILE_USER;
	DWORD reason_code = 0;
	DWORD result = WlanSetProfile(
			handle,
			&iface_guid,
			flags,
			wide_profile.data(),
			nullptr,
			TRUE,
			nullptr,
			&reason_code);
	if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS) {
		set_error(report_win_failure("WlanSetProfile", result, reason_code));
		return false;
	}

	godot::CharString profile_name_utf8 = profile_name.utf8();
	const int profile_wide_size = MultiByteToWideChar(CP_UTF8, 0, profile_name_utf8.get_data(), -1, nullptr, 0);
	if (profile_wide_size <= 0) {
		set_error("Failed to convert profile name to wide string.");
		return false;
	}
	std::vector<wchar_t> wide_profile_name((size_t)profile_wide_size);
	MultiByteToWideChar(CP_UTF8, 0, profile_name_utf8.get_data(), -1, wide_profile_name.data(), profile_wide_size);

	WLAN_CONNECTION_PARAMETERS params{};
	params.wlanConnectionMode = wlan_connection_mode_profile;
	params.strProfile = wide_profile_name.data();
	params.pDot11Ssid = nullptr;
	params.dot11BssType = dot11_BSS_type_infrastructure;
	params.dwFlags = 0;

	if (notification_waiter) {
		notification_waiter->begin_connect_wait(iface_guid, ssid);
	}

	result = WlanConnect(handle, &iface_guid, &params, nullptr);
	if (result != ERROR_SUCCESS) {
		set_error(report_win_failure("WlanConnect", result));
		return false;
	}

	if (notification_waiter) {
		const WlanConnectWaitResult wait_result =
				notification_waiter->wait_for_connect_complete(kConnectNotificationTimeoutMs);

		if (wait_result.completed && wait_result.success) {
			last_error = "";
			return true;
		}

		if (poll_connection_established(handle, iface_guid, ssid, kConnectPollFallbackMs)) {
			last_error = "";
			return true;
		}

		if (wait_result.completed && !wait_result.success) {
			const godot::String reason = wlan_reason_text(wait_result.reason_code);
			log_windows_error_to_console("WlanConnect", 0, wait_result.reason_code);
			if (reason.is_empty()) {
				set_error("Could not connect to the network. The connection attempt failed.");
			} else {
				set_error("Could not connect to the network: " + reason);
			}
			return false;
		}

		set_error("Connection timed out. The network may be out of range or the password may be incorrect.");
		return false;
	}

	if (poll_connection_established(handle, iface_guid, ssid, kConnectNotificationTimeoutMs)) {
		last_error = "";
		return true;
	}

	set_error("Connection timed out. The network may be out of range or the password may be incorrect.");
	return false;
}

bool NetworkBackendWindows::disconnect_from_wifi(const godot::String &adapter_id) {
	if (!ensure_wlan_handle()) {
		return false;
	}

	HANDLE handle = static_cast<HANDLE>(wlan_handle);
	GUID iface_guid{};
	godot::String resolved_id;
	godot::String error_message;
	if (!resolve_interface_guid(handle, adapter_id, iface_guid, resolved_id, error_message)) {
		set_error(error_message);
		return false;
	}

	const DWORD result = WlanDisconnect(handle, &iface_guid, nullptr);
	if (result != ERROR_SUCCESS) {
		set_error(report_win_failure("WlanDisconnect", result));
		return false;
	}

	last_error = "";
	return true;
}

ConnectivityInfo NetworkBackendWindows::get_connectivity_info() {
	ConnectivityInfo info;
	if (!ensure_wlan_handle()) {
		return info;
	}

	HANDLE handle = static_cast<HANDLE>(wlan_handle);
	const std::vector<IpAdapterSnapshot> ip_adapters = query_ip_adapters();

	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	DWORD result = WlanEnumInterfaces(handle, nullptr, &interfaces);
	if (result != ERROR_SUCCESS || interfaces == nullptr || interfaces->dwNumberOfItems == 0) {
		if (interfaces != nullptr) {
			WlanFreeMemory(interfaces);
		}
		return info;
	}

	const WLAN_INTERFACE_INFO *active_iface = nullptr;
	for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
		const WLAN_INTERFACE_STATE state = interfaces->InterfaceInfo[i].isState;
		if (state == wlan_interface_state_connected || state == wlan_interface_state_associating) {
			active_iface = &interfaces->InterfaceInfo[i];
			break;
		}
	}
	if (active_iface == nullptr) {
		active_iface = &interfaces->InterfaceInfo[0];
	}

	info.state = map_interface_state(active_iface->isState);
	info.is_wifi_connected = active_iface->isState == wlan_interface_state_connected;

	PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
	DWORD attr_size = 0;
	result = WlanQueryInterface(
			handle,
			&active_iface->InterfaceGuid,
			wlan_intf_opcode_current_connection,
			nullptr,
			&attr_size,
			reinterpret_cast<PVOID *>(&attrs),
			nullptr);
	if (result == ERROR_SUCCESS && attrs != nullptr) {
		info.connected_ssid = godot::String::utf8(
				reinterpret_cast<const char *>(attrs->wlanAssociationAttributes.dot11Ssid.ucSSID),
				attrs->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
		WlanFreeMemory(attrs);
	}

	const godot::String iface_description = wide_to_utf8(active_iface->strInterfaceDescription);
	if (const IpAdapterSnapshot *ip_adapter =
				find_ip_adapter_for_wlan(ip_adapters, active_iface->InterfaceGuid, iface_description)) {
		info.local_ip = ip_adapter->ip_address;
		info.gateway = ip_adapter->gateway;
		info.dns_primary = ip_adapter->dns_primary;
		info.has_internet = !info.local_ip.is_empty() && !info.gateway.is_empty();
	}

	WlanFreeMemory(interfaces);
	return info;
}

std::vector<NetworkAdapter> NetworkBackendWindows::get_network_adapters() {
	std::vector<NetworkAdapter> adapters;
	const std::vector<IpAdapterSnapshot> ip_adapters = query_ip_adapters();

	if (ensure_wlan_handle()) {
		HANDLE handle = static_cast<HANDLE>(wlan_handle);
		PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
		const DWORD result = WlanEnumInterfaces(handle, nullptr, &interfaces);
		if (result == ERROR_SUCCESS && interfaces != nullptr) {
			for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
				const WLAN_INTERFACE_INFO &iface = interfaces->InterfaceInfo[i];
				NetworkAdapter adapter;
				adapter.id = guid_to_string(iface.InterfaceGuid);
				adapter.description = wide_to_utf8(iface.strInterfaceDescription);
				adapter.name = adapter.description;
				adapter.type = AdapterType::WIFI;
				adapter.is_up = iface.isState != wlan_interface_state_not_ready;
				adapter.is_connected = iface.isState == wlan_interface_state_connected;

				const godot::String iface_description = wide_to_utf8(iface.strInterfaceDescription);
				if (const IpAdapterSnapshot *ip_adapter =
							find_ip_adapter_for_wlan(ip_adapters, iface.InterfaceGuid, iface_description)) {
					if (!ip_adapter->friendly_name.is_empty()) {
						adapter.name = ip_adapter->friendly_name;
					}
					adapter.mac_address = ip_adapter->mac_address;
					adapter.ip_address = ip_adapter->ip_address;
					adapter.gateway = ip_adapter->gateway;
					adapter.dns_primary = ip_adapter->dns_primary;
					adapter.is_up = ip_adapter->oper_status == IfOperStatusUp;
				}

				adapters.push_back(adapter);
			}
			WlanFreeMemory(interfaces);
		}
	}

	for (const IpAdapterSnapshot &snapshot : ip_adapters) {
		if (snapshot.if_type == IF_TYPE_IEEE80211) {
			continue;
		}

		NetworkAdapter adapter;
		adapter.id = snapshot.has_network_guid ? guid_to_string(snapshot.network_guid) : snapshot.friendly_name;
		adapter.name = snapshot.friendly_name;
		adapter.description = snapshot.description;
		adapter.mac_address = snapshot.mac_address;
		adapter.ip_address = snapshot.ip_address;
		adapter.gateway = snapshot.gateway;
		adapter.dns_primary = snapshot.dns_primary;
		adapter.is_up = snapshot.oper_status == IfOperStatusUp;
		adapter.is_connected = adapter.is_up && !snapshot.ip_address.is_empty();

		if (snapshot.if_type == IF_TYPE_ETHERNET_CSMACD) {
			adapter.type = AdapterType::ETHERNET;
		} else if (snapshot.if_type == IF_TYPE_SOFTWARE_LOOPBACK) {
			adapter.type = AdapterType::VIRTUAL;
		} else {
			adapter.type = AdapterType::OTHER;
		}

		adapters.push_back(adapter);
	}

	last_error = "";
	return adapters;
}

godot::String NetworkBackendWindows::get_last_error() const {
	return last_error;
}

} // namespace wifigd