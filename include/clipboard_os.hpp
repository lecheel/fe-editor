#pragma once
#include <string>

// Copies text to the system clipboard via OSC 52 terminal escape sequence.
// Works over SSH. Handles tmux and screen passthrough.
void osc52_copy(const std::string& text);

// Reads the system clipboard via OSC 52 query. Returns "" on failure.
// Only works on POSIX systems; returns "" on Windows.
std::string read_osc52_clipboard();