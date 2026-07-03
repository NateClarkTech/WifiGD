# WifiGD

Cross-platform Wi-Fi management for Godot 4 via GDExtension. Enumerate adapters, scan networks, connect, disconnect, and read connectivity — from GDScript without blocking the main thread.

**Godot:** 4.3+  
**Status:** Early development. Planned features may be broken, unoptimized, and not work.

---

## API

`WifiManager` extends `Node`. Register it as an autoload singleton (recommended) or add the provided scene to your tree.

```gdscript
# project.godot autoload (name must differ from the WifiManager class):
# WiFi="*res://addons/WifiGD/wifi_manager_autoload.tscn"

func _ready() -> void:
    WiFi.scan_completed.connect(_on_scan_done)
```

### Methods

| Function | Inputs | Output | Description |
|----------|--------|--------|-------------|
| `is_wifi_enabled()` | — | `bool` | Returns whether the Wi-Fi radio appears enabled. |
| `set_wifi_enabled` | `enabled: bool` | `bool` | Enables or disables the Wi-Fi radio. May require elevated privileges. |
| `scan_wifi_networks` | `adapter_id: String = ""` | `Array` | **Sync.** Scans for nearby networks. Blocks until complete — avoid on the main thread during gameplay. |
| `scan_wifi_networks_async` | `adapter_id: String = ""` | `void` | **Async.** Starts a scan; results arrive via `scan_completed`. |
| `connect_to_wifi` | `ssid: String`, `password: String = ""`, `adapter_id: String = ""` | `Error` | **Sync.** Connects to a network (open or WPA2-PSK). Blocks until the request finishes. |
| `connect_to_wifi_async` | `ssid: String`, `password: String = ""`, `adapter_id: String = ""` | `void` | **Async.** Starts a connect; result arrives via `connect_completed`. |
| `disconnect_from_wifi` | `adapter_id: String = ""` | `Error` | **Sync.** Disconnects from the current Wi-Fi network. |
| `disconnect_from_wifi_async` | `adapter_id: String = ""` | `void` | **Async.** Starts a disconnect; result arrives via `disconnect_completed`. |
| `get_network_adapters` | — | `Array` | **Sync.** Returns the current list of network adapters. |
| `fetch_adapters_async` | — | `void` | **Async.** Refreshes adapters; result arrives via `adapters_updated`. |
| `get_connectivity_info` | — | `Dictionary` | **Sync.** Returns connection state, SSID, IP, gateway, and DNS. |
| `get_cached_connectivity` | — | `Dictionary` | **Sync.** Returns the last cached connectivity snapshot (updated by polling). |
| `get_connection_state` | — | `int` | **Sync.** Returns cached `DISCONNECTED` / `CONNECTING` / `CONNECTED` / `FAILED`. |
| `get_cached_adapters` | — | `Array` | **Sync.** Returns the last cached adapter list. |
| `get_last_error` | — | `String` | Returns a short, user-facing message for the last failure. Detailed OS errors are logged to the Godot **Output** panel separately. |

Empty `adapter_id` uses the default / first Wi-Fi adapter on the platform.

### Signals

| Signal | Payload | Description |
|--------|---------|-------------|
| `scan_completed` | `networks: Array`, `error: int`, `message: String` | Emitted when an async scan finishes. `networks` is an array of `WifiNetwork` dictionaries. |
| `connect_completed` | `error: int`, `message: String` | Emitted when an async connect finishes. |
| `disconnect_completed` | `error: int`, `message: String` | Emitted when an async disconnect finishes. |
| `adapters_updated` | `adapters: Array`, `error: int`, `message: String` | Emitted when an async adapter refresh finishes. `adapters` is an array of `NetworkAdapter` dictionaries. |
| `connectivity_changed` | `info: Dictionary` | Emitted when connectivity state changes (polled ~every 2s on the autoload). |
| `connection_state_changed` | `state: int`, `ssid: String` | Emitted when connection state or active SSID changes. |
| `wifi_enabled_changed` | `enabled: bool` | Emitted when the Wi-Fi radio enabled state changes. |

`error` is a Godot `Error` code (`OK`, `ERR_CANT_CONNECT`, `ERR_BUSY`, etc.). `message` is friendly text for UI logs.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DISCONNECTED` | `0` | Not connected to Wi-Fi |
| `CONNECTING` | `1` | Association or DHCP in progress |
| `CONNECTED` | `2` | Connected and associated |
| `FAILED` | `3` | Connection attempt failed |

---

## Function support

Platform backend status for each method. **Working** means the OS action reliably does what the user expects; **Partial** means code exists but results are incomplete or unreliable; **Not implemented** means the backend is a stub.

| Function | Windows | Linux | macOS |
|----------|---------|-------|-------|
| `is_wifi_enabled` | Partial | **Working** | Not implemented |
| `set_wifi_enabled` | Partial | Partial | Not implemented |
| `scan_wifi_networks` | Partial | **Working** | Not implemented |
| `scan_wifi_networks_async` | Partial | **Working** | Not implemented |
| `connect_to_wifi` | Partial | **Working** | Not implemented |
| `connect_to_wifi_async` | Partial | **Working** | Not implemented |
| `disconnect_from_wifi` | **Working** | **Working** | Not implemented |
| `disconnect_from_wifi_async` | **Working** | **Working** | Not implemented |
| `get_network_adapters` | Partial | **Working** | Not implemented |
| `fetch_adapters_async` | Partial | **Working** | Not implemented |
| `get_connectivity_info` | Partial | **Working** | Not implemented |
| `get_last_error` | Working | Working | Working |

**Windows notes:** Scan returns cached / BSS results when active scan is denied. Connect sends a profile + connect request but does not reliably join a network in testing. Only disconnect is confirmed end-to-end.

**Linux notes:** Requires NetworkManager (libnm). Connect/disconnect and radio toggle may prompt polkit authorization. Open and WPA2-PSK networks supported. IWD-only or ConnMan setups are not supported.

**macOS notes:** Backend in `src/platform/macos/` is a stub.

---

## Quick start

### Clone and build

```bash
git clone <your-repo-url> WifiGD
cd WifiGD
git submodule update --init --recursive

# Windows (PowerShell)
scons platform=windows

# Linux
scons platform=linux

# Release build
scons platform=linux target=template_release
```

Binaries are written to `addons/WifiGD/bin/` and mirrored to `demo/addons/WifiGD/bin/`.

Native libraries are **gitignored** — you must build locally (or CI) before running the demo.

### Run the demo

```bash
# From repo root — opens the demo project (not the editor by default on some setups)
godot --path demo/
```

Or open `demo/project.godot` in the Godot editor.

If you rebuild while Godot is open you may need to reload the project or close and reopen Godot to see changes.

---

## Installation in your project

1. Copy `addons/WifiGD/` into your Godot project.
2. Build the native library for your platform into `addons/WifiGD/bin/` (see [Building](#building)).
3. Use `WifiManager` from GDScript (see [API](#api)).

No editor plugin is required — registration is via `WifiGD.gdextension`.

---

## Data types

#### Scan result (`WifiNetwork`)

| Key | Type | Description |
|-----|------|-------------|
| `ssid` | `String` | Network name |
| `bssid` | `String` | AP MAC address |
| `signal_strength` | `int` | Normalized 0–100 |
| `is_secured` | `bool` | Password required |
| `is_connected` | `bool` | Currently associated |
| `security_type` | `String` | e.g. `open`, `wpa2`, `connected` |
| `adapter_id` | `String` | Adapter that saw this network |

#### Adapter (`NetworkAdapter`)

| Key | Type | Description |
|-----|------|-------------|
| `id` | `String` | Platform ID (Windows GUID, Linux `wlan0`, etc.) |
| `name` | `String` | Friendly name |
| `mac_address` | `String` | Hardware MAC |
| `ip_address` | `String` | Current IPv4 |
| `type` | `String` | `wifi`, `ethernet`, `virtual`, `other`, `unknown` |
| `is_up` | `bool` | Interface administratively up |
| `is_connected` | `bool` | Has active link |

#### Connectivity (`ConnectivityInfo`)

| Key | Type | Description |
|-----|------|-------------|
| `state` | `String` | `disconnected`, `connecting`, `connected`, `failed` |
| `is_wifi_connected` | `bool` | Associated with Wi-Fi |
| `connected_ssid` | `String` | Active SSID |
| `local_ip` | `String` | Assigned IPv4 |
| `gateway` | `String` | Default gateway |
| `dns_primary` | `String` | Primary DNS |

### Example

```gdscript
extends Node

func _ready() -> void:
    WiFi.scan_completed.connect(_on_scan_completed)
    WiFi.disconnect_completed.connect(_on_disconnect_completed)
    WiFi.connectivity_changed.connect(_on_connectivity_changed)
    WiFi.fetch_adapters_async()

func _on_scan_completed(networks: Array, error: int, message: String) -> void:
    if not message.is_empty():
        print("Note: ", message)  # friendly UI text
    for net in networks:
        print(net["ssid"], " ", net["signal_strength"], "%")

func disconnect() -> void:
    WiFi.disconnect_from_wifi_async()

func _on_connectivity_changed(info: Dictionary) -> void:
    print("State: ", info.get("state"), " SSID: ", info.get("connected_ssid"))

func _on_disconnect_completed(error: int, message: String) -> void:
    if error == OK:
        print("Disconnected")
    else:
        print("Failed: ", message)
```

---

## Project layout

```
WifiGD/
├── addons/WifiGD/
│   ├── WifiGD.gdextension      # GDExtension manifest
│   └── bin/                    # Built .dll / .so / .dylib (gitignored)
├── demo/                       # Test UI project
│   ├── project.godot
│   ├── scenes/main.tscn
│   └── scripts/main.gd
├── godot-cpp/                  # Submodule (branch 4.3)
├── src/
│   ├── wifi_manager.{h,cpp}    # Public GDScript API, async tasks, signals
│   ├── network_types.{h,cpp}   # WifiNetwork, NetworkAdapter, ConnectivityInfo
│   ├── console_log.{h,cpp}     # Thread-safe log queue → Godot Output
│   ├── register_types.cpp      # Class registration
│   └── platform/
│       ├── network_backend.h   # Abstract backend interface
│       ├── network_backend.cpp # Platform factory
│       ├── windows/network_backend_windows.cpp
│       ├── linux/network_backend_linux.cpp   # ← implement here
│       └── macos/network_backend_macos.cpp
├── SConstruct
└── sync_addon.ps1              # Windows: copy DLL when demo is locked
```

---

## Building

### Prerequisites

| All platforms | |
|---------------|---|
| Python 3 + SCons | |
| C++17 compiler | |
| godot-cpp submodule | `git submodule update --init --recursive` |

| Linux additional | |
|------------------|---|
| Build deps | `libnm-dev`, `pkg-config` (Debian/Ubuntu) |
| | `NetworkManager-devel`, `pkg-config` (openSUSE) |
| | `NetworkManager-libnm-devel`, `pkg-config` (Fedora) |
| Runtime | NetworkManager running, Wi-Fi enabled; polkit for connect/radio |

| Windows additional | |
|--------------------|---|
| Build | Visual Studio Build Tools or MSVC |
| Runtime | WLAN AutoConfig service, Wi-Fi adapter |

### Commands

```bash
scons platform=linux                        # debug
scons platform=linux target=template_release
scons platform=windows
scons platform=macos
```

Parallel builds: `scons platform=linux -j$(nproc)`

---

## Tests

Unit and integration tests use [GUT](https://github.com/bitwes/Gut) in the demo project.

```bash
./run_tests.sh
```

| Suite | Location | Backend |
|-------|----------|---------|
| Unit (19 tests) | `demo/tests/unit/test_wifi_manager.gd` | Mock (`WIFIGD_MOCK_BACKEND=1`) |
| Linux integration (5 tests) | `demo/tests/integration/test_wifi_manager_linux.gd` | Real NetworkManager |

The mock backend is enabled per-test via `OS.set_environment("WIFIGD_MOCK_BACKEND", "1")` before creating a `WifiManager` instance. Integration tests skip automatically on non-Linux platforms.

Run only unit tests:

```bash
godot --headless --path demo -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gdir=res://tests/unit/
```

### Real Wi-Fi connect tests (`.env`)

Live connect/disconnect tests read credentials from `demo/.env` (gitignored):

```bash
cp demo/.env.example demo/.env
# Edit demo/.env — set WIFI_SSID and WIFI_PASSWORD
./run_real_wifi_tests.sh
```

| Variable | Required | Description |
|----------|----------|-------------|
| `WIFI_SSID` | Yes | Network name to join |
| `WIFI_PASSWORD` | No | Empty for open networks |
| `WIFI_ADAPTER_ID` | No | e.g. `wlan0`; empty uses default adapter |

Tests live in `demo/tests/live/` and use the **real NetworkManager backend** (not the mock). Connect may prompt for polkit/sudo approval. They are **not** run by `./run_tests.sh` — only via `./run_real_wifi_tests.sh`.

---


## Roadmap

### Now

- [x] Linux libnm backend: adapters, scan, connect, disconnect
- [x] Linux `SConstruct` pkg-config wiring
- [x] Polkit / permission error messages
- [x] `Node` autoload wrapper scene

### Next

- [ ] Fix Windows connect end-to-end (association confirmation)
- [ ] macOS CoreWLAN backend
- [ ] Platform capability probe (`get_platform_capabilities()`)
- [ ] Saved networks / forget profile
- [ ] libnm event thread (replace connectivity polling)

### Later

- Enterprise WPA (802.1X)
- Android / iOS (heavily restricted APIs)

---

## Contributing

1. Pick a backend method in `network_backend_linux.cpp`.
2. Implement against `NetworkBackend` — keep Godot calls off worker threads.
3. Use `log_to_console()` for detailed logs, friendly strings for `set_error()`.
4. Test with `demo/` before opening a PR.

There are no commits on `master` yet — early contributions welcome.

---

## License

License not yet specified.