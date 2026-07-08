extends Control

@onready var _wifi_radio: CheckButton = %WifiRadioToggle
@onready var _status_label: Label = %StatusSummary
@onready var _adapters_list: ItemList = %AdaptersList
@onready var _networks_list: ItemList = %NetworksList
@onready var _network_details: RichTextLabel = %NetworkDetails
@onready var _ssid_field: LineEdit = %SsidField
@onready var _password_field: LineEdit = %PasswordField
@onready var _connectivity_details: RichTextLabel = %ConnectivityDetails
@onready var _log: TextEdit = %ActivityLog
@onready var _busy_label: Label = %BusyLabel
@onready var _scan_button: Button = %ScanButton
@onready var _connect_button: Button = %ConnectButton
@onready var _disconnect_button: Button = %DisconnectButton
@onready var _refresh_adapters_button: Button = %RefreshAdaptersButton
@onready var _refresh_status_button: Button = %RefreshStatusButton

var _busy: bool = false
var _uses_async_api: bool = false

var _adapters: Array = []
var _networks: Array = []
var _selected_adapter_index: int = -1
var _selected_network_index: int = -1


func _ready() -> void:
	_uses_async_api = WiFi.has_signal("scan_completed")

	if _uses_async_api:
		WiFi.scan_completed.connect(_on_scan_completed)
		WiFi.connect_completed.connect(_on_connect_completed)
		WiFi.disconnect_completed.connect(_on_disconnect_completed)
		WiFi.adapters_updated.connect(_on_adapters_updated)
		WiFi.connectivity_changed.connect(_on_connectivity_changed)
		if WiFi.has_signal("wifi_radio_set_completed"):
			WiFi.wifi_radio_set_completed.connect(_on_wifi_radio_set_completed)
	else:
		_log_message("ERROR: Outdated WifiGD library loaded in demo/addons/WifiGD/bin.")
		_log_message("Close Godot, rebuild with: scons platform=linux")
		_log_message("Falling back to synchronous API (UI will freeze during scan).")

	_log_message("WifiGD demo ready.")
	_log_message("Select a Wi-Fi adapter, scan for networks, then connect with a password.")
	_log_message("Tip: click the log to select text, Ctrl+C to copy, or use the Copy button.")
	_set_busy(false)
	_refresh_adapters()
	_refresh_connectivity()


func _set_busy(is_busy: bool) -> void:
	_busy = is_busy
	_busy_label.visible = is_busy
	_scan_button.disabled = is_busy
	_connect_button.disabled = is_busy
	_disconnect_button.disabled = is_busy
	_refresh_adapters_button.disabled = is_busy
	_refresh_status_button.disabled = is_busy
	_wifi_radio.disabled = is_busy


func _on_connectivity_changed(info: Dictionary) -> void:
	_update_status_summary(info)
	_update_connectivity_details(info)


func _log_message(message: String) -> void:
	_log.text += message + "\n"
	var line_count := _log.get_line_count()
	if line_count > 0:
		_log.set_caret_line(line_count - 1)
	_log.set_caret_column(_log.get_line(line_count - 1).length())


func _wifi_adapter_id_for_operations() -> String:
	if _selected_adapter_index >= 0 and _selected_adapter_index < _adapters.size():
		var selected: Dictionary = _adapters[_selected_adapter_index]
		if selected.get("type", "") == "wifi":
			return selected.get("id", "")

	for adapter in _adapters:
		if adapter.get("type", "") == "wifi":
			return adapter.get("id", "")
	return ""


func _populate_adapters_list() -> void:
	_adapters_list.clear()
	var first_wifi_index := -1
	for i in range(_adapters.size()):
		var adapter: Dictionary = _adapters[i]
		var adapter_type: String = adapter.get("type", "unknown")
		var label := "%s  [%s]" % [adapter.get("name", "Unknown"), adapter_type]
		if not adapter.get("ip_address", "").is_empty():
			label += "  %s" % adapter.get("ip_address", "")
		_adapters_list.add_item(label)
		if adapter_type == "wifi" and first_wifi_index < 0:
			first_wifi_index = i

	if _adapters_list.item_count == 0:
		_selected_adapter_index = -1
		return

	if _selected_adapter_index < 0 or _selected_adapter_index >= _adapters.size():
		_selected_adapter_index = first_wifi_index if first_wifi_index >= 0 else 0

	_adapters_list.select(_selected_adapter_index)
	_on_adapter_selected(_selected_adapter_index)


func _populate_networks_list() -> void:
	_networks_list.clear()
	_selected_network_index = -1
	_network_details.text = ""

	for network in _networks:
		var ssid: String = network.get("ssid", "")
		var strength: int = network.get("signal_strength", 0)
		var security: String = network.get("security_type", "")
		var connected_marker := " *" if network.get("is_connected", false) else ""
		_networks_list.add_item("%s%s  %d%%  %s" % [ssid, connected_marker, strength, security])


func _update_status_summary(connectivity: Dictionary) -> void:
	var state: String = connectivity.get("state", "unknown")
	var ssid: String = connectivity.get("connected_ssid", "")
	var ip: String = connectivity.get("local_ip", "n/a")
	var wifi_connected: bool = connectivity.get("is_wifi_connected", false)

	if wifi_connected and not ssid.is_empty():
		_status_label.text = "Connected to \"%s\"  |  %s  |  state: %s" % [ssid, ip, state]
	else:
		_status_label.text = "Not connected  |  %s  |  state: %s" % [ip, state]


func _update_connectivity_details(connectivity: Dictionary) -> void:
	var lines: PackedStringArray = [
		"[b]Connection[/b]",
		"State: %s" % connectivity.get("state", "unknown"),
		"Wi-Fi connected: %s" % str(connectivity.get("is_wifi_connected", false)),
		"SSID: %s" % connectivity.get("connected_ssid", ""),
		"",
		"[b]Addresses[/b]",
		"Local IP: %s" % connectivity.get("local_ip", ""),
		"Gateway: %s" % connectivity.get("gateway", ""),
		"DNS: %s" % connectivity.get("dns_primary", ""),
		"Has internet: %s" % str(connectivity.get("has_internet", false)),
	]
	_connectivity_details.text = "\n".join(lines)


func _update_network_details(network: Dictionary) -> void:
	var lines: PackedStringArray = [
		"[b]%s[/b]" % network.get("ssid", ""),
		"Signal: %d%%" % network.get("signal_strength", 0),
		"Security: %s" % network.get("security_type", ""),
		"Secured: %s" % str(network.get("is_secured", false)),
		"Connected: %s" % str(network.get("is_connected", false)),
		"BSSID: %s" % network.get("bssid", ""),
		"Band: %s" % network.get("band", ""),
		"Channel: %s" % str(network.get("channel", 0)),
		"Frequency: %s MHz" % str(network.get("frequency_mhz", 0)),
		"Adapter ID: %s" % network.get("adapter_id", ""),
	]
	_network_details.text = "\n".join(lines)


func _apply_scan_result(networks: Array, error: int, message: String) -> void:
	_set_busy(false)

	if networks.is_empty():
		if message.is_empty():
			_log_message(
				"WARN: Scan finished with 0 networks. "
				+ "Try selecting a Wi-Fi adapter or wait a moment and scan again."
			)
		else:
			_log_message("ERROR: Scan failed: %s" % message)
		_networks = []
		_populate_networks_list()
		return

	_networks = networks
	_populate_networks_list()
	_log_message("Scan complete. Found %d network(s)." % networks.size())
	if error != OK and not message.is_empty():
		_log_message("NOTE: %s" % message)
	elif error == OK and not message.is_empty():
		_log_message("NOTE: %s" % message)
	_networks_list.select(0)
	_on_network_selected(0)


func _on_scan_completed(networks: Array, error: int, message: String) -> void:
	_apply_scan_result(networks, error, message)


func _on_adapters_updated(adapters: Array, error: int, message: String) -> void:
	_set_busy(false)
	if adapters.is_empty() and error != OK:
		_log_message("ERROR: Adapter query failed: %s" % message)
		return

	_adapters = adapters
	_populate_adapters_list()
	_log_message("Found %d adapter(s)." % adapters.size())
	if error != OK and not message.is_empty():
		_log_message("NOTE: %s" % message)


func _on_connect_completed(error: int, message: String) -> void:
	_set_busy(false)
	if error != OK:
		_log_message("ERROR: Connect failed: %s" % message)
	else:
		_log_message("OK: Connect request sent successfully.")
	_refresh_connectivity()


func _on_disconnect_completed(error: int, message: String) -> void:
	_set_busy(false)
	if error != OK:
		_log_message("ERROR: Disconnect failed: %s" % message)
	else:
		_log_message("OK: Disconnected from Wi-Fi.")
	_refresh_connectivity()


func _on_wifi_radio_set_completed(error: int, message: String) -> void:
	_set_busy(false)
	if error != OK:
		if message.is_empty():
			message = WiFi.get_last_error()
		_log_message("ERROR: Wi-Fi radio change failed: %s" % message)
	else:
		_log_message(
			"OK: Wi-Fi radio is now %s." % ("enabled" if WiFi.is_wifi_enabled() else "disabled")
		)
	_wifi_radio.set_pressed_no_signal(WiFi.is_wifi_enabled())


func _refresh_adapters() -> void:
	if _busy:
		return
	_set_busy(true)
	_log_message("Refreshing adapters...")
	if _uses_async_api:
		WiFi.fetch_adapters_async()
	else:
		_adapters = WiFi.get_network_adapters()
		var message: String = WiFi.get_last_error()
		_on_adapters_updated(_adapters, OK if not _adapters.is_empty() or message.is_empty() else ERR_CANT_CONNECT, message)


func _refresh_connectivity() -> void:
	var connectivity: Dictionary = WiFi.get_connectivity_info()
	_update_status_summary(connectivity)
	_update_connectivity_details(connectivity)
	_wifi_radio.set_pressed_no_signal(WiFi.is_wifi_enabled())


func _on_adapter_selected(index: int) -> void:
	_selected_adapter_index = index
	if index < 0 or index >= _adapters.size():
		return
	var adapter: Dictionary = _adapters[index]
	_log_message(
		"Selected adapter: %s (%s)" % [adapter.get("name", ""), adapter.get("type", "")]
	)
	if adapter.get("type", "") != "wifi":
		_log_message(
			"WARN: This is not a Wi-Fi adapter. "
			+ "Scan/connect will use the first available Wi-Fi adapter instead."
		)


func _on_network_selected(index: int) -> void:
	_selected_network_index = index
	if index < 0 or index >= _networks.size():
		_network_details.text = ""
		return
	var network: Dictionary = _networks[index]
	_ssid_field.text = network.get("ssid", "")
	_update_network_details(network)


func _on_scan_pressed() -> void:
	if _busy:
		return
	var adapter_id := _wifi_adapter_id_for_operations()
	if adapter_id.is_empty():
		_log_message("ERROR: No Wi-Fi adapter found. Refresh adapters and try again.")
		return

	_set_busy(true)
	_log_message("Scanning for Wi-Fi networks...")
	if _uses_async_api:
		WiFi.scan_wifi_networks_async(adapter_id)
	else:
		await get_tree().process_frame
		var networks: Array = WiFi.scan_wifi_networks(adapter_id)
		_apply_scan_result(networks, OK if not networks.is_empty() else ERR_CANT_CONNECT, WiFi.get_last_error())


func _on_connect_pressed() -> void:
	if _busy:
		return
	var ssid := _ssid_field.text.strip_edges()
	if ssid.is_empty():
		_log_message("WARN: Enter or select an SSID first.")
		return

	_set_busy(true)
	_log_message("Connecting to %s..." % ssid)
	if _uses_async_api:
		WiFi.connect_to_wifi_async(
			ssid,
			_password_field.text,
			_wifi_adapter_id_for_operations()
		)
	else:
		await get_tree().process_frame
		var err: Error = WiFi.connect_to_wifi(
			ssid,
			_password_field.text,
			_wifi_adapter_id_for_operations()
		)
		_on_connect_completed(err, WiFi.get_last_error())


func _on_disconnect_pressed() -> void:
	if _busy:
		return
	_set_busy(true)
	_log_message("Disconnecting...")
	if _uses_async_api:
		WiFi.disconnect_from_wifi_async(_wifi_adapter_id_for_operations())
	else:
		await get_tree().process_frame
		var err: Error = WiFi.disconnect_from_wifi(_wifi_adapter_id_for_operations())
		_on_disconnect_completed(err, WiFi.get_last_error())


func _on_refresh_status_pressed() -> void:
	_refresh_connectivity()


func _on_wifi_radio_toggled(enabled: bool) -> void:
	if _busy:
		return
	_set_busy(true)
	_log_message("Setting Wi-Fi radio to %s..." % ("on" if enabled else "off"))

	if _uses_async_api and WiFi.has_method("set_wifi_enabled_async"):
		WiFi.set_wifi_enabled_async(enabled)
		if WiFi.has_signal("wifi_radio_set_completed"):
			await WiFi.wifi_radio_set_completed
		else:
			await get_tree().process_frame
			if not WiFi.set_wifi_enabled(enabled):
				_log_message("ERROR: Wi-Fi radio change failed: %s" % WiFi.get_last_error())
			else:
				_log_message(
					"OK: Wi-Fi radio is now %s." % ("enabled" if WiFi.is_wifi_enabled() else "disabled")
				)
			_wifi_radio.set_pressed_no_signal(WiFi.is_wifi_enabled())
			_set_busy(false)
	else:
		await get_tree().process_frame
		if not WiFi.set_wifi_enabled(enabled):
			_log_message("ERROR: Wi-Fi radio change failed: %s" % WiFi.get_last_error())
		else:
			_log_message(
				"OK: Wi-Fi radio is now %s." % ("enabled" if WiFi.is_wifi_enabled() else "disabled")
			)
		_wifi_radio.set_pressed_no_signal(WiFi.is_wifi_enabled())
		_set_busy(false)


func _on_copy_log_pressed() -> void:
	if _log.text.is_empty():
		return
	DisplayServer.clipboard_set(_log.text)
	_log_message("OK: Log copied to clipboard.")


func _on_clear_log_pressed() -> void:
	_log.text = ""
