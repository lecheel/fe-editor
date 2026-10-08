#include "util/bracket_match.hpp"
#include <algorithm>

bool find_matching_bracket(const std::vector<std::string>& lines,
                           int y, int x,
                           int& out_y, int& out_x) {
    if (y < 0 || y >= static_cast<int>(lines.size())) return false;
    static const std::string opens  = "([{";
    static const std::string closes = ")]}";

    const std::string& line = lines[y];
    int pos = -1;
    for (int i = std::max(0, x); i < static_cast<int>(line.size()); ++i) {
        if (opens.find(line[i]) != std::string::npos ||
            closes.find(line[i]) != std::string::npos) {
            pos = i;
            break;
        }
    }
    if (pos < 0) return false;

    char c = line[pos];
    bool forward = opens.find(c) != std::string::npos;
    size_t k = forward ? opens.find(c) : closes.find(c);
    const char open_c  = opens[k];
    const char close_c = closes[k];
    int depth = 0;

    if (forward) {
        for (int cy = y; cy < static_cast<int>(lines.size()); ++cy) {
            const std::string& l = lines[cy];
            for (int cx = (cy == y ? pos : 0);
                 cx < static_cast<int>(l.size()); ++cx) {
                if (l[cx] == open_c) {
                    depth++;
                } else if (l[cx] == close_c) {
                    if (--depth == 0) {
                        out_y = cy;
                        out_x = cx;
                        return true;
                    }
                }
            }
        }
    } else {
        for (int cy = y; cy >= 0; --cy) {
            const std::string& l = lines[cy];
            for (int cx = (cy == y ? pos : static_cast<int>(l.size()) - 1);
                 cx >= 0; --cx) {
                if (l[cx] == close_c) {
                    depth++;
                } else if (l[cx] == open_c) {
                    if (--depth == 0) {
                        out_y = cy;
                        out_x = cx;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}