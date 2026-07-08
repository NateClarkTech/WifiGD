#pragma once

#include "../network_types.h"

#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <vector>

namespace wifigd {

class NetworkBackend {
public:
	virtual ~NetworkBackend() = default;

	virtual bool is_wifi_enabled() = 0;
	virtual bool set_wifi_enabled(bool enabled) = 0;
	virtual WifiRadioState get_wifi_radio_state() = 0;

	virtual std::vector<WifiNetwork> scan_wifi_networks(const godot::String &adapter_id) = 0;
	virtual bool connect_to_wifi(const godot::String &ssid, const godot::String &password, const godot::String &adapter_id) = 0;
	virtual bool disconnect_from_wifi(const godot::String &adapter_id) = 0;

	virtual ConnectivityInfo get_connectivity_info() = 0;
	virtual std::vector<NetworkAdapter> get_network_adapters() = 0;

	virtual godot::String get_last_error() const = 0;
};

std::unique_ptr<NetworkBackend> create_network_backend();

} // namespace wifigd