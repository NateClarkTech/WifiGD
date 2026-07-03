#include "network_backend_linux.h"

// Linux implementation uses NetworkManager (nmcli) and sysfs/netlink.
// Requires NetworkManager and polkit permissions for connect/disconnect.

namespace wifigd {

void NetworkBackendLinux::set_error(const godot::String &message) {
	last_error = message;
}

bool NetworkBackendLinux::is_wifi_enabled() {
	set_error("Linux Wi-Fi radio state query is not yet implemented. Use NetworkManager D-Bus.");
	return false;
}

bool NetworkBackendLinux::set_wifi_enabled(bool enabled) {
	set_error(enabled
			? "Linux Wi-Fi radio enable is not yet implemented."
			: "Linux Wi-Fi radio disable is not yet implemented.");
	return false;
}

std::vector<WifiNetwork> NetworkBackendLinux::scan_wifi_networks(const godot::String &adapter_id) {
	set_error("Linux Wi-Fi scan is not yet implemented. Planned: NetworkManager via libnm.");
	(void)adapter_id;
	return {};
}

bool NetworkBackendLinux::connect_to_wifi(const godot::String &ssid, const godot::String &password, const godot::String &adapter_id) {
	set_error(vformat("Linux connect to '%s' is not yet implemented. Planned: NetworkManager via libnm.", ssid));
	(void)password;
	(void)adapter_id;
	return false;
}

bool NetworkBackendLinux::disconnect_from_wifi(const godot::String &adapter_id) {
	set_error("Linux Wi-Fi disconnect is not yet implemented. Planned: NetworkManager via libnm.");
	(void)adapter_id;
	return false;
}

ConnectivityInfo NetworkBackendLinux::get_connectivity_info() {
	set_error("Linux connectivity info is not yet implemented.");
	return {};
}

std::vector<NetworkAdapter> NetworkBackendLinux::get_network_adapters() {
	set_error("Linux adapter enumeration is not yet implemented. Planned: getifaddrs / rtnetlink.");
	return {};
}

godot::String NetworkBackendLinux::get_last_error() const {
	return last_error;
}

} // namespace wifigd