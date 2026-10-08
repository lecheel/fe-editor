#include "bracket_jump.hpp"
#include "engine.hpp"
#include "util/bracket_match.hpp"

bool jump_matching_bracket(VimEngine& engine, Mode mode) {
    auto& win = engine.active_win();
    auto& buf = engine.active_buf();
    bool moved = false;
    for (auto& c : win.cursors) {
        int ny = 0, nx = 0;
        if (find_matching_bracket(buf.lines, c.y, c.x, ny, nx)) {
            c.y = ny;
            c.x = nx;
            moved = true;
        }
    }
    if (!moved) {
        engine.set_info_msg("No matching bracket");
        return false;
    }
    win.clamp_all_cursors(buf, mode);
    win.deduplicate_cursors();
    engine.update_window_scroll(win, buf);
    return true;
}