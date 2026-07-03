#pragma once

#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace wifigd {

// Thread-safe: may be called from WorkerThreadPool tasks.
void log_to_console(const godot::String &message);

// Drain queued messages; call from the main thread before printing to Godot Output.
std::vector<godot::String> take_console_logs();

} // namespace wifigd