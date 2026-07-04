#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

ENV_FILE=".env"

if [[ ! -f "$ENV_FILE" ]]; then
	echo "Missing $ENV_FILE" >&2
	echo "Copy .env.example to .env and set WIFI_SSID / WIFI_PASSWORD." >&2
	exit 1
fi

# Export variables from .env for the Godot process (shell overrides file if both set).
set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

if [[ -z "${WIFI_SSID:-}" ]]; then
	echo "WIFI_SSID is empty in $ENV_FILE" >&2
	exit 1
fi

if [[ ! -f "demo/addons/WifiGD/bin/libwifigd.linux.template_debug.x86_64.so" ]]; then
	echo "Building WifiGD debug library..."
	scons platform=linux -j"$(nproc)"
fi

echo "Importing project assets..."
godot --headless --path demo --import >/dev/null 2>&1 || true

echo "Running real Wi-Fi connect tests for SSID: ${WIFI_SSID}"
echo "Note: connect may prompt for polkit/sudo approval on Linux."
echo "Tests avoid disconnecting between cases; connection is restored after the disconnect test."

# Clean up leftover profiles from earlier WifiGD versions before the test run.
if command -v nmcli >/dev/null 2>&1; then
	nmcli connection delete "WifiGD Connection" >/dev/null 2>&1 || true
fi

set +e
godot --headless --path demo \
	-s addons/gut/gut_cmdln.gd \
	-gconfig=res://.gutconfig.json \
	-gexit \
	-gdir=res://tests/live/
exit_code=$?
set -e

if command -v nmcli >/dev/null 2>&1; then
	# Remove legacy profiles from older WifiGD builds only (never delete WIFI_SSID).
	nmcli connection delete "WifiGD Connection" >/dev/null 2>&1 || true
fi

if [[ $exit_code -eq 0 ]]; then
	echo "Real Wi-Fi tests passed."
else
	echo "Real Wi-Fi tests failed (exit code $exit_code)." >&2
fi

exit $exit_code