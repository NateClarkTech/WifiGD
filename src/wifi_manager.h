#pragma once

#include "platform/network_backend.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <mutex>

namespace godot {

class WifiManager : public Node {
	GDCLASS(WifiManager, Node)

private:
	std::unique_ptr<wifigd::NetworkBackend> backend;
	mutable std::mutex backend_mutex;

	bool scan_in_progress = false;
	bool connect_in_progress = false;
	bool disconnect_in_progress = false;
	bool adapters_in_progress = false;

	Array pending_networks;
	Error pending_error = OK;
	String pending_message;

	Dictionary cached_connectivity;
	Array cached_adapters;
	int last_connection_state = 0;
	bool last_wifi_enabled = false;
	double connectivity_poll_timer = 0.0;

	static constexpr double CONNECTIVITY_POLL_INTERVAL_SEC = 2.0;

	static void _scan_native_task(void *p_userdata);
	static void _connect_native_task(void *p_userdata);
	static void _disconnect_native_task(void *p_userdata);
	static void _adapters_native_task(void *p_userdata);

	void _emit_scan_completed();
	void _emit_connect_completed();
	void _emit_disconnect_completed();
	void _emit_adapters_updated();
	void _flush_console_logs() const;
	void _poll_connectivity();
	void _update_cached_connectivity(const Dictionary &info);
	void _update_cached_adapters(const Array &adapters);

protected:
	static void _bind_methods();

public:
	void _ready() override;
	void _exit_tree() override;
	void _process(double delta) override;
	WifiManager();
	~WifiManager() override;

	bool is_wifi_enabled() const;
	bool set_wifi_enabled(bool enabled);

	Array scan_wifi_networks(const String &adapter_id = "") const;
	void scan_wifi_networks_async(const String &adapter_id = "");

	Error connect_to_wifi(const String &ssid, const String &password = "", const String &adapter_id = "");
	void connect_to_wifi_async(const String &ssid, const String &password = "", const String &adapter_id = "");

	Error disconnect_from_wifi(const String &adapter_id = "");
	void disconnect_from_wifi_async(const String &adapter_id = "");

	Dictionary get_connectivity_info() const;
	Dictionary get_cached_connectivity() const;
	int get_connection_state() const;

	Array get_network_adapters() const;
	Array get_cached_adapters() const;
	void fetch_adapters_async();

	String get_last_error() const;
};

} // namespace godot