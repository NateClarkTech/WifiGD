#pragma once

#include "platform/network_backend.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <mutex>

namespace godot {

class WifiManager : public RefCounted {
	GDCLASS(WifiManager, RefCounted)

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

	static void _scan_native_task(void *p_userdata);
	static void _connect_native_task(void *p_userdata);
	static void _disconnect_native_task(void *p_userdata);
	static void _adapters_native_task(void *p_userdata);

	void _emit_scan_completed();
	void _emit_connect_completed();
	void _emit_disconnect_completed();
	void _emit_adapters_updated();
	void _flush_console_logs() const;

protected:
	static void _bind_methods();

public:
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
	Array get_network_adapters() const;
	void fetch_adapters_async();

	String get_last_error() const;
};

} // namespace godot