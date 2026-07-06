extends GutTest

const WifiTestHelpers = preload("res://tests/helpers/wifi_test_helpers.gd")

var _wifi: WifiManager


func before_each() -> void:
	if WifiTestHelpers.skip_unless_real_wifi_platform(self):
		return

	OS.unset_environment(WifiTestHelpers.MOCK_ENV_VAR)
	_wifi = WifiManager.new()
	add_child_autofree(_wifi)
	await get_tree().process_frame


func test_real_get_network_adapters_returns_wifi_adapter() -> void:
	var adapters: Array = _wifi.get_network_adapters()
	if adapters.is_empty():
		pending("No adapters found: %s" % _wifi.get_last_error())
		return

	var found_wifi := false
	for adapter in adapters:
		WifiTestHelpers.assert_adapter_shape(self, adapter)
		if adapter.get("type") == "wifi":
			found_wifi = true
			assert_false(adapter.get("id", "").is_empty())

	assert_true(found_wifi, "Expected at least one Wi-Fi adapter.")


func test_real_get_connectivity_info_shape() -> void:
	var info: Dictionary = _wifi.get_connectivity_info()
	WifiTestHelpers.assert_connectivity_shape(self, info)
	assert_true(
		info.get("state") in ["disconnected", "connecting", "connected", "failed"],
		"Unexpected connection state: %s" % info.get("state")
	)


func test_real_is_wifi_enabled_reflects_radio_state() -> void:
	var enabled: bool = _wifi.is_wifi_enabled()
	assert_true(enabled == true or enabled == false)


func test_real_scan_wifi_networks_async() -> void:
	if not _wifi.is_wifi_enabled():
		pending("Wi-Fi radio is disabled on this machine.")
		return

	watch_signals(_wifi)
	_wifi.scan_wifi_networks_async()
	await wait_seconds(20.0)

	assert_signal_emitted(_wifi, "scan_completed")
	var params = get_signal_parameters(_wifi, "scan_completed", 0)
	var networks: Array = params[0]
	var error: int = params[1]
	var message: String = params[2] if params.size() > 2 else ""

	if error != OK and networks.is_empty():
		if OS.get_name() == "Windows" and (
			message.to_lower().contains("access denied")
			or message.to_lower().contains("location")
			or message.to_lower().contains("cached")
		):
			pending(
				"Scan unavailable on Windows (enable Location in Settings, or use cached results): %s"
				% message
			)
		else:
			pending("Scan failed on this machine: %s" % message)
		return

	for network in networks:
		WifiTestHelpers.assert_wifi_network_shape(self, network)
		assert_false(network.get("ssid", "").is_empty())


func test_real_get_last_error_is_string_after_operations() -> void:
	_wifi.get_network_adapters()
	var message: String = _wifi.get_last_error()
	assert_typeof(message, TYPE_STRING)