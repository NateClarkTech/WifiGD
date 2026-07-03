#include "console_log.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace wifigd {

namespace {

std::mutex log_mutex;
std::vector<godot::String> pending_logs;

} // namespace

void log_to_console(const godot::String &message) {
	if (message.is_empty()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(log_mutex);
		pending_logs.push_back(message);
	}

	// Also emit to the OS terminal when Godot is launched from a shell.
	const godot::CharString utf8 = message.utf8();
	fprintf(stderr, "%s\n", utf8.get_data());
	fflush(stderr);
}

std::vector<godot::String> take_console_logs() {
	std::lock_guard<std::mutex> lock(log_mutex);
	std::vector<godot::String> logs = std::move(pending_logs);
	pending_logs.clear();
	return logs;
}

} // namespace wifigd