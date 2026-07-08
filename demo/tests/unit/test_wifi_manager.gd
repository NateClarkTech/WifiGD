extends GutTest

const WifiTestHelpers = preload("res://tests/helpers/wifi_test_helpers.gd")

const STATE_DISCONNECTED := 0
const STATE_CONNECTING := 1
const STATE_CONNECTED := 2
const STATE_FAILED := 3

var _wifi: WifiManager


func before_each() -> void:
	OS.set_environment(WifiTestHelpers.MOCK_ENV_VAR, "1")
	_wifi = WifiManager.new()
	add_child_autofree(_wifi)
	await get_tree().process_frame


func after_each() -> void:
	OS.unset_environment(WifiTestHelpers.MOCK_ENV_VAR)


func test_connection_state_constants_are_bound() -> void:
	# GDExtension enum constants are bound in C++; verify via runtime connection state mapping.
	assert_eq(_wifi.get_connection_state(), STATE_CONNECTED)
	_wifi.disconnect_from_wifi("wlan0")
	await wait_seconds(0.1)
	assert_eq(_wifi.get_connection_state(), STATE_DISCONNECTED)


func test_get_last_error_returns_string() -> void:
	var message: String = _wifi.get_last_error()
	assert_typeof(message, TYPE_STRING)


func test_get_network_adapters_returns_mock_adapters() -> void:
	var adapters: Array = _wifi.get_network_adapters()
	assert_eq(adapters.size(), 2)

	var wifi_adapter: Dictionary = adapters[0]
	WifiTestHelpers.assert_adapter_shape(self, wifi_adapter)
	assert_eq(wifi_adapter.get("id"), "wlan0")
	assert_eq(wifi_adapter.get("type"), "wifi")

	var eth_adapter: Dictionary = adapters[1]
	assert_eq(eth_adapter.get("id"), "eth0")
	assert_eq(eth_adapter.get("type"), "ethernet")


func test_get_connectivity_info_returns_expected_shape() -> void:
	var info: Dictionary = _wifi.get_connectivity_info()
	WifiTestHelpers.assert_connectivity_shape(self, info)
	assert_true(info.get("is_wifi_connected", false))
	assert_eq(info.get("connected_ssid"), "TestNet")
	assert_eq(info.get("local_ip"), "192.168.1.42")
	assert_eq(info.get("state"), "connected")


func test_get_cached_connectivity_and_connection_state() -> void:
	_wifi.get_connectivity_info()
	await wait_seconds(0.1)

	var cached: Dictionary = _wifi.get_cached_connectivity()
	assert_gte(cached.size(), 0)
	assert_eq(_wifi.get_connection_state(), STATE_CONNECTED)


func test_is_wifi_enabled_mock() -> void:
	assert_true(_wifi.is_wifi_enabled())


func test_set_wifi_enabled_mock() -> void:
	assert_true(_wifi.set_wifi_enabled(false))
	assert_false(_wifi.is_wifi_enabled())
	assert_true(_wifi.set_wifi_enabled(true))
	assert_true(_wifi.is_wifi_enabled())


func test_get_wifi_radio_state_mock() -> void:
	var state: Dictionary = _wifi.get_wifi_radio_state()
	WifiTestHelpers.assert_wifi_radio_shape(self, state)
	assert_true(state.get("enabled", false))
	assert_true(state.get("software_enabled", false))
	assert_true(state.get("hardware_enabled", false))
	assert_true(state.get("can_toggle", false))
	assert_eq(state.get("permission"), "yes")


func test_set_wifi_enabled_async_emits_completed() -> void:
	watch_signals(_wifi)
	_wifi.set_wifi_enabled_async(false)
	await wait_seconds(0.5)

	assert_signal_emitted(_wifi, "wifi_radio_set_completed")
	var params = get_signal_parameters(_wifi, "wifi_radio_set_completed", 0)
	assert_not_null(params)
	assert_eq(params[0], OK)
	assert_false(_wifi.is_wifi_enabled())


func test_radio_busy_guard() -> void:
	watch_signals(_wifi)
	_wifi.set_wifi_enabled_async(false)
	_wifi.set_wifi_enabled_async(true)
	await wait_seconds(0.5)

	var busy_calls := 0
	for i in range(get_signal_emit_count(_wifi, "wifi_radio_set_completed")):
		var params = get_signal_parameters(_wifi, "wifi_radio_set_completed", i)
		if params[0] == ERR_BUSY:
			busy_calls += 1

	assert_eq(busy_calls, 1)


func test_scan_wifi_networks_sync() -> void:
	var networks: Array = _wifi.scan_wifi_networks("wlan0")
	assert_eq(networks.size(), 3)

	for network in networks:
		WifiTestHelpers.assert_wifi_network_shape(self, network)

	assert_eq(networks[0].get("ssid"), "TestNet")
	assert_eq(networks[2].get("ssid"), "OpenNet")
	assert_false(networks[2].get("is_secured", true))


func test_scan_wifi_networks_async_emits_completed() -> void:
	watch_signals(_wifi)
	_wifi.scan_wifi_networks_async("wlan0")
	await wait_seconds(1.0)

	assert_signal_emitted(_wifi, "scan_completed")
	var params = get_signal_parameters(_wifi, "scan_completed", 0)
	assert_not_null(params)
	assert_eq(params.size(), 3)

	var networks: Array = params[0]
	var error: int = params[1]
	assert_eq(error, OK)
	assert_eq(networks.size(), 3)


func test_scan_busy_guard_rejects_duplicate_request() -> void:
	watch_signals(_wifi)
	_wifi.scan_wifi_networks_async("wlan0")
	_wifi.scan_wifi_networks_async("wlan0")
	await wait_seconds(1.0)

	var busy_calls := 0
	for i in range(get_signal_emit_count(_wifi, "scan_completed")):
		var params = get_signal_parameters(_wifi, "scan_completed", i)
		if params[1] == ERR_BUSY:
			busy_calls += 1

	assert_eq(busy_calls, 1)


func test_connect_to_wifi_sync_success() -> void:
	var err: Error = _wifi.connect_to_wifi("TestNet", "test-password", "wlan0")
	assert_eq(err, OK)

	var info: Dictionary = _wifi.get_connectivity_info()
	assert_true(info.get("is_wifi_connected", false))


func test_connect_to_wifi_sync_rejects_bad_credentials() -> void:
	var err: Error = _wifi.connect_to_wifi("BadNet", "wrong", "wlan0")
	assert_eq(err, ERR_CANT_CONNECT)


func test_connect_to_wifi_async_emits_completed() -> void:
	watch_signals(_wifi)
	_wifi.connect_to_wifi_async("TestNet", "test-password", "wlan0")
	await wait_seconds(0.5)

	assert_signal_emitted(_wifi, "connect_completed")
	var params = get_signal_parameters(_wifi, "connect_completed", 0)
	assert_eq(params[0], OK)


func test_connect_busy_guard_rejects_duplicate_request() -> void:
	watch_signals(_wifi)
	_wifi.connect_to_wifi_async("TestNet", "test-password", "wlan0")
	_wifi.connect_to_wifi_async("TestNet", "test-password", "wlan0")
	await wait_seconds(0.5)

	var busy_calls := 0
	for i in range(get_signal_emit_count(_wifi, "connect_completed")):
		var params = get_signal_parameters(_wifi, "connect_completed", i)
		if params[0] == ERR_BUSY:
			busy_calls += 1

	assert_eq(busy_calls, 1)


func test_disconnect_from_wifi_sync() -> void:
	assert_eq(_wifi.disconnect_from_wifi("wlan0"), OK)
	var info: Dictionary = _wifi.get_connectivity_info()
	assert_false(info.get("is_wifi_connected", true))


func test_disconnect_from_wifi_async_emits_completed() -> void:
	watch_signals(_wifi)
	_wifi.disconnect_from_wifi_async("wlan0")
	await wait_seconds(0.5)

	assert_signal_emitted(_wifi, "disconnect_completed")
	var params = get_signal_parameters(_wifi, "disconnect_completed", 0)
	assert_eq(params[0], OK)


func test_fetch_adapters_async_emits_updated() -> void:
	watch_signals(_wifi)
	_wifi.fetch_adapters_async()
	await wait_seconds(0.5)

	assert_signal_emitted(_wifi, "adapters_updated")
	var params = get_signal_parameters(_wifi, "adapters_updated", 0)
	assert_eq(params[0].size(), 2)
	assert_eq(params[1], OK)


func test_get_cached_adapters_updates_after_async_fetch() -> void:
	_wifi.fetch_adapters_async()
	await wait_seconds(0.5)

	var cached: Array = _wifi.get_cached_adapters()
	assert_eq(cached.size(), 2)


func test_connectivity_changed_signal_emits_on_poll() -> void:
	watch_signals(_wifi)
	_wifi.disconnect_from_wifi("wlan0")
	# add_child_autofree already invoked _ready(), which enables _process polling.
	await wait_seconds(2.5)
	assert_signal_emitted(_wifi, "connectivity_changed")