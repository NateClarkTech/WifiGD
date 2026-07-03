#include "network_types.h"

#include <godot_cpp/variant/variant.hpp>

namespace wifigd {

godot::String adapter_type_to_string(AdapterType type) {
	switch (type) {
		case AdapterType::WIFI:
			return "wifi";
		case AdapterType::ETHERNET:
			return "ethernet";
		case AdapterType::VIRTUAL:
			return "virtual";
		case AdapterType::OTHER:
			return "other";
		default:
			return "unknown";
	}
}

godot::String connection_state_to_string(ConnectionState state) {
	switch (state) {
		case ConnectionState::CONNECTING:
			return "connecting";
		case ConnectionState::CONNECTED:
			return "connected";
		case ConnectionState::FAILED:
			return "failed";
		default:
			return "disconnected";
	}
}

godot::Dictionary WifiNetwork::to_dict() const {
	godot::Dictionary dict;
	dict[godot::Variant("ssid")] = ssid;
	dict[godot::Variant("bssid")] = bssid;
	dict[godot::Variant("signal_strength")] = signal_strength;
	dict[godot::Variant("is_secured")] = is_secured;
	dict[godot::Variant("is_connected")] = is_connected;
	dict[godot::Variant("security_type")] = security_type;
	dict[godot::Variant("frequency_mhz")] = frequency_mhz;
	dict[godot::Variant("channel")] = channel;
	dict[godot::Variant("band")] = band;
	dict[godot::Variant("adapter_id")] = adapter_id;
	return dict;
}

godot::Dictionary NetworkAdapter::to_dict() const {
	godot::Dictionary dict;
	dict[godot::Variant("id")] = id;
	dict[godot::Variant("name")] = name;
	dict[godot::Variant("description")] = description;
	dict[godot::Variant("mac_address")] = mac_address;
	dict[godot::Variant("ip_address")] = ip_address;
	dict[godot::Variant("gateway")] = gateway;
	dict[godot::Variant("dns_primary")] = dns_primary;
	dict[godot::Variant("type")] = adapter_type_to_string(type);
	dict[godot::Variant("is_up")] = is_up;
	dict[godot::Variant("is_connected")] = is_connected;
	return dict;
}

godot::Dictionary ConnectivityInfo::to_dict() const {
	godot::Dictionary dict;
	dict[godot::Variant("has_internet")] = has_internet;
	dict[godot::Variant("is_wifi_connected")] = is_wifi_connected;
	dict[godot::Variant("connected_ssid")] = connected_ssid;
	dict[godot::Variant("local_ip")] = local_ip;
	dict[godot::Variant("gateway")] = gateway;
	dict[godot::Variant("dns_primary")] = dns_primary;
	dict[godot::Variant("state")] = connection_state_to_string(state);
	return dict;
}

} // namespace wifigd