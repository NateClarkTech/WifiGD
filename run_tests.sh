#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ ! -f "demo/addons/WifiGD/bin/libwifigd.linux.template_debug.x86_64.so" ]]; then
	echo "Building WifiGD debug library..."
	scons platform=linux -j"$(nproc)"
fi

echo "Importing project assets (required for GUT)..."
godot --headless --path demo --import >/dev/null 2>&1 || true

echo "Running GUT unit + integration tests..."
set +e
godot --headless --path demo -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gexit "$@"
exit_code=$?
set -e

if [[ $exit_code -eq 0 ]]; then
	echo "Tests passed."
else
	echo "Tests failed (exit code $exit_code)." >&2
fi

exit $exit_code