#pragma once
#include <string>

class VimEngine;

enum class DotCommand {
    NONE,
    DD,
    DW,
    DB,
    DE,
    D_CARET,
    D_ZERO,
    D_TOP,
    D_END,
    D_DOLLAR,
    X,
    CAP_X,
    DJ,
    DK
};

// Computes the end column (exclusive) of the word starting at `cx` on `line`.
// Used by DotDW (delete word) and yw (yank word) in normal mode.
int compute_dw_end(const std::string& line, int cx);

// Returns the last dot-command that was executed (for `.` repeat).
DotCommand get_last_dot_command();

// Dispatches a dot-command to its registered action.
void execute_dot_command(VimEngine& engine, DotCommand cmd, bool is_repeat);