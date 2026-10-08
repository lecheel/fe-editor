#include "engine.hpp"
#include "util/indent.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <string>

using namespace Keymap;

void VimEngine::toggle_line_comments() {
    auto& win = active_win();
    auto& buf = active_buf();

    std::string lang = buf.syntax ? buf.syntax->get_language() : "";
    if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);

    std::string prefix = get_line_comment_prefix(lang);
    if (prefix.empty()) {
        set_info_msg("Comment toggle: unsupported language '" + lang + "'");
        return;
    }

    std::set<int> target_lines;
    if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) {
        Cursor primary = win.cursors.front();
        int min_y = std::min(win.visual_anchor.y, primary.y);
        int max_y = std::max(win.visual_anchor.y, primary.y);
        for (int y = min_y; y <= max_y; ++y) target_lines.insert(y);
    } else {
        for (const auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size()))
                target_lines.insert(c.y);
        }
    }
    if (target_lines.empty()) return;

    buf.push_undo(win.cursors);

    bool all_commented = true;
    for (int y : target_lines) {
        if (y < 0 || y >= static_cast<int>(buf.lines.size())) continue;
        const std::string& line = buf.lines[y];
        bool only_ws = true;
        for (char c : line) {
            if (!std::isspace(static_cast<unsigned char>(c))) { only_ws = false; break; }
        }
        if (only_ws) continue;
        size_t p = 0;
        while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) p++;
        if (line.compare(p, prefix.size(), prefix) != 0) {
            all_commented = false;
            break;
        }
    }

    int toggled_count = 0;
    for (int y : target_lines) {
        if (y < 0 || y >= static_cast<int>(buf.lines.size())) continue;
        std::string& line = buf.lines[y];
        bool only_ws = true;
        for (char c : line) {
            if (!std::isspace(static_cast<unsigned char>(c))) { only_ws = false; break; }
        }
        if (only_ws) continue;
        toggled_count++;

        size_t p = 0;
        while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) p++;

        if (all_commented) {
            if (line.compare(p, prefix.size(), prefix) == 0) {
                size_t remove_end = p + prefix.size();
                if (remove_end < line.size() && line[remove_end] == ' ') remove_end++;
                int removed = static_cast<int>(remove_end - p);
                line.erase(p, removed);
                for (auto& c : win.cursors) {
                    if (c.y == y && c.x > static_cast<int>(p))
                        c.x = std::max(static_cast<int>(p), c.x - removed);
                }
            }
        } else {
            line.insert(p, prefix + " ");
            int added = static_cast<int>(prefix.size() + 1);
            for (auto& c : win.cursors) {
                if (c.y == y && c.x > static_cast<int>(p))
                    c.x += added;
            }
        }
    }

    buf.modified = true;
    buf.version++;
    buf.invalidate_hunks();
    if (buf.syntax) buf.syntax->update_text(buf.lines);
    win.clamp_all_cursors(buf, mode);
    update_window_scroll(win, buf);

    if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) {
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
    }

    set_info_msg(std::string(all_commented ? "Uncommented " : "Commented ") +
                 std::to_string(toggled_count) + " line(s)");
}

bool VimEngine::handle_global_shortcuts(const ncinput& ni, uint32_t key) {
    if (is_alt(ni, key, 'q')) {
        running = false;
        return true;
    }

    bool popup_active = show_git_status ||
                        show_hunk_diff ||
                        show_theme_popup ||
                        show_mini_help ||
                        show_buffer_list ||
                        show_settings_popup ||
                        show_rg_popup ||
                        show_filepicker ||
                        show_git_hunk_popup ||
                        show_workspace_list ||
                        show_whichkey_popup ||
                        leader_pending ||
                        ctrl_w_pending ||
                        leader_p_pending ||
                        show_cmd_completion;
    if (popup_active) {
        return false;
    }

    if (is_alt(ni, key, 'e')) {
        open_filepicker();
        return true;
    }

    // Alt-0 (or Alt-W) opens workspace slots popup (:ws)
    if (ni.alt && !ni.ctrl) {
        uint32_t base_k = (key >= 32 && key < 127) ? key : ni.id;
        if (base_k == '0' || base_k == 'W' || (ni.shift && (base_k == 'w' || key == 'w'))) {
            open_workspace_list();
            return true;
        }
    }

    if (mode != Mode::COMMAND) {
        if (is_alt(ni, key, 'b')) {
            open_buffer_list();
            return true;
        }
        if (is_alt(ni, key, '-') || is_alt(ni, key, '_')) {
            prev_buffer();
            return true;
        }
        if (is_alt(ni, key, '=') || is_alt(ni, key, '+')) {
            next_buffer();
            return true;
        }
        if (is_alt(ni, key, 's')) {
            split_window(SplitType::HORIZONTAL);
            return true;
        }
        if (is_alt(ni, key, 'v')) {
            split_window(SplitType::VERTICAL);
            return true;
        }
        if (is_alt(ni, key, 'x')) {
            close_active_window();
            return true;
        }
        if (key == '\t' || is_alt(ni, key, 'w')) {
            active_win_idx = (active_win_idx + 1) % windows.size();
            set_info_msg("Focused Window #" + std::to_string(windows[active_win_idx].id));
            return true;
        }
    }

    return false;
}