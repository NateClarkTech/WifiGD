#!/usr/bin/env python3

import os

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])

sources = [
    "src/register_types.cpp",
    "src/network_types.cpp",
    "src/console_log.cpp",
    "src/wifi_manager.cpp",
    "src/platform/network_backend.cpp",
]

if env["platform"] == "windows":
    sources.append("src/platform/windows/network_backend_windows.cpp")
    env.Append(LIBS=["wlanapi", "iphlpapi", "ws2_32"])
elif env["platform"] == "linux":
    sources.append("src/platform/linux/network_backend_linux.cpp")
elif env["platform"] == "macos":
    sources.append("src/platform/macos/network_backend_macos.cpp")
    env.Append(FRAMEWORKS=["CoreWLAN", "SystemConfiguration", "Network"])

library_name = "wifigd{}{}".format(env["suffix"], env["SHLIBSUFFIX"])
output_dirs = [
    os.path.join("addons", "WifiGD", "bin"),
    os.path.join("demo", "addons", "WifiGD", "bin"),
]

primary_library = env.SharedLibrary(
    os.path.join(output_dirs[0], library_name),
    source=sources,
)

targets = [primary_library]
for output_dir in output_dirs[1:]:
    mirror_path = os.path.join(output_dir, library_name)
    targets.append(env.Command(mirror_path, primary_library, Copy("$TARGET", "$SOURCE")))

# Warn when the demo copy fails (usually Godot has the DLL open).
env.AddPostAction(
    primary_library,
    lambda *args, **kwargs: print(
        "Built: addons/WifiGD/bin/%s\n"
        "If the demo copy failed, close Godot and run: .\\sync_addon.ps1"
        % library_name
    ),
)

Default(*targets)