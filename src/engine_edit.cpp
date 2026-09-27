#include "engine.hpp"
#include "command.hpp"
#include "autocomplete.hpp"
#include "log.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>

namespace fs = std::filesystem;

REGISTER_COMMAND(
    minimap,
    (std::vector<std::string>{"minimap", "mm"}),
    "Toggle code minimap",
    [](CommandContext& ctx) {
        if (!ctx.argv.empty()) {
            ctx.engine.set_info_msg("Minimap: " + ctx.args);
        } else {
            ctx.engine.set_info_msg("Minimap toggled.");
        }
    }
);

REGISTER_COMMAND(
    hunkdiff,
    (std::vector<std::string>{"hunkdiff", "diff"}),
    "Toggle F14 side-by-side hunk diff view against HEAD (F5)",
    [](CommandContext& ctx) {
        ctx.engine.open_hunk_diff();
    }
);

REGISTER_COMMAND(
    gitview,
    (std::vector<std::string>{"git", "gitstatus", "gitview", "gs"}),
    "Open git status view (F1 / F6)",
    [](CommandContext& ctx) {
        ctx.engine.open_git_status();
    }
);

namespace {

enum class DotCommand {
    NONE,
    DD,
    DW,
    D_CARET,
    D_TOP,
    D_END,
    D_DOLLAR
};

DotCommand s_last_dot_cmd = DotCommand::NONE;
bool s_d_pending = false;
bool s_dg_pending = false;
bool s_y_pending = false;
bool s_g_pending = false;
bool s_visual_g_pending = false;

int compute_dw_end(const std::string& line, int cx) {
    int len = static_cast<int>(line.size());
    if (cx >= len) return cx;
    auto is_word = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };
    auto is_space = [](char ch) {
        return std::isspace(static_cast<unsigned char>(ch));
    };

    int p = cx;
    if (is_word(line[p])) {
        while (p < len && is_word(line[p])) p++;
        while (p < len && is_space(line[p])) p++;
    } else if (is_space(line[p])) {
        while (p < len && is_space(line[p])) p++;
    } else {
        while (p < len && !is_word(line[p]) && !is_space(line[p])) p++;
        while (p < len && is_space(line[p])) p++;
    }
    return p;
}

void execute_dot_command(VimEngine& engine, DotCommand cmd, bool is_repeat) {
    auto& win = engine.active_win();
    auto& buf = engine.active_buf();

    if (cmd == DotCommand::DD) {
        buf.push_undo(win.cursors);
        std::set<int> lines_to_delete;
        for (const auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                lines_to_delete.insert(c.y);
            }
        }
        engine.yank_reg.is_linewise = true;
        engine.yank_reg.lines.clear();
        engine.yank_reg.text.clear();
        for (int y : lines_to_delete) {
            engine.yank_reg.lines.push_back(buf.lines[y]);
            engine.yank_reg.text += buf.lines[y] + "\n";
        }
        std::vector<int> sorted_lines(lines_to_delete.rbegin(), lines_to_delete.rend());
        for (int y : sorted_lines) {
            if (buf.lines.size() > 1) {
                buf.lines.erase(buf.lines.begin() + y);
            } else {
                buf.lines[0] = "";
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::DD;
        engine.set_info_msg(is_repeat ? "Repeated: dd" : "Deleted line (dd)");
    } else if (cmd == DotCommand::DW) {
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                int end_x = compute_dw_end(line, c.x);
                if (end_x > c.x) {
                    line.erase(c.x, end_x - c.x);
                }
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::DW;
        engine.set_info_msg(is_repeat ? "Repeated: dw" : "Deleted word (dw)");
    } else if (cmd == DotCommand::D_CARET) {
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x > 0) {
                    int del_count = std::min(c.x, static_cast<int>(line.size()));
                    line.erase(0, del_count);
                    c.x = 0;
                }
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::D_CARET;
        engine.set_info_msg(is_repeat ? "Repeated: d^" : "Deleted to line start (d^)");
    } else if (cmd == DotCommand::D_TOP) {
        buf.push_undo(win.cursors);
        int target_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        target_y = std::clamp(target_y, 0, static_cast<int>(buf.lines.size()) - 1);
        buf.lines.erase(buf.lines.begin(), buf.lines.begin() + target_y + 1);
        if (buf.lines.empty()) {
            buf.lines.push_back("");
        }
        win.cursors = {{0, 0}};
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::D_TOP;
        engine.set_info_msg(is_repeat ? "Repeated: d0" : "Deleted to top of file (d0)");
    } else if (cmd == DotCommand::D_END) {
        buf.push_undo(win.cursors);
        int target_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        target_y = std::clamp(target_y, 0, static_cast<int>(buf.lines.size()) - 1);
        buf.lines.erase(buf.lines.begin() + target_y, buf.lines.end());
        if (buf.lines.empty()) {
            buf.lines.push_back("");
            win.cursors = {{0, 0}};
        } else {
            int new_y = std::min(target_y, static_cast<int>(buf.lines.size()) - 1);
            win.cursors = {{new_y, 0}};
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::D_END;
        engine.set_info_msg(is_repeat ? "Repeated: dG" : "Deleted to end of file (dG)");
    } else if (cmd == DotCommand::D_DOLLAR) {
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x < static_cast<int>(line.size())) {
                    line.erase(c.x);
                }
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::D_DOLLAR;
        engine.set_info_msg(is_repeat ? "Repeated: d$" : "Deleted to line end (d$)");
    }
}

} // namespace

bool VimEngine::handle_navigation(const ncinput& ni, uint32_t key) {
    auto& win = active_win();
    auto& buf = active_buf();

    switch (key) {
        case NCKEY_UP:
            for (auto& c : win.cursors) c.y = std::max(0, c.y - 1);
            win.clamp_all_cursors(buf, mode);
            return true;
        case NCKEY_DOWN:
            for (auto& c : win.cursors) c.y = std::min(static_cast<int>(buf.lines.size()) - 1, c.y + 1);
            win.clamp_all_cursors(buf, mode);
            return true;
        case NCKEY_LEFT:
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
            return true;
        case NCKEY_RIGHT:
            for (auto& c : win.cursors) c.x = std::min(win.get_max_x(buf, c.y, mode), c.x + 1);
            return true;
        case NCKEY_HOME:
            for (auto& c : win.cursors) c.x = 0;
            return true;
        case NCKEY_END:
            for (auto& c : win.cursors) c.x = win.get_max_x(buf, c.y, mode);
            return true;
        case NCKEY_PGUP:
            for (auto& c : win.cursors) c.y = std::max(0, c.y - win.h);
            win.clamp_all_cursors(buf, mode);
            return true;
        case NCKEY_PGDOWN:
            for (auto& c : win.cursors) c.y = std::min(static_cast<int>(buf.lines.size()) - 1, c.y + win.h);
            win.clamp_all_cursors(buf, mode);
            return true;
        default:
            return false;
    }
}

void VimEngine::handle_normal_mode(const ncinput& ni, uint32_t key) {
    AutocompleteState::instance().reset();
    auto& win = active_win();
    auto& buf = active_buf();

    if (s_d_pending) {
        s_d_pending = false;
        if (key == 'd') {
            execute_dot_command(*this, DotCommand::DD, false);
            return;
        } else if (key == 'w') {
            execute_dot_command(*this, DotCommand::DW, false);
            return;
        } else if (key == '^' || (ni.shift && (key == '6' || ni.id == '6'))) {
            execute_dot_command(*this, DotCommand::D_CARET, false);
            return;
        } else if (key == '0') {
            execute_dot_command(*this, DotCommand::D_TOP, false);
            return;
        } else if (key == 'G' || ni.id == 'G' || (ni.shift && (key == 'g' || ni.id == 'g'))) {
            execute_dot_command(*this, DotCommand::D_END, false);
            return;
        } else if (key == 'g') {
            s_dg_pending = true;
            set_info_msg("dg");
            return;
        } else if (key == '$' || (ni.shift && (key == '4' || ni.id == '4'))) {
            execute_dot_command(*this, DotCommand::D_DOLLAR, false);
            return;
        } else {
            set_info_msg("");
            if (key == NCKEY_ESC) return;
        }
    }

    if (s_dg_pending) {
        s_dg_pending = false;
        if (key == 'g') {
            execute_dot_command(*this, DotCommand::D_TOP, false);
            return;
        } else {
            set_info_msg("");
            if (key == NCKEY_ESC) return;
        }
    }

    if (s_y_pending) {
        s_y_pending = false;
        if (key == 'y') {
            std::set<int> lines_to_yank;
            for (const auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    lines_to_yank.insert(c.y);
                }
            }
            yank_reg.is_linewise = true;
            yank_reg.lines.clear();
            yank_reg.text.clear();
            for (int y : lines_to_yank) {
                yank_reg.lines.push_back(buf.lines[y]);
                yank_reg.text += buf.lines[y] + "\n";
            }
            set_info_msg(std::to_string(yank_reg.lines.size()) + " line(s) yanked (yy)");
            return;
        } else if (key == 'w') {
            Cursor primary = win.cursors.front();
            yank_reg.is_linewise = false;
            yank_reg.lines.clear();
            if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
                const std::string& line = buf.lines[primary.y];
                int end_x = compute_dw_end(line, primary.x);
                yank_reg.text = (end_x > primary.x) ? line.substr(primary.x, end_x - primary.x) : "";
            } else {
                yank_reg.text.clear();
            }
            yank_reg.lines = {yank_reg.text};
            set_info_msg("Word yanked (yw)");
            return;
        } else if (key == '$' || (ni.shift && (key == '4' || ni.id == '4'))) {
            Cursor primary = win.cursors.front();
            yank_reg.is_linewise = false;
            yank_reg.lines.clear();
            if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
                const std::string& line = buf.lines[primary.y];
                yank_reg.text = (primary.x < static_cast<int>(line.size())) ? line.substr(primary.x) : "";
            } else {
                yank_reg.text.clear();
            }
            yank_reg.lines = {yank_reg.text};
            set_info_msg("Yanked to line end (y$)");
            return;
        } else {
            set_info_msg("");
            if (key == NCKEY_ESC) return;
        }
    }

    if (s_g_pending) {
        s_g_pending = false;
        if (key == 'g') {
            for (auto& c : win.cursors) {
                c.y = 0;
                c.x = 0;
            }
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Top of file (gg)");
            return;
        } else {
            set_info_msg("");
            if (key == NCKEY_ESC) return;
        }
    }

    if (key == ' ') {
        leader_pending = true;
        show_whichkey_popup = false;
        leader_start_time = std::chrono::steady_clock::now();
        return;
    }

    if (key == 22 || (ni.ctrl && (ni.id == 'v' || ni.id == 'V'))) {
        mode = Mode::VISUAL_BLOCK;
        win.visual_anchor = win.cursors.front();
        win.cursors = {win.cursors.front()};
        set_info_msg("-- VISUAL BLOCK --");
        return;
    }

    if (ni.alt && (ni.id == 'k' || ni.id == 'K')) {
        Cursor primary = win.cursors.front();
        if (primary.y - 1 >= 0) {
            win.cursors.insert(win.cursors.begin(), {primary.y - 1, primary.x});
            win.clamp_all_cursors(buf, mode);
            set_info_msg("Multi-Cursor: Added cursor above [Alt-k]. Total: " + std::to_string(win.cursors.size()));
        }
        return;
    }

    // Direct Command Mode trigger
    // Some terminals (Kitty keyboard protocol) report the unshifted key (';')
    // with shift as a separate modifier flag instead of sending ':' directly.
    if (key == ':' || ni.id == ':' || (ni.utf8[0] == ':' && ni.utf8[1] == '\0') ||
        (ni.shift && (key == ';' || ni.id == ';'))) {
        LOGD("Entering COMMAND mode (key=%u id=%u utf8=%02x %02x shift=%d)", key, ni.id, (unsigned)ni.utf8[0], (unsigned)ni.utf8[1], ni.shift);
        mode = Mode::COMMAND;
        cmd_buffer.clear();
        return;
    }

    if (handle_navigation(ni, key)) return;

    switch (key) {
        case 'v':
            mode = Mode::VISUAL;
            win.visual_anchor = win.cursors.front();
            win.cursors = {win.cursors.front()};
            set_info_msg("-- VISUAL --");
            break;
        case 'h':
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
            break;
        case 'l':
            for (auto& c : win.cursors) c.x = std::min(win.get_max_x(buf, c.y, mode), c.x + 1);
            break;
        case 'k':
            for (auto& c : win.cursors) c.y = std::max(0, c.y - 1);
            win.clamp_all_cursors(buf, mode);
            break;
        case 'j':
            for (auto& c : win.cursors) c.y = std::min(static_cast<int>(buf.lines.size()) - 1, c.y + 1);
            win.clamp_all_cursors(buf, mode);
            break;
        case '0':
        case '^':
            for (auto& c : win.cursors) c.x = 0;
            break;
        case '$':
            for (auto& c : win.cursors) c.x = win.get_max_x(buf, c.y, mode);
            break;
        case 'd':
            s_d_pending = true;
            set_info_msg("d");
            break;
        case 'D':
            execute_dot_command(*this, DotCommand::D_DOLLAR, false);
            break;
        case 'y':
            s_y_pending = true;
            set_info_msg("y");
            break;
        case 'Y': {
            Cursor primary = win.cursors.front();
            yank_reg.is_linewise = true;
            yank_reg.lines.clear();
            yank_reg.text.clear();
            if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
                yank_reg.lines.push_back(buf.lines[primary.y]);
                yank_reg.text = buf.lines[primary.y] + "\n";
            }
            set_info_msg("1 line yanked (Y)");
            break;
        }
        case 'p': {
            if (yank_reg.lines.empty() && yank_reg.text.empty()) {
                set_info_msg("Nothing to paste.");
                break;
            }
            buf.push_undo(win.cursors);
            if (yank_reg.is_linewise) {
                Cursor primary = win.cursors.front();
                int insert_pos = std::min(primary.y + 1, static_cast<int>(buf.lines.size()));
                buf.lines.insert(buf.lines.begin() + insert_pos, yank_reg.lines.begin(), yank_reg.lines.end());
                for (auto& c : win.cursors) {
                    c.y = insert_pos;
                    c.x = 0;
                }
                buf.modified = true;
                buf.version++;
                buf.invalidate_hunks();
                if (buf.syntax) buf.syntax->update_text(buf.lines);
                win.clamp_all_cursors(buf, mode);
                update_window_scroll(win, buf);
                set_info_msg("Pasted " + std::to_string(yank_reg.lines.size()) + " line(s) below (p)");
            } else {
                for (auto& c : win.cursors) {
                    if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                        std::string& line = buf.lines[c.y];
                        int ins_x = std::min(c.x + (line.empty() ? 0 : 1), static_cast<int>(line.size()));
                        line.insert(ins_x, yank_reg.text);
                        c.x = ins_x + static_cast<int>(yank_reg.text.size()) - 1;
                        c.x = std::max(0, c.x);
                    }
                }
                buf.modified = true;
                buf.version++;
                buf.invalidate_hunks();
                if (buf.syntax) buf.syntax->update_text(buf.lines);
                win.clamp_all_cursors(buf, mode);
                update_window_scroll(win, buf);
                set_info_msg("Pasted text (p)");
            }
            break;
        }
        case 'P': {
            if (yank_reg.lines.empty() && yank_reg.text.empty()) {
                set_info_msg("Nothing to paste.");
                break;
            }
            buf.push_undo(win.cursors);
            if (yank_reg.is_linewise) {
                Cursor primary = win.cursors.front();
                int insert_pos = std::clamp(primary.y, 0, static_cast<int>(buf.lines.size()));
                buf.lines.insert(buf.lines.begin() + insert_pos, yank_reg.lines.begin(), yank_reg.lines.end());
                for (auto& c : win.cursors) {
                    c.y = insert_pos;
                    c.x = 0;
                }
                buf.modified = true;
                buf.version++;
                buf.invalidate_hunks();
                if (buf.syntax) buf.syntax->update_text(buf.lines);
                win.clamp_all_cursors(buf, mode);
                update_window_scroll(win, buf);
                set_info_msg("Pasted " + std::to_string(yank_reg.lines.size()) + " line(s) above (P)");
            } else {
                for (auto& c : win.cursors) {
                    if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                        std::string& line = buf.lines[c.y];
                        int ins_x = std::clamp(c.x, 0, static_cast<int>(line.size()));
                        line.insert(ins_x, yank_reg.text);
                        c.x = ins_x + static_cast<int>(yank_reg.text.size()) - 1;
                        c.x = std::max(0, c.x);
                    }
                }
                buf.modified = true;
                buf.version++;
                buf.invalidate_hunks();
                if (buf.syntax) buf.syntax->update_text(buf.lines);
                win.clamp_all_cursors(buf, mode);
                update_window_scroll(win, buf);
                set_info_msg("Pasted text (P)");
            }
            break;
        }
        case 'g':
            s_g_pending = true;
            set_info_msg("g");
            break;
        case 'G': {
            int last_y = std::max(0, static_cast<int>(buf.lines.size()) - 1);
            for (auto& c : win.cursors) {
                c.y = last_y;
                c.x = 0;
            }
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("End of file (G)");
            break;
        }
        case '.':
            if (s_last_dot_cmd == DotCommand::NONE) {
                set_info_msg("No previous change to repeat.");
            } else {
                execute_dot_command(*this, s_last_dot_cmd, true);
            }
            break;
        case 'C': {
            Cursor primary = win.cursors.back();
            if (primary.y + 1 < static_cast<int>(buf.lines.size())) {
                win.cursors.push_back({primary.y + 1, primary.x});
                win.clamp_all_cursors(buf, mode);
                set_info_msg("Multi-Cursor: Added cursor below [C]. Total: " + std::to_string(win.cursors.size()));
            }
            break;
        }
        case NCKEY_ESC:
            if (win.cursors.size() > 1) {
                win.cursors = {win.cursors.front()};
                set_info_msg("Multi-Cursor: Reset to single primary cursor.");
            }
            break;
        case 'b':
            next_buffer();
            break;
        case 'B':
            prev_buffer();
            break;
        case 'u':
            if (buf.undo(win.cursors)) {
                win.clamp_all_cursors(buf, mode);
                set_info_msg("Undo applied. Undo states left: " + std::to_string(buf.undo_stack.size()));
            } else {
                set_info_msg("Already at oldest change.");
            }
            break;
        case 'U':
            if (buf.redo(win.cursors)) {
                win.clamp_all_cursors(buf, mode);
                set_info_msg("Redo applied.");
            } else {
                set_info_msg("Already at newest change.");
            }
            break;
        case 'i':
            buf.push_undo(win.cursors);
            mode = Mode::INSERT;
            break;
        case 'a':
            buf.push_undo(win.cursors);
            mode = Mode::INSERT;
            for (auto& c : win.cursors) {
                c.x = std::min(static_cast<int>(buf.lines[c.y].size()), c.x + 1);
            }
            break;
        case 'o':
            buf.push_undo(win.cursors);
            for (auto& c : win.cursors) {
                buf.lines.insert(buf.lines.begin() + c.y + 1, "");
                c.y++;
                c.x = 0;
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            mode = Mode::INSERT;
            break;
    }
}

void VimEngine::handle_visual_mode(const ncinput& ni, uint32_t key) {
    AutocompleteState::instance().reset();
    auto& win = active_win();
    auto& buf = active_buf();

    if (key == NCKEY_ESC) {
        s_visual_g_pending = false;
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
        set_info_msg("");
        return;
    }

    if (s_visual_g_pending) {
        s_visual_g_pending = false;
        if (key == 'g') {
            for (auto& c : win.cursors) {
                c.y = 0;
                c.x = 0;
            }
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            return;
        }
    }

    if (key == ':' || ni.id == ':' || (ni.utf8[0] == ':' && ni.utf8[1] == '\0') ||
        (ni.shift && (key == ';' || ni.id == ';'))) {
        mode = Mode::COMMAND;
        cmd_buffer.clear();
        return;
    }

    if (handle_navigation(ni, key)) return;

    switch (key) {
        case 'h':
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
            break;
        case 'l':
            for (auto& c : win.cursors) c.x = std::min(win.get_max_x(buf, c.y, mode), c.x + 1);
            break;
        case 'k':
            for (auto& c : win.cursors) c.y = std::max(0, c.y - 1);
            win.clamp_all_cursors(buf, mode);
            break;
        case 'j':
            for (auto& c : win.cursors) c.y = std::min(static_cast<int>(buf.lines.size()) - 1, c.y + 1);
            win.clamp_all_cursors(buf, mode);
            break;
        case '0':
        case '^':
            for (auto& c : win.cursors) c.x = 0;
            break;
        case '$':
            for (auto& c : win.cursors) c.x = win.get_max_x(buf, c.y, mode);
            break;
        case 'g':
            s_visual_g_pending = true;
            break;
        case 'G': {
            int last_y = std::max(0, static_cast<int>(buf.lines.size()) - 1);
            for (auto& c : win.cursors) {
                c.y = last_y;
                c.x = 0;
            }
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            break;
        }
        case 'I': {
            if (mode == Mode::VISUAL_BLOCK) {
                buf.push_undo(win.cursors);
                Cursor primary = win.cursors.front();
                int min_y = std::min(win.visual_anchor.y, primary.y);
                int max_y = std::max(win.visual_anchor.y, primary.y);
                int min_x = std::min(win.visual_anchor.x, primary.x);

                win.cursors.clear();
                for (int y = min_y; y <= max_y; ++y) {
                    win.cursors.push_back({y, std::min(min_x, static_cast<int>(buf.lines[y].size()))});
                }
                mode = Mode::INSERT;
                set_info_msg("-- INSERT (MULTI-CURSOR BLOCK) --");
            }
            break;
        }
        case 'A': {
            if (mode == Mode::VISUAL_BLOCK) {
                buf.push_undo(win.cursors);
                Cursor primary = win.cursors.front();
                int min_y = std::min(win.visual_anchor.y, primary.y);
                int max_y = std::max(win.visual_anchor.y, primary.y);
                int max_x = std::max(win.visual_anchor.x, primary.x) + 1;

                win.cursors.clear();
                for (int y = min_y; y <= max_y; ++y) {
                    win.cursors.push_back({y, std::min(max_x, static_cast<int>(buf.lines[y].size()))});
                }
                mode = Mode::INSERT;
                set_info_msg("-- INSERT (MULTI-CURSOR BLOCK) --");
            }
            break;
        }
        case 'd':
        case 'x': {
            buf.push_undo(win.cursors);
            Cursor primary = win.cursors.front();
            if (mode == Mode::VISUAL_BLOCK) {
                int min_y = std::min(win.visual_anchor.y, primary.y);
                int max_y = std::max(win.visual_anchor.y, primary.y);
                int min_x = std::min(win.visual_anchor.x, primary.x);
                int max_x = std::max(win.visual_anchor.x, primary.x);

                for (int y = min_y; y <= max_y; ++y) {
                    if (y >= static_cast<int>(buf.lines.size())) continue;
                    std::string& l = buf.lines[y];
                    if (min_x < static_cast<int>(l.size())) {
                        int count = std::min(max_x - min_x + 1, static_cast<int>(l.size()) - min_x);
                        l.erase(min_x, count);
                    }
                }
                win.cursors = {{min_y, min_x}};
            } else {
                Cursor start = std::min(win.visual_anchor, primary);
                Cursor end = std::max(win.visual_anchor, primary);
                if (start.y == end.y) {
                    int count = std::min(end.x - start.x + 1, static_cast<int>(buf.lines[start.y].size()) - start.x);
                    buf.lines[start.y].erase(start.x, count);
                } else {
                    buf.lines[start.y].erase(start.x);
                    std::string rest = (end.x + 1 < static_cast<int>(buf.lines[end.y].size())) ? buf.lines[end.y].substr(end.x + 1) : "";
                    buf.lines[start.y] += rest;
                    buf.lines.erase(buf.lines.begin() + start.y + 1, buf.lines.begin() + end.y + 1);
                }
                win.cursors = {start};
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            set_info_msg("Block deleted.");
            break;
        }
        case 'y': {
            Cursor primary = win.cursors.front();
            yank_reg.lines.clear();
            yank_reg.text.clear();
            if (mode == Mode::VISUAL_BLOCK) {
                yank_reg.is_linewise = false;
                int min_y = std::min(win.visual_anchor.y, primary.y);
                int max_y = std::max(win.visual_anchor.y, primary.y);
                int min_x = std::min(win.visual_anchor.x, primary.x);
                int max_x = std::max(win.visual_anchor.x, primary.x);
                for (int y = min_y; y <= max_y && y < static_cast<int>(buf.lines.size()); ++y) {
                    std::string& l = buf.lines[y];
                    if (min_x < static_cast<int>(l.size())) {
                        int count = std::min(max_x - min_x + 1, static_cast<int>(l.size()) - min_x);
                        std::string part = l.substr(min_x, count);
                        yank_reg.lines.push_back(part);
                        yank_reg.text += part + "\n";
                    } else {
                        yank_reg.lines.push_back("");
                        yank_reg.text += "\n";
                    }
                }
            } else {
                Cursor start = std::min(win.visual_anchor, primary);
                Cursor end = std::max(win.visual_anchor, primary);
                if (start.y == end.y) {
                    yank_reg.is_linewise = false;
                    std::string& l = buf.lines[start.y];
                    int count = std::min(end.x - start.x + 1, static_cast<int>(l.size()) - start.x);
                    if (count > 0 && start.x < static_cast<int>(l.size())) {
                        yank_reg.text = l.substr(start.x, count);
                        yank_reg.lines = {yank_reg.text};
                    }
                } else {
                    yank_reg.is_linewise = false;
                    for (int y = start.y; y <= end.y && y < static_cast<int>(buf.lines.size()); ++y) {
                        std::string& l = buf.lines[y];
                        if (y == start.y) {
                            std::string part = (start.x < static_cast<int>(l.size())) ? l.substr(start.x) : "";
                            yank_reg.lines.push_back(part);
                            yank_reg.text += part + "\n";
                        } else if (y == end.y) {
                            int count = std::min(end.x + 1, static_cast<int>(l.size()));
                            std::string part = l.substr(0, count);
                            yank_reg.lines.push_back(part);
                            yank_reg.text += part;
                        } else {
                            yank_reg.lines.push_back(l);
                            yank_reg.text += l + "\n";
                        }
                    }
                }
            }
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            set_info_msg("Selection yanked (" + std::to_string(yank_reg.text.size()) + " chars).");
            break;
        }
    }
}

void VimEngine::trigger_cmd_completion() {
    std::string base;
    std::string pfx;

    if (cmd_buffer == "e" || cmd_buffer == "edit") {
        base = cmd_buffer + " ";
        pfx = "";
        cmd_buffer = base;
    } else if (cmd_buffer.rfind("e ", 0) == 0) {
        base = "e ";
        pfx = cmd_buffer.substr(2);
    } else if (cmd_buffer.rfind("edit ", 0) == 0) {
        base = "edit ";
        pfx = cmd_buffer.substr(5);
    } else if (cmd_buffer.rfind("sp ", 0) == 0) {
        base = "sp ";
        pfx = cmd_buffer.substr(3);
    } else if (cmd_buffer.rfind("split ", 0) == 0) {
        base = "split ";
        pfx = cmd_buffer.substr(6);
    } else if (cmd_buffer.rfind("vsp ", 0) == 0) {
        base = "vsp ";
        pfx = cmd_buffer.substr(4);
    } else if (cmd_buffer.rfind("vsplit ", 0) == 0) {
        base = "vsplit ";
        pfx = cmd_buffer.substr(7);
    } else {
        return;
    }

    cmd_completion_base_cmd = base;
    cmd_completion_prefix = pfx;
    cmd_completion_selected_idx = 0;
    cmd_completion_scroll_row = 0;

    update_cmd_completion();

    if (!cmd_completion_candidates.empty()) {
        show_cmd_completion = true;
        update_cmd_completion_preview();
        set_info_msg("Completion: [▲/▼/◀/▶/Tab] 2D Grid  [Enter] Open  [Esc] Cancel");
    } else {
        show_cmd_completion = false;
        set_info_msg("No file matches for '" + pfx + "'");
    }
}

void VimEngine::update_cmd_completion() {
    if (filepicker_all_files.empty()) {
        scan_project_files();
    }

    std::string root = !project_dir.empty() ? project_dir : ".";
    std::string pfx = cmd_completion_prefix;

    cmd_completion_candidates.clear();
    std::unordered_set<std::string> seen;

    auto add = [&](const std::string& path) {
        if (!path.empty() && seen.find(path) == seen.end()) {
            seen.insert(path);
            cmd_completion_candidates.push_back(path);
        }
    };

    // 1. Filesystem directory entries matching prefix
    std::string dir_part;
    std::string file_part;
    size_t last_slash = pfx.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        dir_part = pfx.substr(0, last_slash + 1);
        file_part = pfx.substr(last_slash + 1);
    } else {
        dir_part = "";
        file_part = pfx;
    }

    std::error_code ec;
    fs::path search_path = fs::path(root) / dir_part;
    if (fs::exists(search_path, ec) && fs::is_directory(search_path, ec)) {
        for (const auto& entry : fs::directory_iterator(search_path, fs::directory_options::skip_permission_denied, ec)) {
            std::string fn = entry.path().filename().string();
            if (fn.empty()) continue;
            if (fn.front() == '.' && (file_part.empty() || file_part.front() != '.')) continue;

            bool is_dir = entry.is_directory(ec);
            std::string cand = dir_part + fn + (is_dir ? "/" : "");

            std::string fn_lower = fn;
            std::string fp_lower = file_part;
            std::transform(fn_lower.begin(), fn_lower.end(), fn_lower.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(fp_lower.begin(), fp_lower.end(), fp_lower.begin(), [](unsigned char c) { return std::tolower(c); });

            if (file_part.empty() || fn_lower.rfind(fp_lower, 0) == 0) {
                add(cand);
            }
        }
    }

    // 2. Project files from filepicker_all_files
    std::string pfx_lower = pfx;
    std::transform(pfx_lower.begin(), pfx_lower.end(), pfx_lower.begin(), [](unsigned char c) { return std::tolower(c); });

    for (const auto& file : filepicker_all_files) {
        std::string f_lower = file;
        std::transform(f_lower.begin(), f_lower.end(), f_lower.begin(), [](unsigned char c) { return std::tolower(c); });

        if (pfx.empty()) {
            add(file);
        } else if (f_lower.rfind(pfx_lower, 0) == 0) {
            add(file);
        } else if (f_lower.find(pfx_lower) != std::string::npos) {
            add(file);
        }
    }

    std::sort(cmd_completion_candidates.begin(), cmd_completion_candidates.end(),
              [](const std::string& a, const std::string& b) {
                  bool a_dir = (!a.empty() && a.back() == '/');
                  bool b_dir = (!b.empty() && b.back() == '/');
                  if (a_dir != b_dir) return a_dir > b_dir;
                  return a < b;
              });

    if (cmd_completion_selected_idx >= static_cast<int>(cmd_completion_candidates.size())) {
        cmd_completion_selected_idx = std::max(0, static_cast<int>(cmd_completion_candidates.size()) - 1);
    }
}

void VimEngine::update_cmd_completion_preview() {
    if (cmd_completion_candidates.empty() ||
        cmd_completion_selected_idx < 0 ||
        cmd_completion_selected_idx >= static_cast<int>(cmd_completion_candidates.size())) {
        return;
    }
    cmd_buffer = cmd_completion_base_cmd + cmd_completion_candidates[cmd_completion_selected_idx];
}

void VimEngine::close_cmd_completion() {
    show_cmd_completion = false;
    cmd_completion_candidates.clear();
    cmd_completion_prefix.clear();
    cmd_completion_base_cmd.clear();
    cmd_completion_selected_idx = 0;
    cmd_completion_scroll_row = 0;
}

void VimEngine::handle_cmd_completion_input(const ncinput& ni, uint32_t key) {
    const int NUM_COLS = 5;
    int total = static_cast<int>(cmd_completion_candidates.size());

    if (key == NCKEY_ESC) {
        cmd_buffer = cmd_completion_base_cmd + cmd_completion_prefix;
        close_cmd_completion();
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        if (!cmd_completion_candidates.empty() &&
            cmd_completion_selected_idx >= 0 &&
            cmd_completion_selected_idx < total) {
            std::string cand = cmd_completion_candidates[cmd_completion_selected_idx];
            if (!cand.empty() && cand.back() == '/') {
                cmd_completion_prefix = cand;
                cmd_buffer = cmd_completion_base_cmd + cmd_completion_prefix;
                cmd_completion_selected_idx = 0;
                cmd_completion_scroll_row = 0;
                update_cmd_completion();
                if (!cmd_completion_candidates.empty()) {
                    update_cmd_completion_preview();
                } else {
                    close_cmd_completion();
                }
                return;
            } else {
                std::string to_exec = cmd_completion_base_cmd + cand;
                close_cmd_completion();
                cmd_buffer.clear();
                mode = Mode::NORMAL;
                execute_command(to_exec);
                return;
            }
        }
        close_cmd_completion();
        execute_command(cmd_buffer);
        cmd_buffer.clear();
        mode = Mode::NORMAL;
        return;
    }

    if (key == '\t' || key == NCKEY_TAB) {
        if (total > 0) {
            cmd_completion_selected_idx = (cmd_completion_selected_idx + 1) % total;
            update_cmd_completion_preview();
        }
        return;
    }

    if (ni.shift && (key == '\t' || key == NCKEY_TAB || ni.id == '\t' || ni.id == NCKEY_TAB)) {
        if (total > 0) {
            cmd_completion_selected_idx = (cmd_completion_selected_idx + total - 1) % total;
            update_cmd_completion_preview();
        }
        return;
    }

    if (key == NCKEY_LEFT) {
        if (total > 0) {
            cmd_completion_selected_idx = (cmd_completion_selected_idx + total - 1) % total;
            update_cmd_completion_preview();
        }
        return;
    }

    if (key == NCKEY_RIGHT) {
        if (total > 0) {
            cmd_completion_selected_idx = (cmd_completion_selected_idx + 1) % total;
            update_cmd_completion_preview();
        }
        return;
    }

    if (key == NCKEY_UP) {
        if (total > 0) {
            int new_idx = cmd_completion_selected_idx - NUM_COLS;
            if (new_idx < 0) {
                new_idx = cmd_completion_selected_idx;
                while (new_idx + NUM_COLS < total) {
                    new_idx += NUM_COLS;
                }
            }
            cmd_completion_selected_idx = new_idx;
            update_cmd_completion_preview();
        }
        return;
    }

    if (key == NCKEY_DOWN) {
        if (total > 0) {
            int new_idx = cmd_completion_selected_idx + NUM_COLS;
            if (new_idx >= total) {
                new_idx = cmd_completion_selected_idx % NUM_COLS;
            }
            cmd_completion_selected_idx = new_idx;
            update_cmd_completion_preview();
        }
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        if (cmd_buffer.size() > cmd_completion_base_cmd.size()) {
            cmd_buffer.pop_back();
            cmd_completion_prefix = cmd_buffer.substr(cmd_completion_base_cmd.size());
            cmd_completion_selected_idx = 0;
            cmd_completion_scroll_row = 0;
            update_cmd_completion();
            if (cmd_completion_candidates.empty()) {
                close_cmd_completion();
            }
        } else {
            close_cmd_completion();
            if (!cmd_buffer.empty()) {
                cmd_buffer.pop_back();
            } else {
                mode = Mode::NORMAL;
                set_info_msg("");
            }
        }
        return;
    }

    if (!nckey_synthesized_p(key) && !ni.alt && !ni.ctrl) {
        std::string ch;
        if (ni.utf8[0] != '\0') {
            ch = reinterpret_cast<const char*>(ni.utf8);
        } else if (key >= 32 && key < 127) {
            ch = std::string(1, static_cast<char>(key));
        }
        if (!ch.empty()) {
            cmd_buffer += ch;
            if (cmd_buffer.size() >= cmd_completion_base_cmd.size()) {
                cmd_completion_prefix = cmd_buffer.substr(cmd_completion_base_cmd.size());
            } else {
                cmd_completion_prefix = "";
            }
            cmd_completion_selected_idx = 0;
            cmd_completion_scroll_row = 0;
            update_cmd_completion();
            if (cmd_completion_candidates.empty()) {
                close_cmd_completion();
            }
            return;
        }
    }
}

void VimEngine::handle_command_mode(const ncinput& ni, uint32_t key) {
    AutocompleteState::instance().reset();
    LOGD("handle_command_mode key=%u id=%u utf8=%02x %02x cmd_buffer='%s'",
         key, ni.id, (unsigned)ni.utf8[0], (unsigned)ni.utf8[1], cmd_buffer.c_str());

    if (show_cmd_completion) {
        handle_cmd_completion_input(ni, key);
        return;
    }

    if (key == NCKEY_ESC) {
        mode = Mode::NORMAL;
        cmd_buffer.clear();
        set_info_msg("");
        return;
    }

    if (key == '\t' || key == NCKEY_TAB) {
        trigger_cmd_completion();
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        close_cmd_completion();
        execute_command(cmd_buffer);
        cmd_buffer.clear();
        mode = Mode::NORMAL;
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        if (!cmd_buffer.empty()) {
            cmd_buffer.pop_back();
        } else {
            mode = Mode::NORMAL;
            set_info_msg("");
        }
        return;
    }

    // Ignore synthesized non-printable keys (like arrows/F-keys)
    if (!nckey_synthesized_p(key)) {
        if (ni.utf8[0] != '\0') {
            cmd_buffer += reinterpret_cast<const char*>(ni.utf8);
        } else if (key >= 32 && key < 127) {
            cmd_buffer += static_cast<char>(key);
        }
    }
}

void VimEngine::execute_command(const std::string& cmd_str) {
    close_cmd_completion();
    std::istringstream iss(cmd_str);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty()) return;

    if (CommandRegistry::instance().execute(*this, cmd_str)) {
        return;
    }

    if (cmd == "q") {
        if (active_buf().modified) {
            set_info_msg("E37: No write since last change (add ! to override)");
        } else {
            save_all_positions();
            config.save();
            running = false;
        }
    } else if (cmd == "q!") {
        save_all_positions();
        config.save();
        running = false;
    } else if (cmd == "w") {
        std::string path;
        iss >> path;
        if (active_buf().save_to_file(path)) {
            save_window_position(active_win(), active_buf());
            config.save();
            set_info_msg("\"" + active_buf().name + "\" written");
        } else {
            set_info_msg("E212: Can't open file for writing");
        }
    } else if (cmd == "wq" || cmd == "x") {
        std::string path;
        iss >> path;
        if (active_buf().save_to_file(path)) {
            save_all_positions();
            config.save();
            running = false;
        } else {
            set_info_msg("E212: Can't open file for writing");
        }
    } else if (cmd == "e" || cmd == "edit") {
        std::string raw_arg;
        iss >> raw_arg;
        if (!raw_arg.empty()) {
            std::string path = raw_arg;
            int target_line = -1;
            int target_col = -1;

            if (raw_arg[0] == '+' && raw_arg.size() > 1) {
                size_t sep = raw_arg.find_first_of(":,", 1);
                if (sep != std::string::npos) {
                    try { target_line = std::stoi(raw_arg.substr(1, sep - 1)); } catch (...) {}
                    try { target_col = std::stoi(raw_arg.substr(sep + 1)); } catch (...) {}
                } else {
                    try { target_line = std::stoi(raw_arg.substr(1)); } catch (...) {}
                }
                iss >> path;
            }

            if (!path.empty()) {
                size_t last_colon = path.rfind(':');
                if (last_colon != std::string::npos && last_colon > 0) {
                    std::string part1 = path.substr(last_colon + 1);
                    auto is_num = [](const std::string& s) {
                        if (s.empty()) return false;
                        for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
                        return true;
                    };
                    if (is_num(part1)) {
                        size_t prev_colon = path.rfind(':', last_colon - 1);
                        if (prev_colon != std::string::npos && prev_colon > 0) {
                            std::string part2 = path.substr(prev_colon + 1, last_colon - prev_colon - 1);
                            if (is_num(part2)) {
                                std::string ppath = path.substr(0, prev_colon);
                                std::error_code ec;
                                if (!fs::exists(path, ec) || fs::exists(ppath, ec)) {
                                    path = ppath;
                                    try { target_line = std::stoi(part2); } catch (...) {}
                                    try { target_col = std::stoi(part1); } catch (...) {}
                                }
                            }
                        } else {
                            std::string ppath = path.substr(0, last_colon);
                            std::error_code ec;
                            if (!fs::exists(path, ec) || fs::exists(ppath, ec)) {
                                path = ppath;
                                try { target_line = std::stoi(part1); } catch (...) {}
                            }
                        }
                    }
                }

                save_window_position(active_win(), active_buf());
                size_t found_idx = buffers.size();
                for (size_t i = 0; i < buffers.size(); ++i) {
                    if (buffers[i]->file_path == path || buffers[i]->name == path) {
                        found_idx = i;
                        break;
                    }
                }
                if (found_idx == buffers.size()) {
                    buffers.push_back(TextBuffer::from_file(path));
                    found_idx = buffers.size() - 1;
                }
                active_win().buffer_idx = found_idx;
                restore_window_position(active_win(), active_buf());

                if (target_line > 0) {
                    int ty = std::clamp(target_line - 1, 0, std::max(0, static_cast<int>(active_buf().lines.size()) - 1));
                    int tx = 0;
                    if (target_col > 0 && ty < static_cast<int>(active_buf().lines.size())) {
                        tx = std::clamp(target_col - 1, 0, static_cast<int>(active_buf().lines[ty].size()));
                    }
                    active_win().cursors = {{ty, tx}};
                    active_win().clamp_all_cursors(active_buf(), mode);
                    update_window_scroll(active_win(), active_buf());
                }

                set_info_msg("\"" + active_buf().name + "\" [" + std::to_string(active_buf().lines.size()) + " lines]");
            } else {
                set_info_msg("E471: Argument required");
            }
        } else {
            set_info_msg("E471: Argument required");
        }
    } else if (cmd == "git" || cmd == "gitstatus" || cmd == "gitview" || cmd == "gs") {
        open_git_status();
    } else if (cmd == "sp" || cmd == "split") {
        split_window(SplitType::HORIZONTAL);
    } else if (cmd == "vsp" || cmd == "vsplit") {
        split_window(SplitType::VERTICAL);
    } else if (cmd == "bn" || cmd == "bnext") {
        next_buffer();
    } else if (cmd == "bp" || cmd == "bprev") {
        prev_buffer();
    } else if (cmd == "b") {
        size_t idx;
        if (iss >> idx && idx >= 1 && idx <= buffers.size()) {
            switch_to_buffer(idx - 1);
        } else {
            open_buffer_list();
        }
    } else if (cmd == "ls" || cmd == "buffers") {
        open_buffer_list();
    } else if (cmd == "vg" || cmd == "vimgrep" || cmd == "rg") {
        std::string pattern;
        std::string word;
        while (iss >> word) {
            if (!pattern.empty()) pattern += " ";
            pattern += word;
        }
        if (!pattern.empty()) {
            run_ripgrep(pattern);
        } else {
            // :vg without pattern reuses previous search from rg_search.json
            if (!rg_groups.empty()) {
                show_rg_popup = true;
                set_info_msg("Ripgrep: \"" + rg_query + "\" (" + std::to_string(rg_flattened_matches.size()) + " matches)");
            } else {
                std::string c_word = get_word_under_cursor();
                if (!c_word.empty()) {
                    run_ripgrep(c_word);
                } else {
                    set_info_msg("No previous search results. Usage: :vg <pattern>");
                }
            }
        }
    } else if (cmd == "pwd" || cmd == "proj" || cmd == "project") {
        set_info_msg("Project [" + project_name + "]: " + project_dir);
    } else if (cmd == "cd") {
        std::string dir;
        iss >> dir;
        if (dir.empty()) dir = project_dir;
        std::error_code ec;
        fs::current_path(dir, ec);
        if (!ec) {
            project_dir = detect_project_dir(dir);
            try { project_name = fs::path(project_dir).filename().string(); } catch (...) {}
            if (project_name.empty()) project_name = "fe";
            set_info_msg("Directory changed to: " + dir + " (Project: " + project_name + ")");
        } else {
            set_info_msg("E344: Can't find directory " + dir);
        }
    } else {
        set_info_msg("E492: Not an editor command: " + cmd);
    }
}

void VimEngine::handle_insert_mode(const ncinput& ni, uint32_t key) {
    auto& win = active_win();
    auto& buf = active_buf();
    auto& ac = AutocompleteState::instance();

    if (win.cursors.size() > 1) {
        ac.reset();
    }

    auto update_autocomplete_after_edit = [&]() {
        if (win.cursors.size() != 1) {
            ac.reset();
            return;
        }
        Cursor primary = win.cursors.front();
        if (primary.y < 0 || primary.y >= static_cast<int>(buf.lines.size())) {
            ac.reset();
            return;
        }
        std::string pfx = get_prefix_before_cursor(buf.lines[primary.y], primary.x);
        if (ac.manual && pfx.size() >= 3) {
            auto cands = find_local_buffer_candidates(buf.lines, primary.y, pfx);
            if (!cands.empty()) {
                ac.active = true;
                ac.prefix = pfx;
                ac.candidates = std::move(cands);
                ac.selected_idx = 0;
                set_info_msg("Autocomplete [1/" + std::to_string(ac.candidates.size()) + "]: " +
                             ac.candidates[0] + "  (▲/▼ cycle, ▶ accept)");
            } else {
                ac.reset();
                if (info_msg.rfind("Autocomplete", 0) == 0) set_info_msg("");
            }
        } else if (pfx.size() >= 5) {
            auto cands = find_local_buffer_candidates(buf.lines, primary.y, pfx);
            if (!cands.empty()) {
                ac.active = true;
                ac.manual = false;
                ac.prefix = pfx;
                ac.candidates = std::move(cands);
                ac.selected_idx = 0;
                set_info_msg("Autocomplete [1/" + std::to_string(ac.candidates.size()) + "]: " +
                             ac.candidates[0] + "  (▲/▼ cycle, ▶ accept)");
            } else {
                ac.reset();
                if (info_msg.rfind("Autocomplete", 0) == 0) set_info_msg("");
            }
        } else {
            ac.reset();
            if (info_msg.rfind("Autocomplete", 0) == 0) set_info_msg("");
        }
    };

    // Alt-/ manual trigger (>= 3 chars)
    bool is_alt_slash = ni.alt && (ni.id == '/' || key == '/' || ni.id == '?' || key == '?' ||
                                   (ni.utf8[0] == '/' && ni.utf8[1] == '\0'));
    if (is_alt_slash) {
        Cursor primary = win.cursors.front();
        if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
            std::string pfx = get_prefix_before_cursor(buf.lines[primary.y], primary.x);
            if (pfx.size() >= 3) {
                if (ac.active && ac.prefix == pfx && !ac.candidates.empty()) {
                    ac.selected_idx = (ac.selected_idx + 1) % ac.candidates.size();
                    set_info_msg("Autocomplete [" + std::to_string(ac.selected_idx + 1) + "/" +
                                 std::to_string(ac.candidates.size()) + "]: " +
                                 ac.candidates[ac.selected_idx] + "  (▲/▼ cycle, ▶ accept)");
                } else {
                    auto cands = find_local_buffer_candidates(buf.lines, primary.y, pfx);
                    if (!cands.empty()) {
                        ac.active = true;
                        ac.manual = true;
                        ac.prefix = pfx;
                        ac.candidates = std::move(cands);
                        ac.selected_idx = 0;
                        set_info_msg("Autocomplete [1/" + std::to_string(ac.candidates.size()) + "]: " +
                                     ac.candidates[0] + "  (▲/▼ cycle, ▶ accept)");
                    } else {
                        ac.reset();
                        set_info_msg("Autocomplete: No candidates matching '" + pfx + "'");
                    }
                }
            } else {
                set_info_msg("Autocomplete: Prefix too short (requires >= 3 chars for Alt-/)");
            }
        }
        return;
    }

    // Intercept Up / Down / Right when autocomplete candidate ghost text is active
    if (ac.active && !ac.candidates.empty() && win.cursors.size() == 1) {
        if (key == NCKEY_UP) {
            ac.selected_idx = (ac.selected_idx + ac.candidates.size() - 1) % ac.candidates.size();
            set_info_msg("Autocomplete [" + std::to_string(ac.selected_idx + 1) + "/" +
                         std::to_string(ac.candidates.size()) + "]: " +
                         ac.candidates[ac.selected_idx] + "  (▲/▼ cycle, ▶ accept)");
            return;
        }
        if (key == NCKEY_DOWN) {
            ac.selected_idx = (ac.selected_idx + 1) % ac.candidates.size();
            set_info_msg("Autocomplete [" + std::to_string(ac.selected_idx + 1) + "/" +
                         std::to_string(ac.candidates.size()) + "]: " +
                         ac.candidates[ac.selected_idx] + "  (▲/▼ cycle, ▶ accept)");
            return;
        }
        if (key == NCKEY_RIGHT) {
            std::string cand = ac.get_selected_candidate();
            std::string pfx = ac.prefix;
            if (!cand.empty()) {
                buf.push_undo(win.cursors);
                Cursor& c = win.cursors.front();
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    std::string& line = buf.lines[c.y];
                    if (cand.compare(0, pfx.size(), pfx) == 0) {
                        std::string suffix = cand.substr(pfx.size());
                        line.insert(c.x, suffix);
                        c.x += static_cast<int>(suffix.size());
                    } else {
                        int start = std::max(0, c.x - static_cast<int>(pfx.size()));
                        line.replace(start, pfx.size(), cand);
                        c.x = start + static_cast<int>(cand.size());
                    }
                    buf.modified = true;
                    buf.version++;
                    buf.invalidate_hunks();
                    if (buf.syntax) buf.syntax->update_text(buf.lines);
                    win.clamp_all_cursors(buf, mode);
                    update_window_scroll(win, buf);
                }
                ac.reset();
                set_info_msg("Completed: " + cand);
                return;
            }
        }
    }

    if (ni.alt && (ni.id == 'u' || ni.id == 'U' || key == 'u' || key == 'U')) {
        ac.reset();
        if (buf.undo(win.cursors)) {
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Undo applied [Alt-u]. Undo states left: " + std::to_string(buf.undo_stack.size()));
        } else {
            set_info_msg("Already at oldest change.");
        }
        return;
    }

    if (ni.alt && (ni.id == 'd' || ni.id == 'D' || key == 'd' || key == 'D')) {
        ac.reset();
        buf.push_undo(win.cursors);
        std::set<int> lines_to_delete;
        for (const auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                lines_to_delete.insert(c.y);
            }
        }
        std::vector<int> sorted_lines(lines_to_delete.rbegin(), lines_to_delete.rend());
        for (int y : sorted_lines) {
            if (buf.lines.size() > 1) {
                buf.lines.erase(buf.lines.begin() + y);
            } else {
                buf.lines[0] = "";
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        win.clamp_all_cursors(buf, mode);
        win.deduplicate_cursors();
        update_window_scroll(win, buf);
        set_info_msg("Line deleted [Alt-d].");
        return;
    }

    if (key == NCKEY_LEFT || key == NCKEY_HOME || key == NCKEY_END ||
        key == NCKEY_PGUP || key == NCKEY_PGDOWN || key == NCKEY_UP || key == NCKEY_DOWN) {
        ac.reset();
        if (info_msg.rfind("Autocomplete", 0) == 0) set_info_msg("");
    }

    if (handle_navigation(ni, key)) return;

    if (key == NCKEY_ESC) {
        ac.reset();
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        ac.reset();
        if (info_msg.rfind("Autocomplete", 0) == 0) set_info_msg("");
        std::sort(win.cursors.begin(), win.cursors.end());
        for (int i = static_cast<int>(win.cursors.size()) - 1; i >= 0; --i) {
            int cy = win.cursors[i].y;
            int cx = win.cursors[i].x;
            std::string& cur = buf.lines[cy];
            std::string rest = (cx < static_cast<int>(cur.size())) ? cur.substr(cx) : "";
            cur = cur.substr(0, cx);
            buf.lines.insert(buf.lines.begin() + cy + 1, rest);
            win.cursors[i].y++;
            win.cursors[i].x = 0;
            for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                win.cursors[j].y++;
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.deduplicate_cursors();
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.x > 0 && c.y < static_cast<int>(buf.lines.size())) {
                buf.lines[c.y].erase(c.x - 1, 1);
                c.x--;
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.deduplicate_cursors();
        update_autocomplete_after_edit();
        return;
    }

    if (nckey_synthesized_p(key)) {
        return;
    }

    if (!ni.alt && key != NCKEY_ESC) {
        std::string ins;
        if (ni.utf8[0] != '\0') {
            ins = reinterpret_cast<const char*>(ni.utf8);
        } else if (key >= 32 && key < 127) {
            ins = std::string(1, static_cast<char>(key));
        } else {
            return;
        }

        std::sort(win.cursors.begin(), win.cursors.end());

        std::map<int, std::vector<size_t>> line_cursor_map;
        for (size_t idx = 0; idx < win.cursors.size(); ++idx) {
            line_cursor_map[win.cursors[idx].y].push_back(idx);
        }

        for (auto& pair : line_cursor_map) {
            int line_y = pair.first;
            auto& c_indices = pair.second;
            if (line_y >= static_cast<int>(buf.lines.size())) continue;

            int accumulated_shift = 0;
            for (size_t idx : c_indices) {
                int pos = win.cursors[idx].x + accumulated_shift;
                pos = std::clamp(pos, 0, static_cast<int>(buf.lines[line_y].size()));
                buf.lines[line_y].insert(pos, ins);
                win.cursors[idx].x += ins.size() + accumulated_shift;
                accumulated_shift += ins.size();
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.deduplicate_cursors();
        update_autocomplete_after_edit();
    }
}