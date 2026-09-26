#pragma once
#include "types.hpp"
#include "buffer.hpp"
#include <vector>
#include <algorithm>

struct Window {
    int id{0};
    size_t buffer_idx{0};
    int y{0};
    int x{0};
    int h{0};
    int w{0};
    int scroll_y{0};
    int scroll_x{0};
    std::vector<Cursor> cursors{{0, 0}};
    Cursor visual_anchor{0, 0};

    void deduplicate_cursors() {
        std::sort(cursors.begin(), cursors.end());
        cursors.erase(std::unique(cursors.begin(), cursors.end()), cursors.end());
    }

    int get_max_x(const TextBuffer& buf, int line_idx, Mode mode) const {
        if (line_idx < 0 || line_idx >= static_cast<int>(buf.lines.size())) return 0;
        int len = static_cast<int>(buf.lines[line_idx].size());
        if (mode == Mode::NORMAL) {
            return std::max(0, len - 1);
        }
        return len;
    }

    void clamp_all_cursors(const TextBuffer& buf, Mode mode) {
        int max_y = std::max(0, static_cast<int>(buf.lines.size()) - 1);
        for (auto& c : cursors) {
            c.y = std::clamp(c.y, 0, max_y);
            c.x = std::clamp(c.x, 0, get_max_x(buf, c.y, mode));
        }
        deduplicate_cursors();
    }
};