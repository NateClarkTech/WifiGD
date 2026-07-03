#include "wifi_manager.h"

#include "console_log.h"

#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <mutex>

namespace godot {

namespace {

struct ScanTaskData {
	WifiManager *manager = nullptr;
	String adapter_id;
};

struct ConnectTaskData {
	WifiManager *manager = nullptr;
	String ssid;
	String password;
	String adapter_id;
};

struct DisconnectTaskData {
	WifiManager *manager = nullptr;
	String adapter_id;
};

struct AdaptersTaskData {
	WifiManager *manager = nullptr;
};

Array networks_to_array(const std::vector<wifigd::WifiNetwork> &networks) {
	Array result;
	result.resize((int)networks.size());
	for (int i = 0; i < (int)networks.size(); i++) {
		result[i] = networks[i].to_dict();
	}
	return result;
}

Array adapters_to_array(const std::vector<wifigd::NetworkAdapter> &adapters) {
	Array result;
	result.resize((int)adapters.size());
	for (int i = 0; i < (int)adapters.size(); i++) {
		result[i] = adapters[i].to_dict();
	}
	return result;
}

} // namespace

WifiManager::WifiManager() {
	backend = wifigd::create_network_backend();
}

WifiManager::~WifiManager() = default;

void WifiManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_wifi_enabled"), &WifiManager::is_wifi_enabled);
	ClassDB::bind_method(D_METHOD("set_wifi_enabled", "enabled"), &WifiManager::set_wifi_enabled);

	ClassDB::bind_method(D_METHOD("scan_wifi_networks", "adapter_id"), &WifiManager::scan_wifi_networks, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("scan_wifi_networks_async", "adapter_id"), &WifiManager::scan_wifi_networks_async, DEFVAL(""));

	ClassDB::bind_method(D_METHOD("connect_to_wifi", "ssid", "password", "adapter_id"), &WifiManager::connect_to_wifi, DEFVAL(""), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("connect_to_wifi_async", "ssid", "password", "adapter_id"), &WifiManager::connect_to_wifi_async, DEFVAL(""), DEFVAL(""));

	ClassDB::bind_method(D_METHOD("disconnect_from_wifi", "adapter_id"), &WifiManager::disconnect_from_wifi, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("disconnect_from_wifi_async", "adapter_id"), &WifiManager::disconnect_from_wifi_async, DEFVAL(""));

	ClassDB::bind_method(D_METHOD("get_connectivity_info"), &WifiManager::get_connectivity_info);
	ClassDB::bind_method(D_METHOD("get_network_adapters"), &WifiManager::get_network_adapters);
	ClassDB::bind_method(D_METHOD("fetch_adapters_async"), &WifiManager::fetch_adapters_async);
	ClassDB::bind_method(D_METHOD("get_last_error"), &WifiManager::get_last_error);

	ClassDB::bind_method(D_METHOD("_emit_scan_completed"), &WifiManager::_emit_scan_completed);
	ClassDB::bind_method(D_METHOD("_emit_connect_completed"), &WifiManager::_emit_connect_completed);
	ClassDB::bind_method(D_METHOD("_emit_disconnect_completed"), &WifiManager::_emit_disconnect_completed);
	ClassDB::bind_method(D_METHOD("_emit_adapters_updated"), &WifiManager::_emit_adapters_updated);

	ADD_SIGNAL(MethodInfo("scan_completed", PropertyInfo(Variant::ARRAY, "networks"), PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("connect_completed", PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("disconnect_completed", PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("adapters_updated", PropertyInfo(Variant::ARRAY, "adapters"), PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));

	BIND_CONSTANT(static_cast<int>(wifigd::ConnectionState::DISCONNECTED));
	BIND_CONSTANT(static_cast<int>(wifigd::ConnectionState::CONNECTING));
	BIND_CONSTANT(static_cast<int>(wifigd::ConnectionState::CONNECTED));
	BIND_CONSTANT(static_cast<int>(wifigd::ConnectionState::FAILED));
}

bool WifiManager::is_wifi_enabled() const {
	std::lock_guard<std::mutex> lock(backend_mutex);
	return backend ? backend->is_wifi_enabled() : false;
}

bool WifiManager::set_wifi_enabled(bool enabled) {
	std::lock_guard<std::mutex> lock(backend_mutex);
	return backend ? backend->set_wifi_enabled(enabled) : false;
}

Array WifiManager::scan_wifi_networks(const String &adapter_id) const {
	Array networks;
	{
		std::lock_guard<std::mutex> lock(backend_mutex);
		if (!backend) {
			return networks;
		}
		networks = networks_to_array(backend->scan_wifi_networks(adapter_id));
	}
	_flush_console_logs();
	return networks;
}

void WifiManager::scan_wifi_networks_async(const String &adapter_id) {
	if (scan_in_progress) {
		emit_signal("scan_completed", Array(), ERR_BUSY, "A Wi-Fi scan is already in progress.");
		return;
	}

	scan_in_progress = true;
	ScanTaskData *task = new ScanTaskData();
	task->manager = this;
	task->adapter_id = adapter_id;
	WorkerThreadPool::get_singleton()->add_native_task(_scan_native_task, task, true, "WifiGD scan");
}

void WifiManager::_scan_native_task(void *p_userdata) {
	ScanTaskData *task = static_cast<ScanTaskData *>(p_userdata);
	WifiManager *manager = task->manager;
	const String adapter_id = task->adapter_id;
	delete task;

	Array networks;
	Error error = OK;
	String message;

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		if (!manager->backend) {
			error = ERR_UNAVAILABLE;
			message = "Network backend unavailable.";
		} else {
			networks = networks_to_array(manager->backend->scan_wifi_networks(adapter_id));
			message = manager->backend->get_last_error();
			if (networks.is_empty()) {
				error = message.is_empty() ? ERR_CANT_CONNECT : ERR_CANT_CONNECT;
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		manager->pending_networks = networks;
		manager->pending_error = error;
		manager->pending_message = message;
	}
	manager->call_deferred("_emit_scan_completed");
}

void WifiManager::_emit_scan_completed() {
	_flush_console_logs();
	scan_in_progress = false;
	emit_signal("scan_completed", pending_networks, pending_error, pending_message);
}

Error WifiManager::connect_to_wifi(const String &ssid, const String &password, const String &adapter_id) {
	Error error = OK;
	{
		std::lock_guard<std::mutex> lock(backend_mutex);
		if (!backend) {
			return ERR_UNAVAILABLE;
		}
		error = backend->connect_to_wifi(ssid, password, adapter_id) ? OK : ERR_CANT_CONNECT;
	}
	_flush_console_logs();
	return error;
}

void WifiManager::connect_to_wifi_async(const String &ssid, const String &password, const String &adapter_id) {
	if (connect_in_progress) {
		emit_signal("connect_completed", ERR_BUSY, "A connect operation is already in progress.");
		return;
	}

	connect_in_progress = true;
	ConnectTaskData *task = new ConnectTaskData();
	task->manager = this;
	task->ssid = ssid;
	task->password = password;
	task->adapter_id = adapter_id;
	WorkerThreadPool::get_singleton()->add_native_task(_connect_native_task, task, true, "WifiGD connect");
}

void WifiManager::_connect_native_task(void *p_userdata) {
	ConnectTaskData *task = static_cast<ConnectTaskData *>(p_userdata);
	WifiManager *manager = task->manager;
	const String ssid = task->ssid;
	const String password = task->password;
	const String adapter_id = task->adapter_id;
	delete task;

	Error error = OK;
	String message;

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		if (!manager->backend) {
			error = ERR_UNAVAILABLE;
			message = "Network backend unavailable.";
		} else if (!manager->backend->connect_to_wifi(ssid, password, adapter_id)) {
			error = ERR_CANT_CONNECT;
			message = manager->backend->get_last_error();
		}
	}

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		manager->pending_error = error;
		manager->pending_message = message;
	}
	manager->call_deferred("_emit_connect_completed");
}

void WifiManager::_emit_connect_completed() {
	_flush_console_logs();
	connect_in_progress = false;
	emit_signal("connect_completed", pending_error, pending_message);
}

Error WifiManager::disconnect_from_wifi(const String &adapter_id) {
	Error error = OK;
	{
		std::lock_guard<std::mutex> lock(backend_mutex);
		if (!backend) {
			return ERR_UNAVAILABLE;
		}
		error = backend->disconnect_from_wifi(adapter_id) ? OK : ERR_CANT_CONNECT;
	}
	_flush_console_logs();
	return error;
}

void WifiManager::disconnect_from_wifi_async(const String &adapter_id) {
	if (disconnect_in_progress) {
		emit_signal("disconnect_completed", ERR_BUSY, "A disconnect operation is already in progress.");
		return;
	}

	disconnect_in_progress = true;
	DisconnectTaskData *task = new DisconnectTaskData();
	task->manager = this;
	task->adapter_id = adapter_id;
	WorkerThreadPool::get_singleton()->add_native_task(_disconnect_native_task, task, true, "WifiGD disconnect");
}

void WifiManager::_disconnect_native_task(void *p_userdata) {
	DisconnectTaskData *task = static_cast<DisconnectTaskData *>(p_userdata);
	WifiManager *manager = task->manager;
	const String adapter_id = task->adapter_id;
	delete task;

	Error error = OK;
	String message;

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		if (!manager->backend) {
			error = ERR_UNAVAILABLE;
			message = "Network backend unavailable.";
		} else if (!manager->backend->disconnect_from_wifi(adapter_id)) {
			error = ERR_CANT_CONNECT;
			message = manager->backend->get_last_error();
		}
	}

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		manager->pending_error = error;
		manager->pending_message = message;
	}
	manager->call_deferred("_emit_disconnect_completed");
}

void WifiManager::_emit_disconnect_completed() {
	_flush_console_logs();
	disconnect_in_progress = false;
	emit_signal("disconnect_completed", pending_error, pending_message);
}

Dictionary WifiManager::get_connectivity_info() const {
	std::lock_guard<std::mutex> lock(backend_mutex);
	if (!backend) {
		return Dictionary();
	}
	return backend->get_connectivity_info().to_dict();
}

Array WifiManager::get_network_adapters() const {
	std::lock_guard<std::mutex> lock(backend_mutex);
	if (!backend) {
		return Array();
	}
	return adapters_to_array(backend->get_network_adapters());
}

void WifiManager::fetch_adapters_async() {
	if (adapters_in_progress) {
		emit_signal("adapters_updated", Array(), ERR_BUSY, "An adapter refresh is already in progress.");
		return;
	}

	adapters_in_progress = true;
	AdaptersTaskData *task = new AdaptersTaskData();
	task->manager = this;
	WorkerThreadPool::get_singleton()->add_native_task(_adapters_native_task, task, true, "WifiGD adapters");
}

void WifiManager::_adapters_native_task(void *p_userdata) {
	AdaptersTaskData *task = static_cast<AdaptersTaskData *>(p_userdata);
	WifiManager *manager = task->manager;
	delete task;

	Array adapters;
	Error error = OK;
	String message;

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		if (!manager->backend) {
			error = ERR_UNAVAILABLE;
			message = "Network backend unavailable.";
		} else {
			adapters = adapters_to_array(manager->backend->get_network_adapters());
			message = manager->backend->get_last_error();
			if (adapters.is_empty() && !message.is_empty()) {
				error = ERR_CANT_CONNECT;
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(manager->backend_mutex);
		manager->pending_networks = adapters;
		manager->pending_error = error;
		manager->pending_message = message;
	}
	manager->call_deferred("_emit_adapters_updated");
}

void WifiManager::_emit_adapters_updated() {
	_flush_console_logs();
	adapters_in_progress = false;
	emit_signal("adapters_updated", pending_networks, pending_error, pending_message);
}

void WifiManager::_flush_console_logs() const {
	const std::vector<godot::String> logs = wifigd::take_console_logs();
	for (const godot::String &log : logs) {
		UtilityFunctions::print(log);
	}
}

String WifiManager::get_last_error() const {
	std::lock_guard<std::mutex> lock(backend_mutex);
	return backend ? backend->get_last_error() : "Network backend unavailable.";
}

} // namespace godot