#pragma once
#include <string>
#include <vector>

// Vim-style '%' matcher. If (y, x) is on a bracket, its partner is returned
// in (out_y, out_x). Otherwise the first bracket at or after x on the same
// line is used. Brackets inside strings and comments are NOT skipped.
// Returns false if no bracket is found.
bool find_matching_bracket(const std::vector<std::string>& lines,
                           int y, int x,
                           int& out_y, int& out_x);