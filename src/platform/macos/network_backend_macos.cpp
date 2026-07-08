#include "network_backend_macos.h"

// macOS implementation uses CoreWLAN (CWWiFiClient) and SystemConfiguration.
// Link with -framework CoreWLAN -framework SystemConfiguration -framework Network

namespace wifigd {

void NetworkBackendMacOS::set_error(const godot::String &message) {
	last_error = message;
}

bool NetworkBackendMacOS::is_wifi_enabled() {
	set_error("macOS Wi-Fi radio state query is not yet implemented. Planned: CoreWLAN.");
	return false;
}

bool NetworkBackendMacOS::set_wifi_enabled(bool enabled) {
	set_error(enabled
			? "macOS Wi-Fi radio enable is not yet implemented."
			: "macOS Wi-Fi radio disable is not yet implemented.");
	return false;
}

WifiRadioState NetworkBackendMacOS::get_wifi_radio_state() {
	WifiRadioState state;
	state.permission = "no";
	state.can_toggle = false;
	return state;
}

std::vector<WifiNetwork> NetworkBackendMacOS::scan_wifi_networks(const godot::String &adapter_id) {
	set_error("macOS Wi-Fi scan is not yet implemented. Planned: CWInterface scanForNetworksWithName.");
	(void)adapter_id;
	return {};
}

bool NetworkBackendMacOS::connect_to_wifi(const godot::String &ssid, const godot::String &password, const godot::String &adapter_id) {
	set_error(vformat("macOS connect to '%s' is not yet implemented.", ssid));
	(void)password;
	(void)adapter_id;
	return false;
}

bool NetworkBackendMacOS::disconnect_from_wifi(const godot::String &adapter_id) {
	set_error("macOS Wi-Fi disconnect is not yet implemented.");
	(void)adapter_id;
	return false;
}

ConnectivityInfo NetworkBackendMacOS::get_connectivity_info() {
	set_error("macOS connectivity info is not yet implemented.");
	return {};
}

std::vector<NetworkAdapter> NetworkBackendMacOS::get_network_adapters() {
	set_error("macOS adapter enumeration is not yet implemented. Planned: getifaddrs.");
	return {};
}

godot::String NetworkBackendMacOS::get_last_error() const {
	return last_error;
}

} // namespace wifigd