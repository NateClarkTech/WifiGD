extends Control

@onready var _wifi: WifiManager = $WifiManager
@onready var _status_label: Label = %StatusLabel
@onready var _scan_button: Button = %ScanButton
@onready var _connect_button: Button = %ConnectButton
@onready var _disconnect_button: Button = %DisconnectButton
@onready var _network_list: ItemList = %NetworkList
@onready var _ssid_field: LineEdit = %SsidField
@onready var _password_field: LineEdit = %PasswordField
@onready var _log_output: TextEdit = %LogOutput

var _networks: Array = []
var _busy: bool = false


func _ready() -> void:
	_wifi.scan_completed.connect(_on_scan_completed)
	_wifi.connect_completed.connect(_on_connect_completed)
	_wifi.disconnect_completed.connect(_on_disconnect_completed)
	_wifi.connectivity_changed.connect(_on_connectivity_changed)

	_scan_button.pressed.connect(_on_scan_pressed)
	_connect_button.pressed.connect(_on_connect_pressed)
	_disconnect_button.pressed.connect(_on_disconnect_pressed)
	_network_list.item_selected.connect(_on_network_selected)

	_log("WifiGD example ready.")
	_wifi.fetch_adapters_async()
	_update_status_from_cache()


func _set_busy(is_busy: bool) -> void:
	_busy = is_busy
	_scan_button.disabled = is_busy
	_connect_button.disabled = is_busy
	_disconnect_button.disabled = is_busy


func _wifi_adapter_id() -> String:
	for adapter: Dictionary in _wifi.get_cached_adapters():
		if adapter.get("type", "") == "wifi":
			return adapter.get("id", "")
	return ""


func _on_scan_pressed() -> void:
	var adapter_id := _wifi_adapter_id()
	if adapter_id.is_empty():
		_log("No Wi-Fi adapter found. Refresh adapters and try again.")
		return
	_set_busy(true)
	_log("Scanning...")
	_wifi.scan_wifi_networks_async(adapter_id)


func _on_scan_completed(networks: Array, error: int, message: String) -> void:
	_set_busy(false)
	_networks = networks
	_refresh_network_list()
	_log("Scan finished: %d network(s)." % networks.size())
	if error != OK and not message.is_empty():
		_log("Note: %s" % message)


func _on_connect_pressed() -> void:
	var ssid := _ssid_field.text.strip_edges()
	if ssid.is_empty():
		_log("Enter or select an SSID first.")
		return
	_set_busy(true)
	_log("Connecting to %s..." % ssid)
	_wifi.connect_to_wifi_async(ssid, _password_field.text, _wifi_adapter_id())


func _on_connect_completed(error: int, message: String) -> void:
	_set_busy(false)
	if error == OK:
		_log("Connect request succeeded.")
	else:
		_log("Connect failed: %s" % message)


func _on_disconnect_pressed() -> void:
	_set_busy(true)
	_log("Disconnecting...")
	_wifi.disconnect_from_wifi_async(_wifi_adapter_id())


func _on_disconnect_completed(error: int, message: String) -> void:
	_set_busy(false)
	if error == OK:
		_log("Disconnected.")
	else:
		_log("Disconnect failed: %s" % message)


func _on_connectivity_changed(info: Dictionary) -> void:
	_update_status(info)


func _on_network_selected(index: int) -> void:
	if index < 0 or index >= _networks.size():
		return
	var network: Dictionary = _networks[index]
	_ssid_field.text = network.get("ssid", "")
	_log("Selected: %s (%d%%)" % [network.get("ssid", ""), network.get("signal_strength", 0)])


func _refresh_network_list() -> void:
	_network_list.clear()
	for network: Dictionary in _networks:
		var ssid: String = network.get("ssid", "")
		var marker := " *" if network.get("is_connected", false) else ""
		_network_list.add_item(
			"%s%s  %d%%  %s" % [
				ssid,
				marker,
				network.get("signal_strength", 0),
				network.get("security_type", ""),
			]
		)
	if _network_list.item_count > 0:
		_network_list.select(0)
		_on_network_selected(0)


func _update_status_from_cache() -> void:
	_update_status(_wifi.get_cached_connectivity())


func _update_status(info: Dictionary) -> void:
	var ssid: String = info.get("connected_ssid", "")
	var state: String = info.get("state", "disconnected")
	if info.get("is_wifi_connected", false) and not ssid.is_empty():
		_status_label.text = "Connected to \"%s\" (%s)" % [ssid, info.get("local_ip", "")]
	else:
		_status_label.text = "Not connected (state: %s)" % state


func _log(message: String) -> void:
	print(message)
	_log_output.text += message + "\n"
	var line_count := _log_output.get_line_count()
	if line_count > 0:
		_log_output.set_caret_line(line_count - 1)