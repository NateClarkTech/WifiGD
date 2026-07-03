#include "network_backend_mock.h"

#include <thread>

namespace wifigd {

namespace {

WifiNetwork make_network(
		const godot::String &ssid,
		const godot::String &bssid,
		int strength,
		const godot::String &security,
		const godot::String &band,
		int frequency_mhz,
		int channel,
		bool is_connected) {
	WifiNetwork network;
	network.ssid = ssid;
	network.bssid = bssid;
	network.signal_strength = strength;
	network.is_secured = security != "open";
	network.is_connected = is_connected;
	network.security_type = is_connected ? "connected" : security;
	network.frequency_mhz = frequency_mhz;
	network.channel = channel;
	network.band = band;
	network.adapter_id = "wlan0";
	return network;
}

} // namespace

bool NetworkBackendMock::is_wifi_enabled() {
	last_error = "";
	return wifi_enabled;
}

bool NetworkBackendMock::set_wifi_enabled(bool enabled) {
	last_error = "";
	wifi_enabled = enabled;
	return true;
}

std::vector<WifiNetwork> NetworkBackendMock::scan_wifi_networks(const godot::String &adapter_id) {
	(void)adapter_id;
	scan_call_count++;
	last_error = "";

	// Simulate scan latency so async busy-guard tests can overlap calls.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::vector<WifiNetwork> networks;
	networks.push_back(make_network("TestNet", "AA:BB:CC:DD:EE:01", 82, "wpa2", "2.4", 2437, 6, connected));
	networks.push_back(make_network("TestNet", "AA:BB:CC:DD:EE:02", 74, "wpa2", "5", 5180, 36, false));
	networks.push_back(make_network("OpenNet", "11:22:33:44:55:66", 51, "open", "2.4", 2412, 1, false));
	return networks;
}

bool NetworkBackendMock::connect_to_wifi(
		const godot::String &ssid,
		const godot::String &password,
		const godot::String &adapter_id) {
	(void)adapter_id;
	last_error = "";

	if (ssid.is_empty()) {
		last_error = "SSID is required.";
		return false;
	}

	if (ssid == "BadNet") {
		last_error = "Authentication failed. Check the password.";
		return false;
	}

	if (!password.is_empty() && password != "test-password") {
		last_error = "Authentication failed. Check the password.";
		return false;
	}

	connected = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	return true;
}

bool NetworkBackendMock::disconnect_from_wifi(const godot::String &adapter_id) {
	(void)adapter_id;
	last_error = "";
	connected = false;
	return true;
}

ConnectivityInfo NetworkBackendMock::get_connectivity_info() {
	ConnectivityInfo info;
	info.is_wifi_connected = connected;
	info.connected_ssid = connected ? "TestNet" : "";
	info.local_ip = connected ? "192.168.1.42" : "";
	info.gateway = connected ? "192.168.1.1" : "";
	info.dns_primary = connected ? "1.1.1.1" : "";
	info.has_internet = connected;
	info.state = connected ? ConnectionState::CONNECTED : ConnectionState::DISCONNECTED;
	last_error = "";
	return info;
}

std::vector<NetworkAdapter> NetworkBackendMock::get_network_adapters() {
	last_error = "";

	NetworkAdapter wifi;
	wifi.id = "wlan0";
	wifi.name = "wlan0";
	wifi.description = "Mock Wi-Fi Adapter";
	wifi.mac_address = "AA:BB:CC:DD:EE:FF";
	wifi.ip_address = connected ? "192.168.1.42" : "";
	wifi.gateway = connected ? "192.168.1.1" : "";
	wifi.dns_primary = connected ? "1.1.1.1" : "";
	wifi.type = AdapterType::WIFI;
	wifi.is_up = true;
	wifi.is_connected = connected;

	NetworkAdapter ethernet;
	ethernet.id = "eth0";
	ethernet.name = "eth0";
	ethernet.description = "Mock Ethernet Adapter";
	ethernet.mac_address = "00:11:22:33:44:55";
	ethernet.ip_address = "10.0.0.5";
	ethernet.type = AdapterType::ETHERNET;
	ethernet.is_up = true;
	ethernet.is_connected = false;

	return { wifi, ethernet };
}

godot::String NetworkBackendMock::get_last_error() const {
	return last_error;
}

} // namespace wifigd