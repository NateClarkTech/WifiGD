#pragma once

#include "../network_backend.h"

struct _NMClient;
typedef struct _NMClient NMClient;

struct _NMDeviceWifi;
typedef struct _NMDeviceWifi NMDeviceWifi;

namespace wifigd {

class NetworkBackendLinux : public NetworkBackend {
private:
	NMClient *client = nullptr;
	godot::String last_error;

	void set_error(const godot::String &message);
	bool ensure_nm_available();
	NMDeviceWifi *resolve_wifi_device(const godot::String &adapter_id);

public:
	NetworkBackendLinux();
	~NetworkBackendLinux() override;

	bool is_wifi_enabled() override;
	bool set_wifi_enabled(bool enabled) override;
	WifiRadioState get_wifi_radio_state() override;

	std::vector<WifiNetwork> scan_wifi_networks(const godot::String &adapter_id) override;
	bool connect_to_wifi(const godot::String &ssid, const godot::String &password, const godot::String &adapter_id) override;
	bool disconnect_from_wifi(const godot::String &adapter_id) override;

	ConnectivityInfo get_connectivity_info() override;
	std::vector<NetworkAdapter> get_network_adapters() override;

	godot::String get_last_error() const override;
};

} // namespace wifigd