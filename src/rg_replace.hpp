#pragma once
// rg_replace.hpp — replace-pattern field for the ripgrep popup (F11 / :vg).
//
// Owns the replace pattern and which popup field (search vs. replace) has
// keyboard focus, so the "Tab replace cannot input the pattern" fix needs
// no new member variables in engine.hpp.
//
// Hookup (engine.cpp rg popup handler):
//   1. First thing in the handler:  if (rgreplace::handle_key(ni, key, rg_query)) return;
//   2. On Enter: field 0 -> run the search, then rgreplace::set_input_active(false);
//                field 1 -> set_info_msg(rgreplace::apply(*this, matches));
//   3. When the popup closes: rgreplace::reset();
#include <notcurses/notcurses.h>
#include <string>
#include <vector>

namespace rgreplace {

struct Match {
    std::string file; // path exactly as the popup reports it
    int line;         // 1-based line number
    int col;          // 1-based byte column where the match starts
    int len;          // match length in bytes
};

// ---- state -------------------------------------------------------------
void reset();                    // clear pattern + focus (popup close)
int  active_field();             // 0 = search pattern, 1 = replace pattern
bool input_active();             // true while typing goes to a text field
void set_input_active(bool on);  // set false after running the search
void toggle_field(bool shift);   // Tab (false) / Shift-Tab (true)
const std::string& pattern();    // current replace pattern
void set_pattern(const std::string& s);

// ---- key routing -------------------------------------------------------
// Feed every key while the rg popup is open. `search_field` is the
// engine's search string; it is edited in place while field 0 has focus.
// Returns true when the key was consumed (field switch / text edit) and
// the caller must stop processing it; false means "not mine", so the
// popup continues with its normal Enter/navigation/close handling.
bool handle_key(const ncinput& ni, uint32_t key, std::string& search_field);

// ---- apply -------------------------------------------------------------
// Replace every match with pattern(). If a file matches the active
// buffer's name, the buffer is edited in memory with undo support;
// every other file is rewritten on disk. Returns a status message for
// set_info_msg(). Window/cursor refresh is left to the caller (it needs
// engine state like Mode).
std::string apply(class VimEngine& eng, const std::vector<Match>& matches);

} // namespace rgreplace