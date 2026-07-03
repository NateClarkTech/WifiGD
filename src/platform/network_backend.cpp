#include "network_backend.h"

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

std::unique_ptr<NetworkBackend> create_network_backend() {
#if defined(_WIN32)
	return std::make_unique<NetworkBackendWindows>();
#elif defined(__APPLE__)
	return std::make_unique<NetworkBackendMacOS>();
#elif defined(__linux__)
	return std::make_unique<NetworkBackendLinux>();
#endif
}

} // namespace wifigd