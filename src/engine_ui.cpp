#include "engine.hpp"
#include "autocomplete.hpp"
#include "log.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <set>

extern int g_hunk_marker_style;

namespace fs = std::filesystem;

void VimEngine::render_mini_help(unsigned int screen_h, unsigned int screen_w) {
    struct Slot {
        std::string key;
        std::string label;
    };

    std::vector<Slot> row1 = {
        {"F1", "--"},
        {"F2", "Prev Hunk"},
        {"F3", "Next Hunk"},
        {"F4", "Hunk Diff"},
        {"F5", "--"},
        {"F6", "--"}
    };

    std::vector<Slot> row2 = {
        {"F7", "--"},
        {"F8", "--"},
        {"F9", "Settings"},
        {"F10", "--"},
        {"F11", "Recall Ripg"},
        {"F12", "Mini Help"}
    };

    int popup_h = 4;
    int popup_w = std::min(static_cast<int>(screen_w) - 2, 98);
    int popup_x = std::max(0, (static_cast<int>(screen_w) - popup_w) / 2);
    int popup_y = std::max(0, static_cast<int>(screen_h) - 2 - popup_h);

    // Background
    ncplane_set_bg_rgb8(stdplane, 24, 26, 30);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border: cyan/teal rounded box
    ncplane_set_fg_rgb8(stdplane, 100, 185, 195);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }

    // Header Title
    std::string title = " Help ";
    ncplane_set_fg_rgb8(stdplane, 160, 220, 230);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    auto draw_slots_row = [&](int draw_y, const std::vector<Slot>& slots) {
        int avail_w = popup_w - 2;
        int col_w = avail_w / static_cast<int>(slots.size());

        for (size_t i = 0; i < slots.size(); ++i) {
            int sx = popup_x + 1 + static_cast<int>(i) * col_w + 1;
            if (sx + static_cast<int>(slots[i].key.size()) + 5 >= popup_x + popup_w) break;

            // '[' in gold
            ncplane_set_fg_rgb8(stdplane, 230, 190, 70);
            ncplane_putstr_yx(stdplane, draw_y, sx, "[");

            // 'F...' in green
            ncplane_set_fg_rgb8(stdplane, 110, 205, 120);
            ncplane_putstr_yx(stdplane, draw_y, sx + 1, slots[i].key.c_str());

            // ']' in gold
            int b_end_x = sx + 1 + static_cast<int>(slots[i].key.size());
            ncplane_set_fg_rgb8(stdplane, 230, 190, 70);
            ncplane_putstr_yx(stdplane, draw_y, b_end_x, "] ");

            // label in white/cream
            if (slots[i].label == "--") {
                ncplane_set_fg_rgb8(stdplane, 120, 125, 135);
            } else {
                ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
            }
            int max_lbl_w = col_w - static_cast<int>(slots[i].key.size()) - 4;
            std::string lbl = slots[i].label;
            if (static_cast<int>(lbl.size()) > max_lbl_w && max_lbl_w > 0) {
                lbl = lbl.substr(0, max_lbl_w);
            }
            ncplane_putstr_yx(stdplane, draw_y, b_end_x + 2, lbl.c_str());
        }
    };

    draw_slots_row(popup_y + 1, row1);
    draw_slots_row(popup_y + 2, row2);
}

void VimEngine::handle_whichkey_popup(const ncinput& ni, uint32_t key) {
    auto& win = active_win();
    auto& buf = active_buf();

    leader_pending = false;
    show_whichkey_popup = false;

    if (key == NCKEY_ESC || key == ' ') {
        set_info_msg("");
        return;
    }

    switch (key) {
        case 'f':
            open_filepicker();
            break;
        case 'g': {
            std::string word = get_word_under_cursor();
            if (!word.empty()) {
                run_ripgrep(word);
            } else if (!rg_groups.empty()) {
                show_rg_popup = true;
            } else {
                set_info_msg("No word under cursor. Use :vg <pattern>");
            }
            break;
        }
        case 'w':
            if (buf.save_to_file("")) {
                save_window_position(win, buf);
                config.save();
                set_info_msg("\"" + buf.name + "\" written");
            } else {
                set_info_msg("E212: Can't open file for writing");
            }
            break;
        case 'q':
            if (buf.modified) {
                set_info_msg("E37: No write since last change (use :q! to override)");
            } else {
                save_all_positions();
                config.save();
                running = false;
            }
            break;
        case 'x':
            if (buf.save_to_file("")) {
                save_all_positions();
                config.save();
                running = false;
            } else {
                set_info_msg("E212: Can't open file for writing");
            }
            break;
        case 's':
            split_window(SplitType::HORIZONTAL);
            break;
        case 'v':
            split_window(SplitType::VERTICAL);
            break;
        case 'c':
            close_active_window();
            break;
        case 'b':
            save_window_position(win, buf);
            win.buffer_idx = (win.buffer_idx + 1) % buffers.size();
            restore_window_position(win, active_buf());
            set_info_msg("Switched to Buffer [" + active_buf().name + "]");
            break;
        case 'B':
            save_window_position(win, buf);
            win.buffer_idx = (win.buffer_idx + buffers.size() - 1) % buffers.size();
            restore_window_position(win, active_buf());
            set_info_msg("Switched to Buffer [" + active_buf().name + "]");
            break;
        case 'h':
            open_git_hunk_popup();
            break;
        case 'j':
            jump_to_next_hunk();
            break;
        case 'k':
            jump_to_prev_hunk();
            break;
        case 'l':
            show_settings_popup = true;
            break;
        case 'u':
            if (buf.undo(win.cursors)) {
                win.clamp_all_cursors(buf, mode);
                update_window_scroll(win, buf);
                set_info_msg("Undo applied. Undo states left: " + std::to_string(buf.undo_stack.size()));
            } else {
                set_info_msg("Already at oldest change.");
            }
            break;
        default:
            if (key >= 32 && key < 127) {
                set_info_msg("WhichKey: Unmapped shortcut [" + std::string(1, static_cast<char>(key)) + "]");
            }
            break;
    }
}

void VimEngine::render_whichkey_popup(unsigned int screen_h, unsigned int screen_w) {
    struct WkItem {
        std::string key;
        std::string desc;
    };

    std::vector<WkItem> col1 = {
        {"f", "File Picker"},
        {"g", "Grep Cursor"},
        {"w", "Save Buffer"},
        {"s", "Split Horiz"},
        {"v", "Split Vert"},
        {"b", "Next Buffer"},
        {"u", "Undo"}
    };

    std::vector<WkItem> col2 = {
        {"h", "Hunk Diff"},
        {"j", "Next Hunk"},
        {"k", "Prev Hunk"},
        {"l", "Gutter Settings"},
        {"q", "Quit"},
        {"x", "Save & Quit"}
    };

    int popup_w = 44;
    int popup_h = 9;

    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);
    popup_h = std::min(popup_h, static_cast<int>(screen_h) - 3);

    // Place popup in the right-bottom corner
    int popup_x = static_cast<int>(screen_w) - popup_w - 1;
    int popup_y = static_cast<int>(screen_h) - 2 - popup_h;

    popup_x = std::max(0, popup_x);
    popup_y = std::max(0, popup_y);

    // Background fill
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox) in vibrant violet
    ncplane_set_fg_rgb8(stdplane, 170, 115, 250);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }

    // Header title
    std::string title = " Leader [Space] ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    int c1_x = popup_x + 2;
    int c2_x = popup_x + (popup_w / 2) + 1;

    for (size_t i = 0; i < 6 && (i + 1) < static_cast<size_t>(popup_h - 1); ++i) {
        int draw_y = popup_y + 1 + static_cast<int>(i);

        // Column 1
        if (i < col1.size()) {
            ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c1_x, "[");
            ncplane_set_fg_rgb8(stdplane, 255, 200, 70);
            ncplane_putstr_yx(stdplane, draw_y, c1_x + 1, col1[i].key.c_str());
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c1_x + 2, "] ");
            ncplane_set_fg_rgb8(stdplane, 220, 225, 235);
            ncplane_putstr_yx(stdplane, draw_y, c1_x + 4, col1[i].desc.c_str());
        }

        // Column 2
        if (i < col2.size() && c2_x < popup_x + popup_w - 5) {
            ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c2_x, "[");
            ncplane_set_fg_rgb8(stdplane, 255, 200, 70);
            ncplane_putstr_yx(stdplane, draw_y, c2_x + 1, col2[i].key.c_str());
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c2_x + 2, "] ");
            ncplane_set_fg_rgb8(stdplane, 220, 225, 235);
            ncplane_putstr_yx(stdplane, draw_y, c2_x + 4, col2[i].desc.c_str());
        }
    }

    // Footer hint on bottom border
    std::string footer = " [Esc] Close ";
    ncplane_set_fg_rgb8(stdplane, 140, 145, 160);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - static_cast<int>(footer.size()) - 2, footer.c_str());
}

void VimEngine::render_git_hunk_popup(unsigned int screen_h, unsigned int screen_w) {
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();
    if (active_hunk_idx < 0 || active_hunk_idx >= static_cast<int>(hunks.size())) {
        show_git_hunk_popup = false;
        return;
    }

    const auto& hunk = hunks[active_hunk_idx];

    // Compute diff lines to display
    std::vector<std::pair<char, std::string>> diff_entries;
    for (const auto& l : hunk.orig_lines) {
        diff_entries.push_back({'-', l});
    }
    for (const auto& l : hunk.cur_lines) {
        diff_entries.push_back({'+', l});
    }

    int popup_w = std::max(48, static_cast<int>(screen_w * 0.70));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);

    int diff_lines_avail = std::min(static_cast<int>(diff_entries.size()), 12);
    int popup_h = std::max(7, diff_lines_avail + 5);
    popup_h = std::min(popup_h, static_cast<int>(screen_h) - 2);

    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    // Background
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox)
    ncplane_set_fg_rgb8(stdplane, 200, 100, 255);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }

    // Title
    std::string type_str = (hunk.type == HunkType::ADDED) ? "ADDED" :
                           ((hunk.type == HunkType::MODIFIED) ? "MODIFIED" : "DELETED");
    std::string title = " Git Hunk (" + std::to_string(active_hunk_idx + 1) + "/" +
                        std::to_string(hunks.size()) + ") [" + type_str + "] ";
    if (static_cast<int>(title.size()) < popup_w - 4) {
        ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());
    }

    // Unified diff header @@ -orig,len +cur,len @@
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "@@ -%d,%d +%d,%d @@",
             hunk.orig_start + 1, std::max(1, hunk.orig_count),
             hunk.cur_start + 1, std::max(1, hunk.cur_count));
    ncplane_set_fg_rgb8(stdplane, 80, 200, 240);
    ncplane_set_bg_rgb8(stdplane, 28, 30, 40);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, hdr);

    // Diff lines
    int max_draw_lines = popup_h - 4;
    for (int i = 0; i < max_draw_lines && i < static_cast<int>(diff_entries.size()); ++i) {
        int draw_y = popup_y + 2 + i;
        char sign = diff_entries[i].first;
        const std::string& text = diff_entries[i].second;

        if (sign == '-') {
            ncplane_set_fg_rgb8(stdplane, 255, 110, 110);
            ncplane_set_bg_rgb8(stdplane, 50, 20, 25);
        } else {
            ncplane_set_fg_rgb8(stdplane, 110, 240, 140);
            ncplane_set_bg_rgb8(stdplane, 20, 48, 28);
        }

        std::string line_disp = std::string(1, sign) + " " + text;
        int max_len = popup_w - 4;
        if (static_cast<int>(line_disp.size()) > max_len) {
            line_disp = line_disp.substr(0, max_len);
        } else {
            line_disp += std::string(max_len - line_disp.size(), ' ');
        }
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, line_disp.c_str());
    }

    // Footer actions
    ncplane_set_fg_rgb8(stdplane, 255, 230, 100);
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    std::string footer = " [r] Revert Hunk   [F2/F3] Prev/Next   [Esc/F4/q] Close ";
    if (static_cast<int>(footer.size()) < popup_w - 2) {
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 2, footer.c_str());
    }
}

void VimEngine::render_window(Window& win, bool is_active) {
    auto& buf = *buffers[win.buffer_idx];
    update_window_scroll(win, buf);

    std::set<std::pair<int, int>> cursor_set;
    for (const auto& c : win.cursors) {
        cursor_set.insert({c.y, c.x});
    }

    Cursor primary = win.cursors.empty() ? Cursor{0, 0} : win.cursors.front();
    int v_min_y = std::min(win.visual_anchor.y, primary.y);
    int v_max_y = std::max(win.visual_anchor.y, primary.y);
    int v_min_x = std::min(win.visual_anchor.x, primary.x);
    int v_max_x = std::max(win.visual_anchor.x, primary.x);

    int gutter_w = get_line_num_w(buf);

    for (int r = 0; r < win.h; ++r) {
        int line_idx = win.scroll_y + r;
        int draw_y = win.y + r;

        if (gutter_w > 0) {
            bool is_cursor_line = (line_idx == primary.y);
            if (is_cursor_line && config.settings.highlight_current_line && is_active) {
                ncplane_set_bg_rgb8(stdplane, 35, 38, 48);
            } else {
                ncplane_set_bg_rgb8(stdplane, 22, 22, 26);
            }

            // Determine git hunk gutter sign (~, +, -)
            char git_sign = ' ';
            if (buf.is_git_repo && line_idx < static_cast<int>(buf.lines.size())) {
                const auto& hunks = buf.get_hunks();
                for (const auto& h : hunks) {
                    if (h.type == HunkType::ADDED) {
                        if (line_idx >= h.cur_start && line_idx < h.cur_start + h.cur_count) {
                            git_sign = '+';
                            break;
                        }
                    } else if (h.type == HunkType::MODIFIED) {
                        if (line_idx >= h.cur_start && line_idx < h.cur_start + h.cur_count) {
                            git_sign = '~';
                            break;
                        }
                    } else if (h.type == HunkType::DELETED) {
                        int del_pos = std::min(h.cur_start, static_cast<int>(buf.lines.size()) - 1);
                        if (line_idx == del_pos) {
                            git_sign = '-';
                            break;
                        }
                    }
                }
            }

            if (config.settings.show_line_numbers) {
                if (line_idx < static_cast<int>(buf.lines.size())) {
                    if (is_cursor_line && config.settings.highlight_current_line && is_active) {
                        ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
                    } else if (is_active) {
                        ncplane_set_fg_rgb8(stdplane, 80, 160, 200);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    }

                    int disp_num = line_idx + 1;
                    if (config.settings.line_number_mode == LineNumberMode::RELATIVE) {
                        disp_num = std::abs(line_idx - primary.y);
                    } else if (config.settings.line_number_mode == LineNumberMode::HYBRID) {
                        disp_num = is_cursor_line ? (line_idx + 1) : std::abs(line_idx - primary.y);
                    }
                    std::string num_fmt = "%" + std::to_string(gutter_w - 2) + "d";
                    ncplane_printf_yx(stdplane, draw_y, win.x, num_fmt.c_str(), disp_num);

                    // Print git sign column
                    if (git_sign == '+') {
                        ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                    } else if (git_sign == '~') {
                        ncplane_set_fg_rgb8(stdplane, 80, 180, 240);
                    } else if (git_sign == '-') {
                        ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    }
                    char disp_sign = (g_hunk_marker_style == 1 && git_sign != ' ') ? '|' : git_sign;
                    char sign_buf[3] = {disp_sign, ' ', '\0'};
                    ncplane_putstr_yx(stdplane, draw_y, win.x + gutter_w - 2, sign_buf);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    std::string tilde;
                    for (int sp = 0; sp < std::max(0, gutter_w - 2); ++sp) tilde += " ";
                    tilde += "~ ";
                    ncplane_putstr_yx(stdplane, draw_y, win.x, tilde.c_str());
                }
            } else {
                if (git_sign == '+') {
                    ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                } else if (git_sign == '~') {
                    ncplane_set_fg_rgb8(stdplane, 80, 180, 240);
                } else if (git_sign == '-') {
                    ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                }
                char disp_sign = (g_hunk_marker_style == 1 && git_sign != ' ') ? '|' : git_sign;
                char sign_buf[3] = {disp_sign, ' ', '\0'};
                ncplane_putstr_yx(stdplane, draw_y, win.x, sign_buf);
            }
        }

        int text_avail_w = win.w - gutter_w;
        if (text_avail_w <= 0) continue;

        if (line_idx < static_cast<int>(buf.lines.size())) {
            const std::string& line = buf.lines[line_idx];
            std::vector<SyntaxStyle> syn_styles;
            if (buf.syntax) {
                syn_styles = buf.syntax->get_line_styles(line_idx, line);
            }

            std::string ghost_str;
            if (is_active && mode == Mode::INSERT && line_idx == primary.y && win.cursors.size() == 1) {
                ghost_str = AutocompleteState::instance().get_ghost_suffix();
            }
            int ghost_len = static_cast<int>(ghost_str.size());

            for (int c = 0; c < text_avail_w; ++c) {
                int char_idx = win.scroll_x + c;
                int draw_x = win.x + gutter_w + c;

                if (ghost_len > 0) {
                    if (char_idx >= primary.x && char_idx < primary.x + ghost_len) {
                        int ghost_i = char_idx - primary.x;
                        bool has_cursor = (char_idx == primary.x);
                        if (has_cursor) {
                            ncplane_set_fg_rgb8(stdplane, 0, 0, 0);
                            ncplane_set_bg_rgb8(stdplane, 255, 180, 50);
                            char ch[2] = {ghost_str[ghost_i], '\0'};
                            ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                        } else {
                            ncplane_set_fg_rgb8(stdplane, 130, 140, 155);
                            ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
                            ncplane_on_styles(stdplane, NCSTYLE_ITALIC);
                            char ch[2] = {ghost_str[ghost_i], '\0'};
                            ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                            ncplane_off_styles(stdplane, NCSTYLE_ITALIC);
                        }
                        continue;
                    }

                    int orig_char_idx = (char_idx < primary.x) ? char_idx : (char_idx - ghost_len);
                    if (orig_char_idx < static_cast<int>(syn_styles.size())) {
                        ncplane_set_fg_rgb8(stdplane, syn_styles[orig_char_idx].r, syn_styles[orig_char_idx].g, syn_styles[orig_char_idx].b);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
                    }
                    ncplane_set_bg_rgb8(stdplane, 16, 16, 18);

                    if (orig_char_idx >= 0 && orig_char_idx < static_cast<int>(line.size())) {
                        char ch[2] = {line[orig_char_idx], '\0'};
                        ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                    } else {
                        ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                    }
                    continue;
                }

                bool has_cursor = cursor_set.count({line_idx, char_idx});
                bool in_visual = false;

                if (is_active) {
                    if (mode == Mode::VISUAL_BLOCK) {
                        if (line_idx >= v_min_y && line_idx <= v_max_y &&
                            char_idx >= v_min_x && char_idx <= v_max_x) {
                            in_visual = true;
                        }
                    } else if (mode == Mode::VISUAL) {
                        Cursor cur_pt{line_idx, char_idx};
                        Cursor v_start = std::min(win.visual_anchor, primary);
                        Cursor v_end = std::max(win.visual_anchor, primary);
                        if (!(cur_pt < v_start) && !(v_end < cur_pt)) {
                            in_visual = true;
                        }
                    }
                }

                if (has_cursor) {
                    ncplane_set_fg_rgb8(stdplane, 0, 0, 0);
                    ncplane_set_bg_rgb8(stdplane, 255, 180, 50);
                } else if (in_visual) {
                    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
                    ncplane_set_bg_rgb8(stdplane, 55, 75, 135);
                } else {
                    if (char_idx < static_cast<int>(syn_styles.size())) {
                        ncplane_set_fg_rgb8(stdplane, syn_styles[char_idx].r, syn_styles[char_idx].g, syn_styles[char_idx].b);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
                    }
                    ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                }

                if (char_idx < static_cast<int>(line.size())) {
                    char ch[2] = {line[char_idx], '\0'};
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                } else if ((has_cursor || in_visual) && char_idx == static_cast<int>(line.size())) {
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                } else {
                    ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                }
            }
        } else {
            ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
            std::string empty(text_avail_w, ' ');
            ncplane_putstr_yx(stdplane, draw_y, win.x + gutter_w, empty.c_str());
        }
    }
}

void VimEngine::handle_settings_popup(const ncinput& ni, uint32_t key) {
    if (key == NCKEY_ESC || key == NCKEY_F09 || (ni.id == NCKEY_F09) || key == 'q' || key == 'Q') {
        show_settings_popup = false;
        config.save();
        set_info_msg("Settings applied.");
        return;
    }

    const int total_items = 5;
    if (key == NCKEY_UP || key == 'k' || key == 'K') {
        settings_selected_idx = (settings_selected_idx + total_items - 1) % total_items;
    } else if (key == NCKEY_DOWN || key == 'j' || key == 'J') {
        settings_selected_idx = (settings_selected_idx + 1) % total_items;
    } else if (key == NCKEY_ENTER || key == '\n' || key == '\r' || key == ' ' ||
               key == NCKEY_LEFT || key == NCKEY_RIGHT || key == 'h' || key == 'l') {
        switch (settings_selected_idx) {
            case 0:
                config.settings.show_line_numbers = !config.settings.show_line_numbers;
                break;
            case 1: {
                int m = static_cast<int>(config.settings.line_number_mode);
                if (key == NCKEY_LEFT || key == 'h') {
                    m = (m + 2) % 3;
                } else {
                    m = (m + 1) % 3;
                }
                config.settings.line_number_mode = static_cast<LineNumberMode>(m);
                break;
            }
            case 2: {
                if (key == NCKEY_LEFT || key == 'h') {
                    if (config.settings.line_number_width == 0) config.settings.line_number_width = 8;
                    else if (config.settings.line_number_width <= 3) config.settings.line_number_width = 0;
                    else config.settings.line_number_width--;
                } else {
                    if (config.settings.line_number_width == 0) config.settings.line_number_width = 3;
                    else if (config.settings.line_number_width >= 8) config.settings.line_number_width = 0;
                    else config.settings.line_number_width++;
                }
                break;
            }
            case 3:
                config.settings.highlight_current_line = !config.settings.highlight_current_line;
                break;
            case 4:
                g_hunk_marker_style = (g_hunk_marker_style + 1) % 2;
                break;
        }
        config.save();
    }
}

void VimEngine::render_settings_popup(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(36, static_cast<int>(screen_w * 0.50));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);
    int popup_h = 11;
    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    // Draw panel background
    ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Draw roundbox border
    ncplane_set_fg_rgb8(stdplane, 90, 180, 240);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }

    // Header Title
    std::string title = " Line Number Hunk Settings (F9) ";
    if (static_cast<int>(title.size()) < popup_w - 4) {
        ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());
    }

    // Setting items
    struct Item {
        std::string label;
        std::string value;
    };

    std::string mode_str = "Absolute";
    if (config.settings.line_number_mode == LineNumberMode::RELATIVE) mode_str = "Relative";
    else if (config.settings.line_number_mode == LineNumberMode::HYBRID) mode_str = "Hybrid";

    std::string w_str = (config.settings.line_number_width == 0) ? "Auto" : std::to_string(config.settings.line_number_width);
    std::string hunk_style_str = (g_hunk_marker_style == 1) ? "< | >" : "< ~-= >";

    std::vector<Item> items = {
        {"Show Line Numbers", config.settings.show_line_numbers ? "[ ON ]" : "[ OFF ]"},
        {"Line Number Style", "< " + mode_str + " >"},
        {"Hunk/Gutter Width", "< " + w_str + " >"},
        {"Highlight Active", config.settings.highlight_current_line ? "[ ON ]" : "[ OFF ]"},
        {"Hunk Gutter Style", hunk_style_str}
    };

    for (size_t i = 0; i < items.size(); ++i) {
        int draw_y = popup_y + 2 + static_cast<int>(i);
        bool is_sel = (static_cast<int>(i) == settings_selected_idx);

        if (is_sel) {
            ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            ncplane_set_bg_rgb8(stdplane, 50, 75, 140);
        } else {
            ncplane_set_fg_rgb8(stdplane, 210, 210, 210);
            ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
        }

        std::string pointer = is_sel ? " ▶ " : "   ";
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, pointer.c_str());
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 5, items[i].label.c_str());

        int val_x = popup_x + popup_w - 2 - static_cast<int>(items[i].value.size());
        if (val_x > popup_x + 24) {
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 220, 80);
            } else {
                ncplane_set_fg_rgb8(stdplane, 130, 200, 255);
            }
            ncplane_putstr_yx(stdplane, draw_y, val_x, items[i].value.c_str());
        }
    }

    // Bottom help tip
    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
    std::string help = "[▲/▼] Navigate  [Enter/Space/◀/▶] Toggle  [Esc/F9] Close";
    if (static_cast<int>(help.size()) < popup_w - 4) {
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 2, popup_x + 2, help.c_str());
    }
}

void VimEngine::render_status_bar(int y, unsigned int screen_w) {
    auto& win = active_win();
    auto& buf = active_buf();
    Cursor primary = win.cursors.empty() ? Cursor{0, 0} : win.cursors.front();

    // Clear entire status line background
    ncplane_set_bg_rgb8(stdplane, 22, 24, 30);
    for (unsigned int c = 0; c < screen_w; ++c) {
        ncplane_putchar_yx(stdplane, y, c, ' ');
    }

    int cur_x = 0;

    // --- Left Segment 1: [mode] ---
    std::string mode_str = " NORMAL ";
    uint8_t m_r = 80, m_g = 210, m_b = 120;
    switch (mode) {
        case Mode::NORMAL:
            mode_str = " NORMAL ";
            m_r = 80; m_g = 210; m_b = 120;
            break;
        case Mode::INSERT:
            mode_str = " INSERT ";
            m_r = 80; m_g = 170; m_b = 255;
            break;
        case Mode::VISUAL:
            mode_str = " VISUAL ";
            m_r = 230; m_g = 140; m_b = 60;
            break;
        case Mode::VISUAL_BLOCK:
            mode_str = " V-BLOCK ";
            m_r = 200; m_g = 100; m_b = 255;
            break;
        case Mode::COMMAND:
            mode_str = " COMMAND ";
            m_r = 240; m_g = 200; m_b = 80;
            break;
    }
    ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
    ncplane_set_bg_rgb8(stdplane, m_r, m_g, m_b);
    ncplane_putstr_yx(stdplane, y, cur_x, mode_str.c_str());
    cur_x += static_cast<int>(mode_str.size());

    // --- Left Segment 2: [branch name +0 ~0 -0] ---
    if (buf.is_git_repo && cur_x < static_cast<int>(screen_w)) {
        int add_cnt = 0, mod_cnt = 0, del_cnt = 0;
        for (const auto& h : buf.get_hunks()) {
            if (h.type == HunkType::ADDED) add_cnt += h.cur_count;
            else if (h.type == HunkType::MODIFIED) mod_cnt += h.cur_count;
            else if (h.type == HunkType::DELETED) del_cnt += h.orig_count;
        }

        std::string branch_name = !buf.git_branch.empty() ? buf.git_branch : "git";
        std::string git_seg = "  " + branch_name + " +" + std::to_string(add_cnt) +
                              " ~" + std::to_string(mod_cnt) + " -" + std::to_string(del_cnt) + " ";

        ncplane_set_fg_rgb8(stdplane, 225, 230, 240);
        ncplane_set_bg_rgb8(stdplane, 45, 52, 68);
        ncplane_putstr_yx(stdplane, y, cur_x, git_seg.c_str());
        cur_x += static_cast<int>(git_seg.size()) - 2;

    }

    // --- Left Segment 3: [filename] ---
    if (cur_x < static_cast<int>(screen_w)) {
        std::string display_name = buf.name;
        if (!buf.file_path.empty() && !project_dir.empty()) {
            try {
                std::error_code ec;
                fs::path p = fs::absolute(buf.file_path, ec);
                fs::path root = fs::absolute(project_dir, ec);
                auto rel = fs::relative(p, root, ec);
                if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0) {
                    display_name = rel.string();
                }
            } catch (...) {}
        }

        std::string file_seg = " " + display_name + (buf.modified ? " [+] " : " ");
        ncplane_set_fg_rgb8(stdplane, 210, 215, 225);
        ncplane_set_bg_rgb8(stdplane, 32, 36, 46);
        ncplane_putstr_yx(stdplane, y, cur_x, file_seg.c_str());
        cur_x += static_cast<int>(file_seg.size());
    }

    // --- Right Segments: [filetype][row:col][buffer 1/2] ---
    std::string ft = (buf.syntax && !buf.syntax->get_language().empty()) ? buf.syntax->get_language() : "text";
    std::string ft_seg = " " + ft + " ";
    std::string pos_seg = " " + std::to_string(primary.y + 1) + ":" + std::to_string(primary.x + 1) + " ";
    std::string buf_seg = " " + std::to_string(win.buffer_idx + 1) + "/" + std::to_string(buffers.size()) + " ";

    int right_total_w = static_cast<int>(ft_seg.size() + pos_seg.size() + buf_seg.size());
    int rx = std::max(cur_x, static_cast<int>(screen_w) - right_total_w);

    if (rx < static_cast<int>(screen_w)) {
        // [filetype]
        ncplane_set_fg_rgb8(stdplane, 130, 200, 255);
        ncplane_set_bg_rgb8(stdplane, 40, 45, 58);
        ncplane_putstr_yx(stdplane, y, rx, ft_seg.c_str());
        rx += static_cast<int>(ft_seg.size());

        // [row:col]
        ncplane_set_fg_rgb8(stdplane, 240, 245, 255);
        ncplane_set_bg_rgb8(stdplane, 52, 60, 75);
        ncplane_putstr_yx(stdplane, y, rx, pos_seg.c_str());
        rx += static_cast<int>(pos_seg.size());

        // [buffer 1/2]
        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        ncplane_set_bg_rgb8(stdplane, 68, 78, 98);
        ncplane_putstr_yx(stdplane, y, rx, buf_seg.c_str());
    }
}

void VimEngine::render_info_bar(int y, unsigned int screen_w) {
    // Clear line background cleanly without overflowing the terminal corner
    ncplane_set_bg_rgb8(stdplane, 20, 20, 24);
    for (unsigned int x = 0; x < screen_w; ++x) {
        ncplane_putchar_yx(stdplane, y, x, ' ');
    }

    if (mode == Mode::COMMAND) {
        // Distinct bright yellow ':' prompt
        ncplane_set_fg_rgb8(stdplane, 255, 230, 80);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);
        ncplane_putstr_yx(stdplane, y, 0, ":");

        // Command text typed by user
        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        if (!cmd_buffer.empty()) {
            ncplane_putstr_yx(stdplane, y, 1, cmd_buffer.c_str());
        }
    } else {
        ncplane_set_fg_rgb8(stdplane, 240, 200, 100);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);

        if (!info_msg.empty()) {
            std::string bar = " " + info_msg;
            if (static_cast<int>(bar.size()) >= static_cast<int>(screen_w)) {
                bar = bar.substr(0, screen_w - 1);
            }
            ncplane_putstr_yx(stdplane, y, 0, bar.c_str());
        }
    }
}