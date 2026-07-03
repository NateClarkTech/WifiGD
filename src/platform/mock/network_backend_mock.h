#pragma once

#include "../network_backend.h"

namespace wifigd {

class NetworkBackendMock : public NetworkBackend {
private:
	godot::String last_error;
	bool wifi_enabled = true;
	bool connected = true;
	int scan_call_count = 0;

public:
	bool is_wifi_enabled() override;
	bool set_wifi_enabled(bool enabled) override;

	std::vector<WifiNetwork> scan_wifi_networks(const godot::String &adapter_id) override;
	bool connect_to_wifi(const godot::String &ssid, const godot::String &password, const godot::String &adapter_id) override;
	bool disconnect_from_wifi(const godot::String &adapter_id) override;

	ConnectivityInfo get_connectivity_info() override;
	std::vector<NetworkAdapter> get_network_adapters() override;

	godot::String get_last_error() const override;
};

} // namespace wifigd