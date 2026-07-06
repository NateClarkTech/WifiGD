# WifiGD

Cross-platform Wi-Fi management for Godot 4 via GDExtension. Enumerate adapters, scan networks, connect, disconnect, and read connectivity — from GDScript without blocking the main thread.

**Godot:** 4.3+  
**Status:** Early development. **Windows** and **Linux** backends are functional for scan, connect, disconnect, and connectivity. macOS is not implemented yet.

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
| `is_wifi_enabled` | **Working** | **Working** | Not implemented |
| `set_wifi_enabled` | Partial | Partial | Not implemented |
| `scan_wifi_networks` | **Working** | **Working** | Not implemented |
| `scan_wifi_networks_async` | **Working** | **Working** | Not implemented |
| `connect_to_wifi` | **Working** | **Working** | Not implemented |
| `connect_to_wifi_async` | **Working** | **Working** | Not implemented |
| `disconnect_from_wifi` | **Working** | **Working** | Not implemented |
| `disconnect_from_wifi_async` | **Working** | **Working** | Not implemented |
| `get_network_adapters` | **Working** | **Working** | Not implemented |
| `fetch_adapters_async` | **Working** | **Working** | Not implemented |
| `get_connectivity_info` | **Working** | **Working** | Not implemented |
| `get_last_error` | Working | Working | Working |

**Windows notes:** Uses the Native Wi-Fi API (`wlanapi`). Scan waits for `wlan_notification_acm_scan_complete`; connect waits for `wlan_notification_acm_connection_complete`. Requires [Location permission](#permissions) for active scans. `set_wifi_enabled` may require running as administrator.

**Linux notes:** Requires NetworkManager (libnm). Connect, disconnect, and radio toggle may prompt [polkit authorization](#permissions). Open and WPA2-PSK networks supported. IWD-only or ConnMan setups are not supported.

**macOS notes:** Backend in `src/platform/macos/` is a stub.

---

## Permissions

Wi-Fi APIs on desktop OSes are gated by system policy. If scan or connect fails with access-denied style errors, check the platform requirements below.

### Windows

| Capability | Requirement |
|------------|-------------|
| **Scan for nearby networks** | **Location services must be on.** Settings → Privacy & security → Location → Location services **On**. Allow location access for the app (Godot / your exported game) when prompted. Without this, `WlanScan` and network list APIs return `ERROR_ACCESS_DENIED` and WifiGD falls back to cached results when available. |
| **Connect / disconnect** | Usually works for the current user without elevation. Uses saved WLAN profiles via `WlanSetProfile` + `WlanConnect`. |
| **Toggle Wi-Fi radio** | May require running Godot or your game **as administrator**. |
| **Read current SSID / IP** | Associated network info works without extra prompts; full scan still follows location rules above. |

Microsoft documents this behavior in [Changes to API behavior for Wi-Fi access and location](https://learn.microsoft.com/windows/win32/nativewifi/wi-fi-access-location-changes).

### Linux

| Capability | Requirement |
|------------|-------------|
| **Scan, connect, disconnect** | **NetworkManager** must be running. The user session needs permission to control networking — typically via **polkit**. Connect/disconnect or radio changes may show an authorization dialog (or fail with “Permission denied” in headless/CI unless policy allows it). |
| **Equivalent to Windows Location** | There is no separate “location” toggle for Wi-Fi scan on Linux. Access is governed by NetworkManager + polkit (e.g. `org.freedesktop.NetworkManager.wifi.share.open`, `org.freedesktop.NetworkManager.network-control`). Ensure your user can modify connections (`nmcli` works without sudo on your machine). |
| **Headless / CI** | Run tests as a user with passwordless polkit rules for NetworkManager, or expect connect tests to be **pending** when authorization is denied. |

### macOS

Not implemented — no permissions guidance yet.

---

## Quick start

### Clone and build

```bash
git clone <your-repo-url> WifiGD
cd WifiGD
git submodule update --init --recursive

# Windows (PowerShell)
scons platform=windows

# Linux (local build — uses your host glibc)
scons platform=linux

# Release build
scons platform=linux target=template_release

# Linux (Docker — older glibc for distribution; recommended for published binaries)
./build_linux_docker.sh --all-targets
```

Binaries are written to `addons/WifiGD/bin/` and mirrored to `demo/addons/WifiGD/bin/`.

Prebuilt GDExtension binaries are committed under `addons/WifiGD/bin/` and mirrored to `demo/addons/WifiGD/bin/`. Rebuild with `scons` when you change native code, then commit updated `.dll` / `.so` / `.dylib` files for your platforms.

**Linux distribution builds:** Binaries compiled on a recent distro (e.g. openSUSE Tumbleweed, Ubuntu 24.04) link against a newer glibc and may fail to load on older systems. Use `./build_linux_docker.sh` to compile inside Ubuntu 20.04 (glibc 2.31) so the `.so` works on more Linux machines. The prebuilt Linux libraries in this repo are built that way.

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
| `local_ip` | `String` | Assigned IP (IPv4 preferred, IPv6 fallback on Windows) |
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
│   └── bin/                    # Shipped .dll / .so / .dylib (tracked in git)
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
│       ├── linux/network_backend_linux.cpp
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
| Runtime | WLAN AutoConfig service, Wi-Fi adapter, Location services enabled for scan |

### Commands

```bash
scons platform=linux                        # debug
scons platform=linux target=template_release
scons platform=windows
scons platform=macos
```

Parallel builds: `scons platform=linux -j$(nproc)`

### Linux: Docker build (older glibc)

If you build on a modern Linux host, the resulting `.so` may require a glibc newer than what your users have (e.g. `GLIBC_2.43` on Tumbleweed vs `2.31` on Ubuntu 20.04). For add-ons you ship to others — including the binaries committed to this repository — build inside Ubuntu 20.04 via Docker:

**Requirements:** [Docker](https://docs.docker.com/get-docker/) installed and running.

```bash
# Debug + release (typical for publishing)
./build_linux_docker.sh --all-targets

# Single target
./build_linux_docker.sh target=template_release

# Rebuild the container image after Dockerfile changes
./build_linux_docker.sh --rebuild-image --all-targets
```

The script builds image `wifigd-builder:focal`, cleans stale object files, runs `scons platform=linux` in the container, and prints the GLIBC versions the binary needs. Override the image name with `WIFIGD_DOCKER_IMAGE` if needed.

Use `--no-clean` to keep an existing godot-cpp cache between runs (faster, but only if the previous build was also from this container).

---

## Tests

Tests use [GUT](https://github.com/bitwes/Gut) in the `demo/` project. GUT is installed under `demo/addons/gut/`.

### Automated test suites

| Suite | Tests | Location | Backend | Run by default? |
|-------|-------|----------|---------|-----------------|
| **Unit** | 19 | `demo/tests/unit/test_wifi_manager.gd` | Mock (`WIFIGD_MOCK_BACKEND=1`) | Yes (`run_tests`) |
| **Integration** | 5 | `demo/tests/integration/test_wifi_manager_integration.gd` | Real OS (Linux + Windows) | Yes (`run_tests`) |
| **Live** | 5 | `demo/tests/live/test_wifi_manager_real_connect.gd` | Real OS + your `.env` network | No (separate script) |

**Unit tests** exercise the GDScript API without hardware: async signals, busy guards, mock scan/connect/disconnect, and cached state. They run on any platform where the GDExtension loads.

**Integration tests** hit the real Windows or Linux backend: list adapters, read connectivity, async scan shape, and radio state. They skip on macOS. Scan may **pending** if permissions are missing (see [Permissions](#permissions)).

**Live tests** connect to a real network named in `.env`, verify association and IP, disconnect once, then reconnect to restore your machine's Wi-Fi. They are destructive-adjacent (brief disconnect) — run only when you intend to test against your own AP.

### Run unit + integration tests

```bash
./run_tests.sh          # Linux / macOS
```

```powershell
.\run_tests.ps1         # Windows
```

Or directly:

```bash
godot --headless --path demo -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gexit
```

Unit tests only:

```bash
godot --headless --path demo -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gexit -gdir=res://tests/unit/
```

The mock backend is selected when `WIFIGD_MOCK_BACKEND=1` is set before creating `WifiManager`. Unit tests set this via `OS.set_environment()`; on Windows the extension also reads Godot's environment API so the mock is used reliably.

### Live tests (real connect / disconnect)

Live tests read credentials from `.env` at the **repo root** (`WifiGD/.env`, gitignored). The test helper also checks `demo/.env` and process environment variables.

```bash
cp .env.example .env
# Edit .env — set WIFI_SSID and WIFI_PASSWORD
./run_real_wifi_tests.sh    # Linux
```

```powershell
Copy-Item .env.example .env
# Edit .env
.\run_real_wifi_tests.ps1   # Windows
```

| Variable | Required | Description |
|----------|----------|-------------|
| `WIFI_SSID` | Yes | Network name to join |
| `WIFI_PASSWORD` | No | Empty for open networks |
| `WIFI_ADAPTER_ID` | No | Linux: `wlan0`; Windows: adapter GUID; empty = default |

Live suite is **not** included in `demo/.gutconfig.json` (which only lists `unit/` and `integration/`). Always run via `run_real_wifi_tests` or `-gdir=res://tests/live/`.

**Before live tests:** enable [Permissions](#permissions) (Windows Location; Linux NetworkManager + polkit). Close Godot before rebuilding the DLL, or run `.\sync_addon.ps1` on Windows if the demo copy is locked.

---


## Roadmap

### Now

- [x] Linux libnm backend: adapters, scan, connect, disconnect
- [x] Linux `SConstruct` pkg-config wiring
- [x] Polkit / permission error messages
- [x] `Node` autoload wrapper scene

### Next

- [x] Fix Windows connect end-to-end (association confirmation)
- [ ] macOS CoreWLAN backend
- [ ] Platform capability probe (`get_platform_capabilities()`)
- [ ] Saved networks / forget profile
- [ ] libnm event thread (replace connectivity polling)

### Later

- Enterprise WPA (802.1X)
- Android / iOS (heavily restricted APIs)

---

## Contributing

1. Implement against `NetworkBackend` in `src/platform/<os>/` — keep Godot calls off worker threads.
2. Use `log_to_console()` for detailed logs, friendly strings for `set_error()`.
3. Run `./run_tests.sh` or `.\run_tests.ps1` before opening a PR; use live tests when changing connect/scan behavior.
4. Document new permission requirements in the [Permissions](#permissions) section.

---

## License

License not yet specified.