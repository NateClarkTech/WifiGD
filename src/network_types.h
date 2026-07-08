#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace wifigd {

enum class ConnectionState {
	DISCONNECTED,
	CONNECTING,
	CONNECTED,
	FAILED,
};

enum class AdapterType {
	UNKNOWN,
	WIFI,
	ETHERNET,
	VIRTUAL,
	OTHER,
};

struct WifiNetwork {
	godot::String ssid;
	godot::String bssid;
	int signal_strength = 0; // Normalized 0-100
	bool is_secured = false;
	bool is_connected = false;
	godot::String security_type; // WPA2, WPA3, Open, etc.
	int frequency_mhz = 0;
	int channel = 0;
	godot::String band; // "2.4", "5", "6"
	godot::String adapter_id;

	godot::Dictionary to_dict() const;
};

struct NetworkAdapter {
	godot::String id;
	godot::String name;
	godot::String description;
	godot::String mac_address;
	godot::String ip_address;
	godot::String gateway;
	godot::String dns_primary;
	AdapterType type = AdapterType::UNKNOWN;
	bool is_up = false;
	bool is_connected = false;

	godot::Dictionary to_dict() const;
};

struct ConnectivityInfo {
	bool has_internet = false;
	bool is_wifi_connected = false;
	godot::String connected_ssid;
	godot::String local_ip;
	godot::String gateway;
	godot::String dns_primary;
	ConnectionState state = ConnectionState::DISCONNECTED;

	godot::Dictionary to_dict() const;
};

struct WifiRadioState {
	bool enabled = false;
	bool software_enabled = false;
	bool hardware_enabled = false;
	bool can_toggle = false;
	godot::String permission; // "yes", "auth", "no", "unknown"

	godot::Dictionary to_dict() const;
};

godot::String adapter_type_to_string(AdapterType type);
godot::String connection_state_to_string(ConnectionState state);

} // namespace wifigd