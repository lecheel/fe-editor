#include "engine.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

bool VimEngine::execute_substitute(const std::string& cmd_str) {
    std::string str = cmd_str;
    size_t s = 0;
    while (s < str.size() && std::isspace(static_cast<unsigned char>(str[s]))) s++;
    if (s < str.size() && str[s] == ':') s++;
    while (s < str.size() && std::isspace(static_cast<unsigned char>(str[s]))) s++;
    str = str.substr(s);

    if (str.empty()) return false;

    auto& win = active_win();
    auto& buf = active_buf();

    int start_line = -1;
    int end_line = -1;
    size_t after_sub_cmd = std::string::npos;

    if (str.rfind("%s", 0) == 0) {
        start_line = 0;
        end_line = static_cast<int>(buf.lines.size()) - 1;
        after_sub_cmd = 2;
    } else if (str.rfind("%substitute", 0) == 0) {
        start_line = 0;
        end_line = static_cast<int>(buf.lines.size()) - 1;
        after_sub_cmd = 11;
    } else if (str.rfind("'<,'>s", 0) == 0) {
        start_line = (visual_range_start_y >= 0) ? visual_range_start_y : win.cursors.front().y;
        end_line = (visual_range_end_y >= 0) ? visual_range_end_y : win.cursors.front().y;
        after_sub_cmd = 6;
    } else if (str.rfind("'<,'>substitute", 0) == 0) {
        start_line = (visual_range_start_y >= 0) ? visual_range_start_y : win.cursors.front().y;
        end_line = (visual_range_end_y >= 0) ? visual_range_end_y : win.cursors.front().y;
        after_sub_cmd = 15;
    } else if (str.rfind("s", 0) == 0 && (str.size() == 1 || !std::isalpha(static_cast<unsigned char>(str[1])))) {
        start_line = win.cursors.front().y;
        end_line = win.cursors.front().y;
        after_sub_cmd = 1;
    } else if (str.rfind("substitute", 0) == 0 && (str.size() == 10 || !std::isalpha(static_cast<unsigned char>(str[10])))) {
        start_line = win.cursors.front().y;
        end_line = win.cursors.front().y;
        after_sub_cmd = 10;
    } else {
        size_t comma = str.find(',');
        if (comma != std::string::npos) {
            std::string r1 = str.substr(0, comma);
            size_t post_comma = comma + 1;
            while (post_comma < str.size() && (std::isdigit(static_cast<unsigned char>(str[post_comma])) || str[post_comma] == '.' || str[post_comma] == '$')) {
                post_comma++;
            }
            std::string r2 = str.substr(comma + 1, post_comma - (comma + 1));
            auto parse_val = [&](const std::string& val) -> int {
                if (val == ".") return win.cursors.front().y;
                if (val == "$") return static_cast<int>(buf.lines.size()) - 1;
                try { return std::stoi(val) - 1; } catch (...) { return -1; }
            };
            int v1 = parse_val(r1);
            int v2 = parse_val(r2);
            if (v1 >= 0 && v2 >= 0) {
                if (str.compare(post_comma, 10, "substitute") == 0) {
                    start_line = v1;
                    end_line = v2;
                    after_sub_cmd = post_comma + 10;
                } else if (str.compare(post_comma, 1, "s") == 0) {
                    start_line = v1;
                    end_line = v2;
                    after_sub_cmd = post_comma + 1;
                }
            }
        }
    }

    if (after_sub_cmd == std::string::npos) {
        return false;
    }

    while (after_sub_cmd < str.size() && std::isspace(static_cast<unsigned char>(str[after_sub_cmd]))) {
        after_sub_cmd++;
    }

    if (after_sub_cmd >= str.size()) {
        set_info_msg("E471: Argument required");
        return true;
    }

    char delim = str[after_sub_cmd++];
    if (std::isalnum(static_cast<unsigned char>(delim)) || std::isspace(static_cast<unsigned char>(delim))) {
        return false;
    }

    std::string pattern;
    std::string replacement;
    std::string flags;

    bool escaped = false;
    while (after_sub_cmd < str.size()) {
        char c = str[after_sub_cmd++];
        if (escaped) {
            if (c != delim) pattern += '\\';
            pattern += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == delim) {
            break;
        } else {
            pattern += c;
        }
    }
    if (escaped) pattern += '\\';

    escaped = false;
    while (after_sub_cmd < str.size()) {
        char c = str[after_sub_cmd++];
        if (escaped) {
            if (c == 'n') replacement += '\n';
            else if (c == 't') replacement += '\t';
            else if (c == '\\') replacement += '\\';
            else if (c == delim) replacement += delim;
            else {
                replacement += '\\';
                replacement += c;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == delim) {
            break;
        } else {
            replacement += c;
        }
    }
    if (escaped) replacement += '\\';

    while (after_sub_cmd < str.size()) {
        char c = str[after_sub_cmd++];
        if (!std::isspace(static_cast<unsigned char>(c))) {
            flags += c;
        }
    }

    if (pattern.empty()) {
        if (!search_query.empty()) {
            pattern = search_query;
        } else {
            set_info_msg("E35: No previous regular expression");
            return true;
        }
    }

    bool flag_global = false;
    bool flag_ignore_case = false;
    bool flag_match_case = false;
    for (char f : flags) {
        if (f == 'g' || f == 'G') flag_global = true;
        else if (f == 'i') flag_ignore_case = true;
        else if (f == 'I') flag_match_case = true;
    }

    bool case_sensitive = true;
    if (flag_match_case) {
        case_sensitive = true;
    } else if (flag_ignore_case) {
        case_sensitive = false;
    } else {
        bool has_upper = false;
        for (char c : pattern) {
            if (std::isupper(static_cast<unsigned char>(c))) {
                has_upper = true;
                break;
            }
        }
        case_sensitive = has_upper;
    }

    if (buf.lines.empty()) {
        set_info_msg("E486: Pattern not found: " + pattern);
        return true;
    }

    int max_y = static_cast<int>(buf.lines.size()) - 1;
    start_line = std::clamp(start_line, 0, max_y);
    end_line = std::clamp(end_line, 0, max_y);
    if (start_line > end_line) std::swap(start_line, end_line);

    auto find_match_in_line = [&](const std::string& line, size_t pos) -> size_t {
        if (pos >= line.size() && !line.empty()) return std::string::npos;
        if (case_sensitive) {
            return line.find(pattern, pos);
        } else {
            auto it = std::search(line.begin() + pos, line.end(),
                                  pattern.begin(), pattern.end(),
                                  [](char a, char b) {
                                      return std::tolower(static_cast<unsigned char>(a)) ==
                                             std::tolower(static_cast<unsigned char>(b));
                                  });
            if (it != line.end()) return std::distance(line.begin(), it);
            return std::string::npos;
        }
    };

    int total_substitutions = 0;
    int lines_affected = 0;
    for (int y = start_line; y <= end_line; ++y) {
        size_t p = find_match_in_line(buf.lines[y], 0);
        if (p != std::string::npos) {
            lines_affected++;
            while (p != std::string::npos) {
                total_substitutions++;
                if (!flag_global) break;
                p = find_match_in_line(buf.lines[y], p + std::max<size_t>(1, pattern.size()));
            }
        }
    }

    if (total_substitutions == 0) {
        set_info_msg("E486: Pattern not found: " + pattern);
        return true;
    }

    buf.push_undo(win.cursors);

    int cur_line = start_line;
    int end_target = end_line;
    while (cur_line <= end_target && cur_line < static_cast<int>(buf.lines.size())) {
        std::string line = buf.lines[cur_line];
        size_t p = find_match_in_line(line, 0);
        if (p != std::string::npos) {
            if (!flag_global) {
                line.replace(p, pattern.size(), replacement);
            } else {
                size_t step = std::max<size_t>(1, replacement.size());
                while (p != std::string::npos) {
                    line.replace(p, pattern.size(), replacement);
                    p = find_match_in_line(line, p + step);
                }
            }

            size_t nl = line.find('\n');
            if (nl == std::string::npos) {
                buf.lines[cur_line] = line;
                cur_line++;
            } else {
                std::vector<std::string> split_lines;
                std::string seg;
                for (char ch : line) {
                    if (ch == '\n') {
                        split_lines.push_back(seg);
                        seg.clear();
                    } else {
                        seg += ch;
                    }
                }
                split_lines.push_back(seg);

                buf.lines[cur_line] = split_lines[0];
                buf.lines.insert(buf.lines.begin() + cur_line + 1, split_lines.begin() + 1, split_lines.end());
                int added = static_cast<int>(split_lines.size()) - 1;
                end_target += added;
                cur_line += added + 1;
            }
        } else {
            cur_line++;
        }
    }

    buf.modified = true;
    buf.version++;
    buf.invalidate_hunks();
    if (buf.syntax) buf.syntax->update_text(buf.lines);
    win.clamp_all_cursors(buf, mode);
    update_window_scroll(win, buf);

    search_query = pattern;
    search_highlight_on = true;

    std::string msg = std::to_string(total_substitutions) + " substitution" +
                      (total_substitutions == 1 ? "" : "s") + " on " +
                      std::to_string(lines_affected) + " line" +
                      (lines_affected == 1 ? "" : "s");
    set_info_msg(msg);
    return true;
}