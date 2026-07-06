#include "network_backend.h"

#include "mock/network_backend_mock.h"

#include <godot_cpp/classes/os.hpp>

#include <cstdlib>

#if defined(_WIN32)
#include "windows/network_backend_windows.h"
#elif defined(__APPLE__)
#include "macos/network_backend_macos.h"
#elif defined(__linux__)
#include "linux/network_backend_linux.h"
#else
#error "WifiGD: unsupported desktop platform"
#endif

namespace wifigd {

namespace {

bool mock_flag_enabled(const godot::String &value) {
	return !value.is_empty() && value != "0" && value.to_lower() != "false";
}

bool use_mock_backend() {
	// Godot's OS.set_environment() is not always visible to std::getenv() on Windows.
	if (godot::OS *os = godot::OS::get_singleton()) {
		if (mock_flag_enabled(os->get_environment("WIFIGD_MOCK_BACKEND"))) {
			return true;
		}
	}

	const char *flag = std::getenv("WIFIGD_MOCK_BACKEND");
	return flag != nullptr && flag[0] != '\0' && flag[0] != '0';
}

} // namespace

std::unique_ptr<NetworkBackend> create_network_backend() {
	if (use_mock_backend()) {
		return std::make_unique<NetworkBackendMock>();
	}
#if defined(_WIN32)
	return std::make_unique<NetworkBackendWindows>();
#elif defined(__APPLE__)
	return std::make_unique<NetworkBackendMacOS>();
#elif defined(__linux__)
	return std::make_unique<NetworkBackendLinux>();
#endif
}

} // namespace wifigd