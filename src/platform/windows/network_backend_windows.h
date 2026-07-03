#pragma once

#include "../network_backend.h"

namespace wifigd {

class NetworkBackendWindows : public NetworkBackend {
private:
	godot::String last_error;
	void *wlan_handle = nullptr; // HANDLE, stored as void* to avoid windows.h in header

	bool ensure_wlan_handle();
	void set_error(const godot::String &message);

public:
	NetworkBackendWindows();
	~NetworkBackendWindows() override;

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