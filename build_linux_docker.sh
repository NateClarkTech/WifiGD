#!/usr/bin/env bash
# Build WifiGD Linux binaries inside Ubuntu 20.04 (glibc 2.31) via Docker.
#
# Compiling on a newer host (e.g. openSUSE Tumbleweed, Ubuntu 24.04) links against
# a newer glibc and breaks the addon on older distros. This script rebuilds inside
# a Focal container so the .so works on more Linux systems.
#
# Usage:
#   ./build_linux_docker.sh
#   ./build_linux_docker.sh target=template_release
#   ./build_linux_docker.sh --no-clean target=template_debug -j8
#   ./build_linux_docker.sh --rebuild-image
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="${WIFIGD_DOCKER_IMAGE:-wifigd-builder:focal}"
DOCKERFILE="${ROOT}/docker/Dockerfile"

CLEAN=true
REBUILD_IMAGE=false
ALL_TARGETS=false
SCONS_ARGS=()

usage() {
	cat <<'EOF'
Usage: build_linux_docker.sh [options] [scons arguments...]

Options:
  --rebuild-image   Rebuild the Docker image (use --no-cache with docker build)
  --all-targets     Build template_debug and template_release in one run
  --clean           Remove cached build artifacts before compiling (default)
  --no-clean        Keep existing object files and godot-cpp build cache
  -h, --help        Show this help

SCons arguments are passed through. platform=linux is always set.
Examples:
  ./build_linux_docker.sh
  ./build_linux_docker.sh --all-targets
  ./build_linux_docker.sh target=template_release
  ./build_linux_docker.sh target=template_debug -j8
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--rebuild-image)
			REBUILD_IMAGE=true
			shift
			;;
		--clean)
			CLEAN=true
			shift
			;;
		--no-clean)
			CLEAN=false
			shift
			;;
		--all-targets)
			ALL_TARGETS=true
			shift
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			SCONS_ARGS+=("$1")
			shift
			;;
	esac
done

if ! command -v docker >/dev/null 2>&1; then
	echo "error: docker is required but was not found in PATH" >&2
	exit 1
fi

# Rootless Docker cannot write bind mounts when the container UID matches the host.
if [[ -z "${DOCKER_HOST:-}" && -S "/run/user/$(id -u)/docker.sock" ]]; then
	export DOCKER_HOST="unix:///run/user/$(id -u)/docker.sock"
fi

DOCKER_USER_ARGS=()
if [[ "${DOCKER_HOST:-}" == unix:///run/user/* ]]; then
	echo "Using rootless Docker (${DOCKER_HOST}); container runs as root (mapped to your user)."
else
	DOCKER_USER_ARGS=(-u "$(id -u):$(id -g)")
fi

if [[ ! -f "${DOCKERFILE}" ]]; then
	echo "error: missing Dockerfile at ${DOCKERFILE}" >&2
	exit 1
fi

if [[ ! -f "${ROOT}/godot-cpp/SConstruct" ]]; then
	echo "Initializing godot-cpp submodule..."
	git -C "${ROOT}" submodule update --init --recursive godot-cpp
fi

build_image() {
	local build_args=(-t "${IMAGE_NAME}" -f "${DOCKERFILE}" "${ROOT}/docker")
	if [[ "${REBUILD_IMAGE}" == true ]]; then
		build_args=(--no-cache "${build_args[@]}")
	fi
	echo "Building Docker image ${IMAGE_NAME} (Ubuntu 20.04 / glibc 2.31)..."
	docker build "${build_args[@]}"
}

if [[ "${REBUILD_IMAGE}" == true ]] || ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
	build_image
fi

if [[ "${CLEAN}" == true ]]; then
	echo "Cleaning host build artifacts (avoids mixing host/container object files)..."
	rm -rf "${ROOT}/godot-cpp/bin" "${ROOT}/godot-cpp/gen" "${ROOT}/.sconsign.dblite"
	find "${ROOT}/src" -name '*.os' -delete 2>/dev/null || true
fi

JOBS="$(nproc 2>/dev/null || echo 4)"
BASE_SCONS_ARGS=(platform=linux -j"${JOBS}")
if [[ ${#SCONS_ARGS[@]} -gt 0 ]]; then
	BASE_SCONS_ARGS+=("${SCONS_ARGS[@]}")
fi

run_scons() {
	local -a cmd=(scons "${BASE_SCONS_ARGS[@]}" "$@")
	echo "Running inside ${IMAGE_NAME}: ${cmd[*]}"
	docker run --rm \
		"${DOCKER_USER_ARGS[@]}" \
		-e HOME=/tmp \
		-v "${ROOT}:/workspace" \
		-w /workspace \
		"${IMAGE_NAME}" \
		"${cmd[@]}"
}

if [[ "${ALL_TARGETS}" == true ]]; then
	run_scons target=template_debug
	run_scons target=template_release
else
	run_scons
fi

report_glibc() {
	local so=""
	for candidate in \
		"${ROOT}/addons/WifiGD/bin/libwifigd.linux.template_release.x86_64.so" \
		"${ROOT}/addons/WifiGD/bin/libwifigd.linux.template_debug.x86_64.so"; do
		if [[ -f "${candidate}" ]]; then
			so="${candidate}"
			break
		fi
	done

	if [[ -z "${so}" ]]; then
		echo "warning: no Linux .so found under addons/WifiGD/bin/" >&2
		return
	fi

	echo
	echo "Built: ${so}"
	if command -v objdump >/dev/null 2>&1; then
		echo "GLIBC symbols required (host check):"
		objdump -T "${so}" \
			| grep 'GLIBC_' \
			| grep -v 'GLIBCXX' \
			| sed -E 's/.*(GLIBC_[0-9.]+).*/\1/' \
			| sort -u -t_ -k2 -V
	elif docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
		echo "GLIBC symbols required (container check):"
		docker run --rm -v "${so}:/lib.so:ro" "${IMAGE_NAME}" \
			objdump -T /lib.so \
			| grep 'GLIBC_' \
			| grep -v 'GLIBCXX' \
			| sed -E 's/.*(GLIBC_[0-9.]+).*/\1/' \
			| sort -u -t_ -k2 -V
	fi
}

report_glibc