class_name WifiTestHelpers

const MOCK_ENV_VAR := "WIFIGD_MOCK_BACKEND"

const ENV_WIFI_SSID := "WIFI_SSID"
const ENV_WIFI_PASSWORD := "WIFI_PASSWORD"
const ENV_WIFI_ADAPTER_ID := "WIFI_ADAPTER_ID"


static func create_mock_manager(parent: Node) -> WifiManager:
	OS.set_environment(MOCK_ENV_VAR, "1")
	var manager := WifiManager.new()
	parent.add_child(manager)
	await parent.get_tree().process_frame
	return manager


static func free_manager(manager: WifiManager) -> void:
	if manager != null and is_instance_valid(manager):
		manager.queue_free()
		await Engine.get_main_loop().process_frame


static func await_signal(signal_obj: Object, signal_name: StringName, timeout_sec: float = 5.0):
	var emitted := [false]
	var args := [null]

	var callable := func(...p_args):
		emitted[0] = true
		args[0] = p_args

	signal_obj.connect(signal_name, callable)
	var elapsed := 0.0
	while not emitted[0] and elapsed < timeout_sec:
		await Engine.get_main_loop().process_frame
		elapsed += Engine.get_main_loop().root.get_process_delta_time()

	if not emitted[0]:
		push_error("Timed out waiting for signal: %s" % signal_name)
		return null

	return args[0]


static func assert_wifi_network_shape(gut, network: Dictionary) -> void:
	var required := [
		"ssid", "bssid", "signal_strength", "is_secured", "is_connected",
		"security_type", "frequency_mhz", "channel", "band", "adapter_id",
	]
	for key in required:
		gut.assert_true(network.has(key), "WifiNetwork missing key: %s" % key)


static func assert_adapter_shape(gut, adapter: Dictionary) -> void:
	var required := [
		"id", "name", "description", "mac_address", "ip_address",
		"gateway", "dns_primary", "type", "is_up", "is_connected",
	]
	for key in required:
		gut.assert_true(adapter.has(key), "NetworkAdapter missing key: %s" % key)


static func assert_connectivity_shape(gut, info: Dictionary) -> void:
	var required := [
		"has_internet", "is_wifi_connected", "connected_ssid", "local_ip",
		"gateway", "dns_primary", "state",
	]
	for key in required:
		gut.assert_true(info.has(key), "ConnectivityInfo missing key: %s" % key)


static func load_wifi_env() -> Dictionary:
	var env := {
		ENV_WIFI_SSID: "",
		ENV_WIFI_PASSWORD: "",
		ENV_WIFI_ADAPTER_ID: "",
	}

	for key in env.keys():
		var from_os := OS.get_environment(key)
		if not from_os.is_empty():
			env[key] = from_os

	for path in _dotenv_paths():
		if FileAccess.file_exists(path):
			_merge_dotenv_file(path, env)
			break

	return env


static func _dotenv_paths() -> PackedStringArray:
	return PackedStringArray([
		ProjectSettings.globalize_path("res://.env"),
		ProjectSettings.globalize_path("res://../.env"),
	])


static func _merge_dotenv_file(path: String, env: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return

	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		if not line.contains("="):
			continue

		var parts := line.split("=", true, 1)
		var key := parts[0].strip_edges()
		var value := parts[1].strip_edges() if parts.size() > 1 else ""
		value = _strip_dotenv_quotes(value)

		if env.has(key) and env[key].is_empty():
			env[key] = value


static func disconnect_and_wait(manager: WifiManager, adapter_id: String = "", wait_sec: float = 2.0) -> void:
	if manager == null or not is_instance_valid(manager):
		return
	manager.disconnect_from_wifi(adapter_id)
	if Engine.get_main_loop() != null:
		await Engine.get_main_loop().create_timer(wait_sec).timeout


static func _strip_dotenv_quotes(value: String) -> String:
	if value.length() >= 2:
		if (value.begins_with('"') and value.ends_with('"')) or (
			value.begins_with("'") and value.ends_with("'")
		):
			return value.substr(1, value.length() - 2)
	return value