class_name WifiTestHelpers

const MOCK_ENV_VAR := "WIFIGD_MOCK_BACKEND"

const ENV_WIFI_SSID := "WIFI_SSID"
const ENV_WIFI_PASSWORD := "WIFI_PASSWORD"
const ENV_WIFI_ADAPTER_ID := "WIFI_ADAPTER_ID"

const SUPPORTED_REAL_WIFI_PLATFORMS := ["Linux", "Windows"]


static func is_real_wifi_platform() -> bool:
	return OS.get_name() in SUPPORTED_REAL_WIFI_PLATFORMS


static func skip_unless_real_wifi_platform(gut) -> bool:
	if is_real_wifi_platform():
		return false
	gut.pending(
		"Real Wi-Fi tests run on Linux and Windows only (current platform: %s)." % OS.get_name()
	)
	return true


static func is_connect_permission_denied(message: String) -> bool:
	var lower := message.to_lower()
	if OS.get_name() == "Linux":
		return lower.contains("permission denied") or lower.contains("polkit")
	if OS.get_name() == "Windows":
		return (
			lower.contains("access denied")
			or lower.contains("location")
			or lower.contains("administrator")
		)
	return false


static func adapter_id_documentation() -> String:
	if OS.get_name() == "Linux":
		return "Linux interface name (e.g. wlan0), or empty for default"
	return "Windows Wi-Fi adapter GUID, or empty for default"


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
		# call_deferred completions (e.g. wifi_radio_set_completed) run on idle.
		await Engine.get_main_loop().idle_frame
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


static func assert_wifi_radio_shape(gut, state: Dictionary) -> void:
	var required := [
		"enabled", "software_enabled", "hardware_enabled", "can_toggle", "permission",
	]
	for key in required:
		gut.assert_true(state.has(key), "WifiRadioState missing key: %s" % key)


static func is_radio_permission_denied(message: String) -> bool:
	var lower := message.to_lower()
	if OS.get_name() == "Linux":
		return lower.contains("permission denied") or lower.contains("polkit")
	if OS.get_name() == "Windows":
		return (
			lower.contains("access denied")
			or lower.contains("administrator")
			or lower.contains("elevation")
		)
	return false


static func is_radio_hardware_blocked(state: Dictionary, message: String = "") -> bool:
	if not state.get("hardware_enabled", true):
		return true
	var lower := message.to_lower()
	return lower.contains("rfkill") or lower.contains("hardware")


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
	# Live tests run with --path demo; credentials live at the repo root (.env).
	return PackedStringArray([
		ProjectSettings.globalize_path("res://../.env"),
		ProjectSettings.globalize_path("res://.env"),
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


static func wait_until_has_ip(
	manager: WifiManager, timeout_sec: float = 20.0, poll_sec: float = 0.5
) -> Dictionary:
	if manager == null or not is_instance_valid(manager):
		return {}

	var elapsed := 0.0
	var info: Dictionary = {}
	while elapsed < timeout_sec:
		info = manager.get_connectivity_info()
		if not info.get("local_ip", "").is_empty():
			return info
		await Engine.get_main_loop().create_timer(poll_sec).timeout
		elapsed += poll_sec

	return manager.get_connectivity_info()


static func wait_until_disconnected(
	manager: WifiManager, timeout_sec: float = 15.0, poll_sec: float = 0.5
) -> bool:
	if manager == null or not is_instance_valid(manager):
		return false

	var elapsed := 0.0
	while elapsed < timeout_sec:
		var info: Dictionary = manager.get_connectivity_info()
		if not info.get("is_wifi_connected", true):
			return true
		await Engine.get_main_loop().create_timer(poll_sec).timeout
		elapsed += poll_sec

	return false


static func is_connected_to_ssid(manager: WifiManager, ssid: String) -> bool:
	if manager == null or not is_instance_valid(manager) or ssid.is_empty():
		return false
	var info: Dictionary = manager.get_connectivity_info()
	return info.get("is_wifi_connected", false) and info.get("connected_ssid", "") == ssid


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