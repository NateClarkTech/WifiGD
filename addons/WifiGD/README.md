# WifiGD

Cross-platform Wi-Fi scan, connect, and connectivity management for Godot 4.3+.

## Install

1. Install via the Godot Asset Library, or copy `addons/WifiGD/` into your project.
2. Confirm prebuilt binaries are present under `addons/WifiGD/bin/` for your platform (`template_debug` for the editor, `template_release` for exported games).
3. Either:
   - Register an autoload (recommended): add `wifi_manager_autoload.tscn` as a singleton (e.g. name it `WiFi`), **or**
   - Add a `WifiManager` node to your scene tree, **or**
   - Instance `example/wifi_demo.tscn` to try the reference UI.
4. Connect signals and call async methods from GDScript.

## Supported platforms

- **Windows 10/11** (x86_64) — supported (WLAN API; Location services required for scan)
- **Linux** (x86_64) — supported (NetworkManager / libnm; polkit may prompt for connect/radio)
- **macOS** — not implemented yet

## License

MIT — see [LICENSE.md](LICENSE.md).

## Full documentation

See the [project README](https://github.com/NateClarkTech/WifiGD/blob/main/README.md) for API reference, permissions, platform notes, and build instructions.