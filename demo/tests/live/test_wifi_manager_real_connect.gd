extends GutTest

const WifiTestHelpers = preload("res://tests/helpers/wifi_test_helpers.gd")

const CONNECT_TIMEOUT_SEC := 60.0

var _wifi: WifiManager
var _env: Dictionary = {}
var _ssid: String = ""
var _password: String = ""
var _adapter_id: String = ""


func before_all() -> void:
	if OS.get_name() != "Linux":
		return
	_env = WifiTestHelpers.load_wifi_env()
	_ssid = _env.get("WIFI_SSID", "")
	_password = _env.get("WIFI_PASSWORD", "")
	_adapter_id = _env.get("WIFI_ADAPTER_ID", "")


func before_each() -> void:
	if OS.get_name() != "Linux":
		pending("Real Wi-Fi connect tests only run on Linux.")
		return

	if _ssid.is_empty():
		pending(
			"Missing WIFI_SSID. Copy demo/.env.example to demo/.env and set your network credentials."
		)
		return

	OS.unset_environment(WifiTestHelpers.MOCK_ENV_VAR)
	_wifi = WifiManager.new()
	add_child_autofree(_wifi)
	await get_tree().process_frame


func after_each() -> void:
	await WifiTestHelpers.disconnect_and_wait(_wifi, _adapter_id, 2.0)


func test_env_credentials_are_loaded() -> void:
	assert_false(_ssid.is_empty(), "WIFI_SSID must be set in demo/.env")
	assert_typeof(_password, TYPE_STRING)
	assert_typeof(_adapter_id, TYPE_STRING)


func test_real_scan_includes_env_network() -> void:
	if not _wifi.is_wifi_enabled():
		pending("Wi-Fi radio is disabled on this machine.")
		return

	watch_signals(_wifi)
	_wifi.scan_wifi_networks_async(_adapter_id)
	await wait_seconds(20.0)

	assert_signal_emitted(_wifi, "scan_completed")
	var params = get_signal_parameters(_wifi, "scan_completed", 0)
	var networks: Array = params[0]
	var error: int = params[1]

	if error != OK and networks.is_empty():
		pending("Scan failed: %s" % params[2])
		return

	var found := false
	for network in networks:
		if network.get("ssid", "") == _ssid:
			found = true
			break

	assert_true(found, 'Scanned networks should include WIFI_SSID "%s" from demo/.env' % _ssid)


func test_real_connect_to_env_network_async() -> void:
	if not _wifi.is_wifi_enabled():
		pending("Wi-Fi radio is disabled on this machine.")
		return

	watch_signals(_wifi)
	_wifi.connect_to_wifi_async(_ssid, _password, _adapter_id)
	await wait_seconds(CONNECT_TIMEOUT_SEC)

	assert_signal_emitted(_wifi, "connect_completed")
	var params = get_signal_parameters(_wifi, "connect_completed", 0)
	var error: int = params[0]
	var message: String = params[1]

	if error != OK:
		if message.contains("Permission denied") or message.contains("polkit"):
			pending("Connect requires polkit approval: %s" % message)
		else:
			fail_test("Connect to '%s' failed: %s" % [_ssid, message])
		return

	assert_eq(error, OK)


func test_real_verify_connected_to_env_network() -> void:
	watch_signals(_wifi)
	_wifi.connect_to_wifi_async(_ssid, _password, _adapter_id)
	await wait_seconds(CONNECT_TIMEOUT_SEC)

	var params = get_signal_parameters(_wifi, "connect_completed", 0)
	if params == null or params[0] != OK:
		pending("Skipping connectivity check because connect did not succeed.")
		return

	await wait_seconds(3.0)
	var info: Dictionary = _wifi.get_connectivity_info()
	WifiTestHelpers.assert_connectivity_shape(self, info)
	assert_true(info.get("is_wifi_connected", false))
	assert_eq(info.get("connected_ssid", ""), _ssid)
	assert_false(info.get("local_ip", "").is_empty(), "Expected an IP address after connect.")


func test_real_disconnect_from_env_network() -> void:
	# Ensure we are connected first.
	watch_signals(_wifi)
	_wifi.connect_to_wifi_async(_ssid, _password, _adapter_id)
	await wait_seconds(CONNECT_TIMEOUT_SEC)

	var connect_params = get_signal_parameters(_wifi, "connect_completed", 0)
	if connect_params == null or connect_params[0] != OK:
		pending("Skipping disconnect test because connect did not succeed.")
		return

	watch_signals(_wifi)
	_wifi.disconnect_from_wifi_async(_adapter_id)
	await wait_seconds(10.0)

	assert_signal_emitted(_wifi, "disconnect_completed")
	var params = get_signal_parameters(_wifi, "disconnect_completed", 0)
	assert_eq(params[0], OK)

	await wait_seconds(2.0)
	var info: Dictionary = _wifi.get_connectivity_info()
	assert_false(info.get("is_wifi_connected", true))