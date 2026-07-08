extends GutTest

const WifiTestHelpers = preload("res://tests/helpers/wifi_test_helpers.gd")

# D-Bus set can take up to 30s (polkit); allow headroom for deferred emit on idle.
const RADIO_SET_TIMEOUT_SEC := 45.0

var _wifi: WifiManager
var _initial_enabled: bool = true
var _saved_initial_state: bool = false


func before_each() -> void:
	if WifiTestHelpers.skip_unless_real_wifi_platform(self):
		return

	OS.unset_environment(WifiTestHelpers.MOCK_ENV_VAR)
	_wifi = WifiManager.new()
	add_child_autofree(_wifi)
	await get_tree().process_frame

	if not _saved_initial_state:
		var state: Dictionary = _wifi.get_wifi_radio_state()
		_initial_enabled = state.get("enabled", _wifi.is_wifi_enabled())
		_saved_initial_state = true


func after_all() -> void:
	if not WifiTestHelpers.is_real_wifi_platform() or not _saved_initial_state:
		return

	var tree := Engine.get_main_loop() as SceneTree
	if tree == null:
		return

	var manager := WifiManager.new()
	tree.root.add_child(manager)
	await tree.process_frame

	if manager.is_wifi_enabled() != _initial_enabled:
		manager.set_wifi_enabled_async(_initial_enabled)
		await wait_seconds(RADIO_SET_TIMEOUT_SEC)

	manager.queue_free()


func test_real_toggle_wifi_radio_async() -> void:
	var state: Dictionary = _wifi.get_wifi_radio_state()
	WifiTestHelpers.assert_wifi_radio_shape(self, state)

	if WifiTestHelpers.is_radio_hardware_blocked(state):
		pending("Wi-Fi is blocked by hardware rfkill.")
		return

	if state.get("permission", "") == "no":
		pending("Permission denied for Wi-Fi radio control.")
		return

	if not state.get("can_toggle", false):
		pending("Wi-Fi radio cannot be toggled on this machine.")
		return

	if not state.get("enabled", false):
		pending("Wi-Fi radio is already off; skipping toggle test to avoid disrupting connectivity.")
		return

	var off_result := await _set_radio_and_wait(false)
	if off_result.error != OK:
		if WifiTestHelpers.is_radio_permission_denied(off_result.message):
			pending("Radio off requires elevated permissions or system approval: %s" % off_result.message)
		elif WifiTestHelpers.is_radio_hardware_blocked(state, off_result.message):
			pending("Wi-Fi radio is hardware-blocked: %s" % off_result.message)
		else:
			fail_test("Failed to turn Wi-Fi radio off: %s" % off_result.message)
		return

	assert_false(_wifi.is_wifi_enabled(), "Wi-Fi radio should be off after async set.")

	var on_result := await _set_radio_and_wait(true)
	if on_result.error != OK:
		if WifiTestHelpers.is_radio_permission_denied(on_result.message):
			pending("Radio on requires elevated permissions or system approval: %s" % on_result.message)
		else:
			fail_test("Failed to turn Wi-Fi radio back on: %s" % on_result.message)
		return

	assert_true(_wifi.is_wifi_enabled(), "Wi-Fi radio should be on after restore toggle.")


func _set_radio_and_wait(enabled: bool) -> Dictionary:
	watch_signals(_wifi)
	_wifi.set_wifi_enabled_async(enabled)
	# wait_seconds pumps idle frames where call_deferred(_emit_wifi_radio_set_completed) runs.
	await wait_seconds(RADIO_SET_TIMEOUT_SEC)

	assert_signal_emitted(_wifi, "wifi_radio_set_completed")
	var count: int = get_signal_emit_count(_wifi, "wifi_radio_set_completed")
	var params = get_signal_parameters(_wifi, "wifi_radio_set_completed", count - 1)
	var message: String = params[1] if params.size() > 1 else ""
	return { "error": params[0], "message": message }