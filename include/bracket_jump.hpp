#pragma once
#include "types.hpp"

class VimEngine;

// Moves every cursor to its matching bracket. Shared by the '%' key in
// normal/visual mode and the "match_bracket" keymap action.
bool jump_matching_bracket(VimEngine& engine, Mode mode);