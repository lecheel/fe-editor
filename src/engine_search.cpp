#include "engine.hpp"
#include "log.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <set>
#include <map>
#include <cstdio>

#ifndef NCKEY_F02
#define NCKEY_F02 (NCKEY_F01 + 1)
#endif
#ifndef NCKEY_F03
#define NCKEY_F03 (NCKEY_F01 + 2)
#endif
#ifndef NCKEY_F04
#define NCKEY_F04 (NCKEY_F01 + 3)
#endif
#ifndef NCKEY_F05
#define NCKEY_F05 (NCKEY_F01 + 4)
#endif

namespace fs = std::filesystem;

void VimEngine::jump_to_prev_hunk() {
    auto& win = active_win();
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();
    if (hunks.empty()) {
        set_info_msg("Git: No hunks found.");
        return;
    }
    Cursor primary = win.cursors.front();
    int target_idx = -1;
    for (int i = static_cast<int>(hunks.size()) - 1; i >= 0; --i) {
        if (hunks[i].cur_start < primary.y) {
            target_idx = i;
            break;
        }
    }
    if (target_idx == -1) {
        target_idx = static_cast<int>(hunks.size()) - 1;
    }
    win.cursors = {{hunks[target_idx].cur_start, 0}};
    win.clamp_all_cursors(buf, mode);
    update_window_scroll(win, buf);
    set_info_msg("Git: Hunk " + std::to_string(target_idx + 1) + "/" + std::to_string(hunks.size()) +
                 " (Line " + std::to_string(hunks[target_idx].cur_start + 1) + ")");
}

void VimEngine::jump_to_next_hunk() {
    auto& win = active_win();
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();
    if (hunks.empty()) {
        set_info_msg("Git: No hunks found.");
        return;
    }
    Cursor primary = win.cursors.front();
    int target_idx = -1;
    for (size_t i = 0; i < hunks.size(); ++i) {
        if (hunks[i].cur_start > primary.y) {
            target_idx = static_cast<int>(i);
            break;
        }
    }
    if (target_idx == -1) {
        target_idx = 0;
    }
    win.cursors = {{hunks[target_idx].cur_start, 0}};
    win.clamp_all_cursors(buf, mode);
    update_window_scroll(win, buf);
    set_info_msg("Git: Hunk " + std::to_string(target_idx + 1) + "/" + std::to_string(hunks.size()) +
                 " (Line " + std::to_string(hunks[target_idx].cur_start + 1) + ")");
}

void VimEngine::open_git_hunk_popup() {
    auto& win = active_win();
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();
    if (hunks.empty()) {
        set_info_msg("Git: No hunks in current buffer.");
        return;
    }

    Cursor primary = win.cursors.front();
    int best_idx = 0;
    int min_dist = 999999;
    for (size_t i = 0; i < hunks.size(); ++i) {
        int h_start = hunks[i].cur_start;
        int h_end = hunks[i].cur_start + std::max(1, hunks[i].cur_count) - 1;
        if (primary.y >= h_start && primary.y <= h_end) {
            best_idx = static_cast<int>(i);
            min_dist = 0;
            break;
        }
        int dist = std::min(std::abs(primary.y - h_start), std::abs(primary.y - h_end));
        if (dist < min_dist) {
            min_dist = dist;
            best_idx = static_cast<int>(i);
        }
    }
    active_hunk_idx = best_idx;
    show_git_hunk_popup = true;
}

void VimEngine::revert_active_hunk() {
    auto& win = active_win();
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();
    if (active_hunk_idx < 0 || active_hunk_idx >= static_cast<int>(hunks.size())) return;

    const auto& hunk = hunks[active_hunk_idx];
    buf.push_undo(win.cursors);

    int start = hunk.cur_start;
    int count = hunk.cur_count;

    if (start < static_cast<int>(buf.lines.size())) {
        int erase_count = std::min(count, static_cast<int>(buf.lines.size()) - start);
        buf.lines.erase(buf.lines.begin() + start, buf.lines.begin() + start + erase_count);
    }
    if (!hunk.orig_lines.empty()) {
        int insert_pos = std::min(start, static_cast<int>(buf.lines.size()));
        buf.lines.insert(buf.lines.begin() + insert_pos, hunk.orig_lines.begin(), hunk.orig_lines.end());
    }
    if (buf.lines.empty()) {
        buf.lines.push_back("");
    }

    buf.modified = true;
    buf.invalidate_hunks();

    win.cursors = {{start, 0}};
    win.clamp_all_cursors(buf, mode);
    update_window_scroll(win, buf);

    show_git_hunk_popup = false;
    set_info_msg("Git: Hunk #" + std::to_string(active_hunk_idx + 1) + " reverted. Undo with 'u'.");
}

void VimEngine::handle_git_hunk_popup(const ncinput& ni, uint32_t key) {
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();

    if (key == NCKEY_ESC || key == NCKEY_F04 || (ni.id == NCKEY_F04) || key == 'q' || key == 'Q') {
        show_git_hunk_popup = false;
        return;
    }

    if (hunks.empty()) {
        show_git_hunk_popup = false;
        return;
    }

    if (key == 'r' || key == 'R') {
        revert_active_hunk();
        return;
    }

    if (key == NCKEY_F02 || (ni.id == NCKEY_F02) || key == NCKEY_UP || key == 'k' || key == 'K') {
        active_hunk_idx = (active_hunk_idx + static_cast<int>(hunks.size()) - 1) % hunks.size();
    } else if (key == NCKEY_F03 || (ni.id == NCKEY_F03) || key == NCKEY_DOWN || key == 'j' || key == 'J') {
        active_hunk_idx = (active_hunk_idx + 1) % hunks.size();
    }
}

void VimEngine::open_hunk_diff() {
    auto& buf = active_buf();
    if (!buf.is_git_repo || !buf.git_tracked) {
        set_info_msg("No HEAD version for this file");
        return;
    }

    hunk_diff_head_lines = buf.git_base_lines;
    hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
    hunk_diff_focus = "left";
    hunk_diff_status_msg.clear();

    auto& win = active_win();
    int cur_y = win.cursors.empty() ? 0 : win.cursors.front().y;
    hunk_diff_cursor_row = 0;
    for (size_t r = 0; r < hunk_diff_diff.rows.size(); ++r) {
        if (hunk_diff_diff.rows[r].left_idx == cur_y) {
            hunk_diff_cursor_row = static_cast<int>(r);
            break;
        }
    }
    hunk_diff_scroll_y = 0;
    show_hunk_diff = true;
    set_info_msg("");
}

void VimEngine::close_hunk_diff() {
    show_hunk_diff = false;
    auto& buf = active_buf();
    auto& win = active_win();
    if (hunk_diff_cursor_row >= 0 && hunk_diff_cursor_row < static_cast<int>(hunk_diff_diff.rows.size())) {
        int l_idx = hunk_diff_diff.rows[hunk_diff_cursor_row].left_idx;
        if (l_idx >= 0 && l_idx < static_cast<int>(buf.lines.size())) {
            win.cursors = {{l_idx, 0}};
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
        }
    }
    set_info_msg("");
}

void VimEngine::handle_hunk_diff_input(const ncinput& ni, uint32_t key) {
    auto& buf = active_buf();
    auto& win = active_win();
    const auto& diff = hunk_diff_diff;

    if (key == 'q' || key == 'Q' || key == NCKEY_ESC) {
        close_hunk_diff();
        return;
    }

    if (key == 'j' || key == NCKEY_DOWN) {
        if (!diff.rows.empty()) {
            hunk_diff_cursor_row = std::min(static_cast<int>(diff.rows.size()) - 1, hunk_diff_cursor_row + 1);
        }
        return;
    }

    if (key == 'k' || key == NCKEY_UP) {
        if (!diff.rows.empty()) {
            hunk_diff_cursor_row = std::max(0, hunk_diff_cursor_row - 1);
        }
        return;
    }

    if (key == NCKEY_PGDOWN) {
        if (!diff.rows.empty()) {
            hunk_diff_cursor_row = std::min(static_cast<int>(diff.rows.size()) - 1, hunk_diff_cursor_row + 10);
        }
        return;
    }

    if (key == NCKEY_PGUP) {
        if (!diff.rows.empty()) {
            hunk_diff_cursor_row = std::max(0, hunk_diff_cursor_row - 10);
        }
        return;
    }

    if (key == 'l') {
        hunk_diff_cursor_row = diff.next_hunk_row(hunk_diff_cursor_row);
        return;
    }

    if (key == 'L') {
        hunk_diff_cursor_row = diff.prev_hunk_row(hunk_diff_cursor_row);
        return;
    }

    if (key == '\t' || key == NCKEY_TAB) {
        hunk_diff_focus = (hunk_diff_focus == "left") ? "right" : "left";
        return;
    }

    if (key == 'y') {
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;
        const auto& row = diff.rows[hunk_diff_cursor_row];
        if (hunk_diff_focus == "left") {
            if (row.left_idx < 0 || row.left_idx >= static_cast<int>(diff.left_lines.size())) {
                hunk_diff_status_msg = "Cannot yank padding row";
                return;
            }
            std::string line = diff.left_lines[row.left_idx];
            yank_reg.is_linewise = true;
            yank_reg.lines = {line};
            yank_reg.text = line + "\n";
            hunk_diff_status_msg = "Yanked line from Working";
        } else {
            if (row.right_idx < 0 || row.right_idx >= static_cast<int>(diff.right_lines.size())) {
                hunk_diff_status_msg = "Cannot yank padding row";
                return;
            }
            std::string line = diff.right_lines[row.right_idx];
            yank_reg.is_linewise = true;
            yank_reg.lines = {line};
            yank_reg.text = line + "\n";
            hunk_diff_status_msg = "Yanked line from HEAD";
        }
        return;
    }

    if (key == 'p') {
        if (yank_reg.lines.empty() || !yank_reg.is_linewise) {
            hunk_diff_status_msg = "Clipboard empty or not line content";
            return;
        }
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;
        const auto& row = diff.rows[hunk_diff_cursor_row];
        int target_y = -1;
        if (row.left_idx >= 0) {
            target_y = row.left_idx + 1;
        } else {
            for (int r = hunk_diff_cursor_row - 1; r >= 0; --r) {
                if (diff.rows[r].left_idx >= 0) {
                    target_y = diff.rows[r].left_idx + 1;
                    break;
                }
            }
            if (target_y == -1) target_y = 0;
        }
        buf.push_undo(win.cursors);
        int ins_pos = std::clamp(target_y, 0, static_cast<int>(buf.lines.size()));
        buf.lines.insert(buf.lines.begin() + ins_pos, yank_reg.lines.begin(), yank_reg.lines.end());
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
        hunk_diff_status_msg = "Pasted line below cursor";
        return;
    }

    if (key == 'd') {
        if (hunk_diff_focus == "right") {
            hunk_diff_status_msg = "Cannot delete from HEAD panel";
            return;
        }
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;
        const auto& row = diff.rows[hunk_diff_cursor_row];
        if (row.left_idx < 0 || row.left_idx >= static_cast<int>(buf.lines.size())) {
            hunk_diff_status_msg = "Cannot delete padding row";
            return;
        }
        buf.push_undo(win.cursors);
        int del_y = row.left_idx;
        if (buf.lines.size() > 1) {
            buf.lines.erase(buf.lines.begin() + del_y);
        } else {
            buf.lines[0] = "";
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
        hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
        hunk_diff_status_msg = "Deleted line from working buffer";
        return;
    }

    if (key == 'a' || key == 'A') {
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;
        const auto& row = diff.rows[hunk_diff_cursor_row];
        if (row.hunk_idx == -1 || row.hunk_idx >= static_cast<int>(diff.hunks.size())) {
            hunk_diff_status_msg = "No hunk at cursor to apply";
            return;
        }
        const auto& hunk = diff.hunks[row.hunk_idx];
        buf.push_undo(win.cursors);
        int l_start = hunk.left_start;
        int l_count = hunk.left_count;
        if (l_start < static_cast<int>(buf.lines.size())) {
            int erase_cnt = std::min(l_count, static_cast<int>(buf.lines.size()) - l_start);
            buf.lines.erase(buf.lines.begin() + l_start, buf.lines.begin() + l_start + erase_cnt);
        }
        if (!hunk.right_lines.empty()) {
            int ins_pos = std::min(l_start, static_cast<int>(buf.lines.size()));
            buf.lines.insert(buf.lines.begin() + ins_pos, hunk.right_lines.begin(), hunk.right_lines.end());
        }
        if (buf.lines.empty()) buf.lines.push_back("");
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
        hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
        hunk_diff_status_msg = "Applied hunk #" + std::to_string(hunk.id + 1) + " from HEAD";
        return;
    }

    if (key == 'u') {
        if (buf.undo(win.cursors)) {
            hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
            hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
            hunk_diff_status_msg = "Undo applied";
        } else {
            hunk_diff_status_msg = "Already at oldest change";
        }
        return;
    }

    if (key == 'w' || key == 19 || (ni.ctrl && (key == 's' || key == 'S' || ni.id == 's' || ni.id == 'S'))) {
        if (buf.save_to_file()) {
            hunk_diff_status_msg = "\"" + buf.name + "\" written";
        } else {
            hunk_diff_status_msg = "E212: Can't open file for writing";
        }
        return;
    }
}

void VimEngine::render_hunk_diff(unsigned int screen_h, unsigned int screen_w) {
    if (screen_h < 4 || screen_w < 20) return;

    int left_w = (static_cast<int>(screen_w) - 1) / 2;
    int divider_x = left_w;
    int right_x = divider_x + 1;
    int right_w = static_cast<int>(screen_w) - right_x;

    int visible_rows = std::max(1, static_cast<int>(screen_h) - 2);

    if (hunk_diff_cursor_row < hunk_diff_scroll_y) {
        hunk_diff_scroll_y = hunk_diff_cursor_row;
    }
    if (hunk_diff_cursor_row >= hunk_diff_scroll_y + visible_rows) {
        hunk_diff_scroll_y = hunk_diff_cursor_row - visible_rows + 1;
    }
    hunk_diff_scroll_y = std::max(0, hunk_diff_scroll_y);

    const auto& diff = hunk_diff_diff;
    int total_rows = static_cast<int>(diff.rows.size());

    for (int r = 0; r < visible_rows; ++r) {
        int row_idx = hunk_diff_scroll_y + r;
        int draw_y = r;

        if (row_idx >= total_rows) {
            ncplane_set_bg_rgb8(stdplane, 18, 20, 24);
            for (int c = 0; c < static_cast<int>(screen_w); ++c) {
                if (c == divider_x) {
                    ncplane_set_fg_rgb8(stdplane, 65, 70, 80);
                    ncplane_putstr_yx(stdplane, draw_y, c, "│");
                } else {
                    ncplane_putchar_yx(stdplane, draw_y, c, ' ');
                }
            }
            continue;
        }

        const auto& arow = diff.rows[row_idx];
        bool is_cursor_row = (row_idx == hunk_diff_cursor_row);
        bool is_hunk = (arow.hunk_idx != -1);

        uint8_t left_bg_r = 18, left_bg_g = 20, left_bg_b = 24;
        uint8_t right_bg_r = 18, right_bg_g = 20, right_bg_b = 24;

        if (is_cursor_row) {
            if (hunk_diff_focus == "left") {
                left_bg_r = 40; left_bg_g = 52; left_bg_b = 78;
                right_bg_r = 28; right_bg_g = 32; right_bg_b = 44;
            } else {
                left_bg_r = 28; left_bg_g = 32; left_bg_b = 44;
                right_bg_r = 40; right_bg_g = 52; right_bg_b = 78;
            }
        } else if (is_hunk) {
            const auto& hk = diff.hunks[arow.hunk_idx];
            if (hk.kind == HunkType::ADDED) {
                left_bg_r = 20; left_bg_g = 32; left_bg_b = 26;
                right_bg_r = 22; right_bg_g = 40; right_bg_b = 30;
            } else if (hk.kind == HunkType::DELETED) {
                left_bg_r = 38; left_bg_g = 24; left_bg_b = 26;
                right_bg_r = 30; right_bg_g = 20; right_bg_b = 22;
            } else {
                left_bg_r = 32; left_bg_g = 32; left_bg_b = 44;
                right_bg_r = 30; right_bg_g = 30; right_bg_b = 42;
            }
        }

        // Fill left panel
        ncplane_set_bg_rgb8(stdplane, left_bg_r, left_bg_g, left_bg_b);
        for (int c = 0; c < left_w; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        // Draw left line number
        char l_num[16];
        if (arow.left_idx >= 0) {
            snprintf(l_num, sizeof(l_num), "%4d ", arow.left_idx + 1);
            if (is_cursor_row && hunk_diff_focus == "left") {
                ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            } else if (is_hunk) {
                ncplane_set_fg_rgb8(stdplane, 100, 200, 255);
            } else {
                ncplane_set_fg_rgb8(stdplane, 110, 115, 125);
            }
            ncplane_putstr_yx(stdplane, draw_y, 0, l_num);
        } else {
            ncplane_putstr_yx(stdplane, draw_y, 0, "     ");
        }

        // Draw left content
        int left_text_max_w = left_w - 5;
        if (left_text_max_w > 0) {
            if (arow.left_idx < 0) {
                ncplane_set_fg_rgb8(stdplane, 85, 120, 175);
                ncplane_putstr_yx(stdplane, draw_y, 5, "~");
            } else {
                const std::string& line = diff.left_lines[arow.left_idx];
                std::string disp = line;
                if (static_cast<int>(disp.size()) > left_text_max_w) {
                    disp = disp.substr(0, left_text_max_w);
                }
                ncplane_set_fg_rgb8(stdplane, is_cursor_row ? 255 : 220, is_cursor_row ? 255 : 225, is_cursor_row ? 255 : 230);
                ncplane_putstr_yx(stdplane, draw_y, 5, disp.c_str());
            }
        }

        // Draw divider glyph
        ncplane_set_bg_rgb8(stdplane, 18, 20, 24);
        if (is_cursor_row) {
            ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            if (hunk_diff_focus == "left") {
                ncplane_putstr_yx(stdplane, draw_y, divider_x, "▌");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, divider_x, "▐");
            }
        } else if (is_hunk) {
            ncplane_set_fg_rgb8(stdplane, 130, 200, 255);
            ncplane_putstr_yx(stdplane, draw_y, divider_x, "◆");
        } else {
            ncplane_set_fg_rgb8(stdplane, 65, 70, 80);
            ncplane_putstr_yx(stdplane, draw_y, divider_x, "│");
        }

        // Fill right panel
        ncplane_set_bg_rgb8(stdplane, right_bg_r, right_bg_g, right_bg_b);
        for (int c = right_x; c < static_cast<int>(screen_w); ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        // Draw right line number
        char r_num[16];
        if (arow.right_idx >= 0) {
            snprintf(r_num, sizeof(r_num), "%4d ", arow.right_idx + 1);
            if (is_cursor_row && hunk_diff_focus == "right") {
                ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            } else if (is_hunk) {
                ncplane_set_fg_rgb8(stdplane, 100, 200, 255);
            } else {
                ncplane_set_fg_rgb8(stdplane, 110, 115, 125);
            }
            ncplane_putstr_yx(stdplane, draw_y, right_x, r_num);
        } else {
            ncplane_putstr_yx(stdplane, draw_y, right_x, "     ");
        }

        // Draw right content
        int right_text_max_w = right_w - 5;
        if (right_text_max_w > 0) {
            if (arow.right_idx < 0) {
                ncplane_set_fg_rgb8(stdplane, 85, 120, 175);
                ncplane_putstr_yx(stdplane, draw_y, right_x + 5, "~");
            } else {
                const std::string& line = diff.right_lines[arow.right_idx];
                std::string disp = line;
                if (static_cast<int>(disp.size()) > right_text_max_w) {
                    disp = disp.substr(0, right_text_max_w);
                }
                ncplane_set_fg_rgb8(stdplane, is_cursor_row ? 255 : 220, is_cursor_row ? 255 : 225, is_cursor_row ? 255 : 230);
                ncplane_putstr_yx(stdplane, draw_y, right_x + 5, disp.c_str());
            }
        }
    }

    // Status row (second from bottom, row screen_h - 2) in reverse video
    int status_y = static_cast<int>(screen_h) - 2;
    ncplane_set_bg_rgb8(stdplane, 225, 230, 240);
    ncplane_set_fg_rgb8(stdplane, 20, 22, 28);
    for (unsigned int c = 0; c < screen_w; ++c) {
        ncplane_putchar_yx(stdplane, status_y, c, ' ');
    }

    int current_hunk_id = 0;
    if (hunk_diff_cursor_row >= 0 && hunk_diff_cursor_row < total_rows) {
        int h_idx = diff.rows[hunk_diff_cursor_row].hunk_idx;
        if (h_idx >= 0 && h_idx < static_cast<int>(diff.hunks.size())) {
            current_hunk_id = diff.hunks[h_idx].id + 1;
        }
    }

    std::string focus_label = (hunk_diff_focus == "left") ? "Working" : "HEAD";
    std::string status_text = " " + std::to_string(current_hunk_id) + "/" +
                              std::to_string(diff.hunks.size()) +
                              "  Focus: " + focus_label;
    if (!hunk_diff_status_msg.empty()) {
        status_text += "  |  " + hunk_diff_status_msg;
    }
    if (static_cast<int>(status_text.size()) >= static_cast<int>(screen_w)) {
        status_text = status_text.substr(0, screen_w - 1);
    }
    ncplane_on_styles(stdplane, NCSTYLE_BOLD);
    ncplane_putstr_yx(stdplane, status_y, 0, status_text.c_str());
    ncplane_off_styles(stdplane, NCSTYLE_BOLD);

    // Legend row (bottom, row screen_h - 1)
    int legend_y = static_cast<int>(screen_h) - 1;
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    ncplane_set_fg_rgb8(stdplane, 175, 185, 205);
    for (unsigned int c = 0; c < screen_w; ++c) {
        ncplane_putchar_yx(stdplane, legend_y, c, ' ');
    }
    std::string legend_text = " [Tab] Switch  [l/L] Hunk  [a] Apply  [u] Undo  [d] Delete  [y] Yank  [p] Paste  [w] Write  [q] Quit ";
    if (static_cast<int>(legend_text.size()) >= static_cast<int>(screen_w)) {
        legend_text = legend_text.substr(0, screen_w - 1);
    }
    ncplane_putstr_yx(stdplane, legend_y, 0, legend_text.c_str());
}

void VimEngine::open_filepicker() {
    show_whichkey_popup = false;
    show_git_hunk_popup = false;
    show_settings_popup = false;
    leader_pending = false;

    filepicker_query.clear();
    filepicker_selected_idx = 0;
    filepicker_scroll = 0;
    scan_project_files();
    filter_filepicker_files();
    show_filepicker = true;
    set_info_msg("FilePicker: Type to filter, [Enter] open, [Esc] close");
}

void VimEngine::scan_project_files() {
    filepicker_all_files.clear();
    std::string root = !project_dir.empty() ? project_dir : ".";

    bool used_git = get_git_project_files(root, filepicker_all_files);

    if (!used_git) {
        std::error_code ec;
        auto iter = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
        auto end_iter = fs::recursive_directory_iterator();

        const int MAX_NON_REPO_DEPTH = 3;
        const size_t MAX_FILES = 500;
        auto start_time = std::chrono::steady_clock::now();

        for (; iter != end_iter && !ec; iter.increment(ec)) {
            if (filepicker_all_files.size() >= MAX_FILES) break;

            if ((filepicker_all_files.size() % 100) == 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 100) {
                    break;
                }
            }

            if (iter.depth() >= MAX_NON_REPO_DEPTH) {
                iter.disable_recursion_pending();
            }

            const auto& path = iter->path();
            std::string fn = path.filename().string();

            if (iter->is_symlink(ec)) {
                if (iter->is_directory(ec)) {
                    iter.disable_recursion_pending();
                }
                continue;
            }

            if (iter->is_directory(ec)) {
                if (fn.front() == '.' || fn == "node_modules" || fn == "build" ||
                    fn == "target" || fn == "bin" || fn == "obj" || fn == "dist" ||
                    fn == "vendor" || fn == "venv" || fn == ".venv" || fn == "__pycache__" ||
                    fn == "cache" || fn == ".cache" || fn == "tmp" || fn == "temp" ||
                    fn == "proc" || fn == "sys" || fn == "dev" || fn == "run" ||
                    fn == "var" || fn == "Library" || fn == "AppData") {
                    iter.disable_recursion_pending();
                }
                continue;
            }

            if (iter->is_regular_file(ec)) {
                if (fn.front() == '.') continue;
                std::string rel;
                try {
                    rel = fs::relative(path, root, ec).string();
                } catch (...) {
                    rel = fn;
                }
                if (!ec && !rel.empty()) {
                    filepicker_all_files.push_back(rel);
                }
            }
        }
    }

    std::sort(filepicker_all_files.begin(), filepicker_all_files.end());
}

void VimEngine::filter_filepicker_files() {
    filepicker_filtered_files.clear();
    if (filepicker_query.empty()) {
        filepicker_filtered_files = filepicker_all_files;
    } else {
        std::string q = filepicker_query;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });

        for (const auto& f : filepicker_all_files) {
            std::string lf = f;
            std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lf.find(q) != std::string::npos) {
                filepicker_filtered_files.push_back(f);
            }
        }
    }

    if (filepicker_selected_idx >= static_cast<int>(filepicker_filtered_files.size())) {
        filepicker_selected_idx = std::max(0, static_cast<int>(filepicker_filtered_files.size()) - 1);
    }
}

void VimEngine::handle_filepicker_input(const ncinput& ni, uint32_t key) {
    if (key == NCKEY_ESC) {
        show_filepicker = false;
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        if (!filepicker_filtered_files.empty() &&
            filepicker_selected_idx >= 0 &&
            filepicker_selected_idx < static_cast<int>(filepicker_filtered_files.size())) {

            std::string rel_path = filepicker_filtered_files[filepicker_selected_idx];
            std::string full_path = (fs::path(!project_dir.empty() ? project_dir : ".") / rel_path).lexically_normal().string();

            save_window_position(active_win(), active_buf());

            size_t found_idx = buffers.size();
            for (size_t i = 0; i < buffers.size(); ++i) {
                if (buffers[i]->file_path == full_path || buffers[i]->name == rel_path || buffers[i]->file_path == rel_path) {
                    found_idx = i;
                    break;
                }
            }

            if (found_idx == buffers.size()) {
                buffers.push_back(TextBuffer::from_file(full_path));
                found_idx = buffers.size() - 1;
            }

            active_win().buffer_idx = found_idx;
            restore_window_position(active_win(), active_buf());
            show_filepicker = false;
            set_info_msg("\"" + active_buf().name + "\" [" + std::to_string(active_buf().lines.size()) + " lines]");
        }
        return;
    }

    if (key == NCKEY_UP || (ni.ctrl && (key == 'p' || key == 'P' || key == 'k' || key == 'K'))) {
        if (filepicker_selected_idx > 0) {
            filepicker_selected_idx--;
        } else if (!filepicker_filtered_files.empty()) {
            filepicker_selected_idx = static_cast<int>(filepicker_filtered_files.size()) - 1;
        }
        return;
    }

    if (key == NCKEY_DOWN || (ni.ctrl && (key == 'n' || key == 'N' || key == 'j' || key == 'J'))) {
        if (filepicker_selected_idx + 1 < static_cast<int>(filepicker_filtered_files.size())) {
            filepicker_selected_idx++;
        } else {
            filepicker_selected_idx = 0;
        }
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        if (!filepicker_query.empty()) {
            filepicker_query.pop_back();
            filter_filepicker_files();
        }
        return;
    }

    if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
        filepicker_query += static_cast<char>(key);
        filter_filepicker_files();
        return;
    }
}

void VimEngine::render_filepicker(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(48, static_cast<int>(screen_w * 0.65));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 4);
    int popup_h = 15;
    popup_h = std::min(popup_h, static_cast<int>(screen_h) - 4);

    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    // Draw background
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Draw rounded box
    ncplane_set_fg_rgb8(stdplane, 80, 180, 240);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }
    ncplane_putstr_yx(stdplane, popup_y + 2, popup_x, "├");
    ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + popup_w - 1, "┤");

    // Title & count
    std::string title = " File Finder (Alt-e / Space-f) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    std::string count_str = " (" + std::to_string(filepicker_filtered_files.size()) + "/" +
                            std::to_string(filepicker_all_files.size()) + ") ";
    if (popup_w - static_cast<int>(count_str.size()) - 3 > popup_x + static_cast<int>(title.size()) + 2) {
        ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - static_cast<int>(count_str.size()) - 2, count_str.c_str());
    }

    // Search query prompt
    ncplane_set_fg_rgb8(stdplane, 80, 220, 120);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "> ");
    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
    std::string q_display = filepicker_query + "_";
    int max_q_w = popup_w - 6;
    if (static_cast<int>(q_display.size()) > max_q_w) {
        q_display = q_display.substr(q_display.size() - max_q_w);
    }
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 4, q_display.c_str());

    // File list
    int visible_rows = popup_h - 4;
    if (filepicker_selected_idx < filepicker_scroll) {
        filepicker_scroll = filepicker_selected_idx;
    }
    if (filepicker_selected_idx >= filepicker_scroll + visible_rows) {
        filepicker_scroll = filepicker_selected_idx - visible_rows + 1;
    }

    for (int r = 0; r < visible_rows; ++r) {
        int idx = filepicker_scroll + r;
        int draw_y = popup_y + 3 + r;

        if (idx < static_cast<int>(filepicker_filtered_files.size())) {
            bool is_sel = (idx == filepicker_selected_idx);
            if (is_sel) {
                ncplane_set_bg_rgb8(stdplane, 45, 65, 115);
                ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            } else {
                ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
                ncplane_set_fg_rgb8(stdplane, 210, 215, 225);
            }

            // Fill row background
            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            std::string prefix = is_sel ? " ▶ " : "   ";
            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 205, 60);
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, prefix.c_str());

            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            else ncplane_set_fg_rgb8(stdplane, 210, 215, 225);

            std::string fn = filepicker_filtered_files[idx];
            int max_len = popup_w - 7;
            if (static_cast<int>(fn.size()) > max_len) {
                fn = "..." + fn.substr(fn.size() - max_len + 3);
            }
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 5, fn.c_str());
        }
    }

    // Footer
    std::string footer = " [▲/▼] Navigate  [Enter] Open  [Esc] Cancel ";
    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}

std::string VimEngine::get_word_under_cursor() {
    auto& win = active_win();
    auto& buf = active_buf();
    if (win.cursors.empty()) return "";
    Cursor c = win.cursors.front();
    if (c.y < 0 || c.y >= static_cast<int>(buf.lines.size())) return "";
    const std::string& line = buf.lines[c.y];
    if (line.empty()) return "";

    int cx = std::clamp(c.x, 0, static_cast<int>(line.size()) - 1);
    auto is_word_char = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };

    if (!is_word_char(line[cx])) {
        // Try nearby word char
        if (cx > 0 && is_word_char(line[cx - 1])) cx--;
        else if (cx + 1 < static_cast<int>(line.size()) && is_word_char(line[cx + 1])) cx++;
        else return "";
    }

    int start = cx;
    while (start > 0 && is_word_char(line[start - 1])) start--;
    int end = cx;
    while (end + 1 < static_cast<int>(line.size()) && is_word_char(line[end + 1])) end++;

    return line.substr(start, end - start + 1);
}

std::string VimEngine::get_rg_cache_path() const {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path cdir;
    if (xdg && *xdg != '\0') {
        cdir = fs::path(xdg) / "fe";
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home != '\0') {
            cdir = fs::path(home) / ".config" / "fe";
        } else {
            cdir = ".config/fe";
        }
    }
    return (cdir / "rg_search.json").string();
}

void VimEngine::save_rg_cache() {
    std::string path = get_rg_cache_path();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"query\": \"";
    for (char c : rg_query) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else out << c;
    }
    out << "\",\n";
    out << "  \"groups\": [\n";

    for (size_t gi = 0; gi < rg_groups.size(); ++gi) {
        const auto& grp = rg_groups[gi];
        out << "    {\n";
        out << "      \"file\": \"";
        for (char c : grp.file) {
            if (c == '"') out << "\\\"";
            else if (c == '\\') out << "\\\\";
            else out << c;
        }
        out << "\",\n";
        out << "      \"matches\": [\n";
        for (size_t mi = 0; mi < grp.matches.size(); ++mi) {
            const auto& m = grp.matches[mi];
            out << "        {\"line\": " << m.line << ", \"col\": " << m.col << ", \"text\": \"";
            for (char c : m.text) {
                if (c == '"') out << "\\\"";
                else if (c == '\\') out << "\\\\";
                else if (c == '\t') out << "  ";
                else if (c >= 32 && c <= 126) out << c;
            }
            out << "\"}";
            if (mi + 1 < grp.matches.size()) out << ",";
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (gi + 1 < rg_groups.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
}

void VimEngine::load_rg_cache() {
    std::string path = get_rg_cache_path();
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    rg_groups.clear();
    rg_query.clear();

    std::string cur_file;
    std::vector<RgMatch> cur_matches;

    while (std::getline(in, line)) {
        size_t q_pos = line.find("\"query\": \"");
        if (q_pos != std::string::npos) {
            size_t end_q = line.find("\"", q_pos + 10);
            if (end_q != std::string::npos) {
                rg_query = line.substr(q_pos + 10, end_q - (q_pos + 10));
            }
            continue;
        }

        size_t f_pos = line.find("\"file\": \"");
        if (f_pos != std::string::npos) {
            if (!cur_file.empty() && !cur_matches.empty()) {
                rg_groups.push_back({cur_file, cur_matches});
                cur_matches.clear();
            }
            size_t end_f = line.find("\"", f_pos + 9);
            if (end_f != std::string::npos) {
                cur_file = line.substr(f_pos + 9, end_f - (f_pos + 9));
            }
            continue;
        }

        size_t m_pos = line.find("{\"line\":");
        if (m_pos != std::string::npos) {
            int ln = 1, col = 1;
            std::string txt;

            size_t l_idx = line.find("\"line\":", m_pos);
            if (l_idx != std::string::npos) {
                try { ln = std::stoi(line.substr(l_idx + 7)); } catch (...) {}
            }
            size_t c_idx = line.find("\"col\":", m_pos);
            if (c_idx != std::string::npos) {
                try { col = std::stoi(line.substr(c_idx + 6)); } catch (...) {}
            }
            size_t t_idx = line.find("\"text\": \"", m_pos);
            if (t_idx != std::string::npos) {
                size_t end_t = line.rfind("\"}");
                if (end_t != std::string::npos && end_t > t_idx + 9) {
                    txt = line.substr(t_idx + 9, end_t - (t_idx + 9));
                }
            }
            cur_matches.push_back({cur_file, ln, col, txt});
        }
    }

    if (!cur_file.empty() && !cur_matches.empty()) {
        rg_groups.push_back({cur_file, cur_matches});
    }

    rebuild_rg_display_lines();
}

void VimEngine::run_ripgrep(const std::string& pattern) {
    if (pattern.empty()) return;

    rg_query = pattern;
    rg_groups.clear();
    rg_replace_active = false;
    rg_replace_query.clear();

    std::string root = !project_dir.empty() ? project_dir : ".";

    std::string escaped_pattern;
    for (char c : pattern) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') escaped_pattern += '\\';
        escaped_pattern += c;
    }

    std::string cmd = "rg -n --column --no-heading --hidden -g '!.git' --max-count 100 \"" +
                      escaped_pattern + "\" \"" + root + "\" 2>/dev/null";

    FILE* fp = popen(cmd.c_str(), "r");
    bool ran_grep_fallback = false;
    if (!fp) ran_grep_fallback = true;

    char buf[4096];
    std::map<std::string, std::vector<RgMatch>> grouped_map;

    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

            // Format: file:line:col:text
            size_t first_colon = line.find(':');
            if (first_colon == std::string::npos) continue;
            size_t sec_colon = line.find(':', first_colon + 1);
            if (sec_colon == std::string::npos) continue;
            size_t third_colon = line.find(':', sec_colon + 1);
            if (third_colon == std::string::npos) continue;

            std::string file = line.substr(0, first_colon);
            int ln = 1, col = 1;
            try { ln = std::stoi(line.substr(first_colon + 1, sec_colon - first_colon - 1)); } catch (...) {}
            try { col = std::stoi(line.substr(sec_colon + 1, third_colon - sec_colon - 1)); } catch (...) {}
            std::string text = line.substr(third_colon + 1);

            std::error_code ec;
            std::string rel_file = fs::relative(file, root, ec).string();
            if (ec || rel_file.empty()) rel_file = file;

            grouped_map[rel_file].push_back({rel_file, ln, col, text});
        }
        int status = pclose(fp);
        if (status != 0 && grouped_map.empty()) {
            ran_grep_fallback = true;
        }
    }

    if (ran_grep_fallback) {
        std::string fb_cmd = "grep -rnI --exclude-dir=.git \"" + escaped_pattern + "\" \"" + root + "\" 2>/dev/null";
        FILE* fb_fp = popen(fb_cmd.c_str(), "r");
        if (fb_fp) {
            while (fgets(buf, sizeof(buf), fb_fp)) {
                std::string line(buf);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                size_t first_colon = line.find(':');
                if (first_colon == std::string::npos) continue;
                size_t sec_colon = line.find(':', first_colon + 1);
                if (sec_colon == std::string::npos) continue;

                std::string file = line.substr(0, first_colon);
                int ln = 1;
                try { ln = std::stoi(line.substr(first_colon + 1, sec_colon - first_colon - 1)); } catch (...) {}
                std::string text = line.substr(sec_colon + 1);

                std::error_code ec;
                std::string rel_file = fs::relative(file, root, ec).string();
                if (ec || rel_file.empty()) rel_file = file;

                grouped_map[rel_file].push_back({rel_file, ln, 1, text});
            }
            pclose(fb_fp);
        }
    }

    for (auto& pair : grouped_map) {
        rg_groups.push_back({pair.first, pair.second});
    }

    rebuild_rg_display_lines();
    save_rg_cache();

    rg_selected_match_idx = 0;
    rg_selected_display_idx = 0;
    rg_scroll = 0;
    show_rg_popup = true;

    set_info_msg("Ripgrep: \"" + rg_query + "\" (" + std::to_string(rg_flattened_matches.size()) +
                 " matches in " + std::to_string(rg_groups.size()) + " files)");
}

void VimEngine::rebuild_rg_display_lines() {
    rg_display_lines.clear();
    rg_flattened_matches.clear();

    for (const auto& grp : rg_groups) {
        rg_display_lines.push_back({true, grp.file, -1, 0, 0, ""});
        for (const auto& m : grp.matches) {
            int midx = static_cast<int>(rg_flattened_matches.size());
            rg_flattened_matches.push_back(m);
            rg_display_lines.push_back({false, m.file, midx, m.line, m.col, m.text});
        }
    }

    if (rg_selected_display_idx >= static_cast<int>(rg_display_lines.size())) {
        rg_selected_display_idx = std::max(0, static_cast<int>(rg_display_lines.size()) - 1);
    }
    if (rg_selected_match_idx >= static_cast<int>(rg_flattened_matches.size())) {
        rg_selected_match_idx = std::max(0, static_cast<int>(rg_flattened_matches.size()) - 1);
    }
}

void VimEngine::open_selected_rg_match() {
    if (rg_display_lines.empty() ||
        rg_selected_display_idx < 0 ||
        rg_selected_display_idx >= static_cast<int>(rg_display_lines.size())) {
        return;
    }

    const auto& dline = rg_display_lines[rg_selected_display_idx];
    int target_midx = dline.match_idx;
    if (dline.is_file_header) {
        for (size_t i = 0; i < rg_flattened_matches.size(); ++i) {
            if (rg_flattened_matches[i].file == dline.file) {
                target_midx = static_cast<int>(i);
                break;
            }
        }
    }

    if (target_midx < 0 || target_midx >= static_cast<int>(rg_flattened_matches.size())) {
        return;
    }

    const auto& m = rg_flattened_matches[target_midx];
    std::string root = !project_dir.empty() ? project_dir : ".";
    std::string full_path = (fs::path(root) / m.file).lexically_normal().string();

    save_window_position(active_win(), active_buf());

    size_t found_idx = buffers.size();
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (buffers[i]->file_path == full_path || buffers[i]->name == m.file || buffers[i]->file_path == m.file) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == buffers.size()) {
        buffers.push_back(TextBuffer::from_file(full_path));
        found_idx = buffers.size() - 1;
    }

    active_win().buffer_idx = found_idx;
    auto& nb = active_buf();

    int target_y = std::clamp(m.line - 1, 0, std::max(0, static_cast<int>(nb.lines.size()) - 1));
    int target_x = 0;
    if (target_y < static_cast<int>(nb.lines.size())) {
        target_x = std::clamp(m.col - 1, 0, static_cast<int>(nb.lines[target_y].size()));
    }

    active_win().cursors = {{target_y, target_x}};
    active_win().clamp_all_cursors(nb, mode);
    update_window_scroll(active_win(), nb);

    show_rg_popup = false;
    set_info_msg("\"" + nb.name + "\" [" + std::to_string(target_y + 1) + ":" + std::to_string(target_x + 1) + "]");
}

void VimEngine::apply_rg_replace() {
    int non_ignored_cnt = 0;
    for (const auto& m : rg_flattened_matches) {
        if (!m.ignored) non_ignored_cnt++;
    }
    if (non_ignored_cnt == 0) {
        set_info_msg("Replace cancelled: All matches are ignored.");
        return;
    }

    std::map<std::string, std::vector<RgMatch>> file_matches;
    for (const auto& m : rg_flattened_matches) {
        if (!m.ignored) {
            file_matches[m.file].push_back(m);
        }
    }

    std::string root = !project_dir.empty() ? project_dir : ".";
    int replaced_count = 0;
    std::set<std::string> modified_files;

    for (auto& pair : file_matches) {
        auto& matches = pair.second;
        std::sort(matches.begin(), matches.end(), [](const RgMatch& a, const RgMatch& b) {
            if (a.line != b.line) return a.line > b.line;
            return a.col > b.col;
        });

        std::string full_path = (fs::path(root) / pair.first).lexically_normal().string();
        std::shared_ptr<TextBuffer> target_buf = nullptr;
        for (auto& b : buffers) {
            if (b->file_path == full_path || b->name == pair.first || b->file_path == pair.first) {
                target_buf = b;
                break;
            }
        }

        if (!target_buf) {
            target_buf = TextBuffer::from_file(full_path);
        }

        if (target_buf) {
            target_buf->push_undo(active_win().cursors);
            for (const auto& m : matches) {
                int l_idx = m.line - 1;
                if (l_idx >= 0 && l_idx < static_cast<int>(target_buf->lines.size())) {
                    std::string& line = target_buf->lines[l_idx];
                    size_t pos = std::string::npos;
                    if (m.col > 0 && static_cast<size_t>(m.col - 1) < line.size()) {
                        if (line.compare(m.col - 1, rg_query.size(), rg_query) == 0) {
                            pos = m.col - 1;
                        }
                    }
                    if (pos == std::string::npos) {
                        pos = line.find(rg_query);
                    }
                    if (pos != std::string::npos) {
                        line.replace(pos, rg_query.size(), rg_replace_query);
                        replaced_count++;
                    }
                }
            }
            target_buf->save_to_file();
            modified_files.insert(pair.first);
        }
    }

    show_rg_popup = false;
    rg_replace_active = false;
    active_win().clamp_all_cursors(active_buf(), mode);
    update_window_scroll(active_win(), active_buf());
    set_info_msg("Replaced " + std::to_string(replaced_count) + " occurrences across " +
                 std::to_string(modified_files.size()) + " files.");
}

void VimEngine::handle_rg_popup_input(const ncinput& ni, uint32_t key) {
    if (key == NCKEY_ESC || key == NCKEY_F11 || (ni.id == NCKEY_F11) ||
        ((key == 'q' || key == 'Q') && !rg_replace_active)) {
        if (rg_replace_active) {
            rg_replace_active = false;
            set_info_msg("View Mode: [j/k/▲/▼] Navigate, [Enter] Open, [TAB] Replace Mode");
            return;
        }
        show_rg_popup = false;
        rg_replace_active = false;
        set_info_msg("");
        return;
    }

    // TAB: Toggle View mode vs Replace mode
    if (key == '\t' || key == NCKEY_TAB) {
        rg_replace_active = !rg_replace_active;
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        if (rg_replace_active) {
            apply_rg_replace();
        } else {
            open_selected_rg_match();
        }
        return;
    }

    // Space: In Replace Mode, toggle ignore for file group or individual match line
    // Shift/Ctrl/Alt-Space: insert space into replacement query
    if (key == ' ') {
        if (rg_replace_active) {
            if (ni.shift || ni.ctrl || ni.alt) {
                rg_replace_query += ' ';
                return;
            }
            if (!rg_display_lines.empty() &&
                rg_selected_display_idx >= 0 &&
                rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {

                const auto& dline = rg_display_lines[rg_selected_display_idx];
                if (dline.is_file_header) {
                    bool any_included = false;
                    for (const auto& m : rg_flattened_matches) {
                        if (m.file == dline.file && !m.ignored) {
                            any_included = true;
                            break;
                        }
                    }
                    for (auto& m : rg_flattened_matches) {
                        if (m.file == dline.file) {
                            m.ignored = any_included;
                        }
                    }
                    set_info_msg(any_included ? "File ignored for replace: " + dline.file
                                              : "File included for replace: " + dline.file);
                } else if (dline.match_idx >= 0 && dline.match_idx < static_cast<int>(rg_flattened_matches.size())) {
                    rg_flattened_matches[dline.match_idx].ignored = !rg_flattened_matches[dline.match_idx].ignored;
                    set_info_msg(rg_flattened_matches[dline.match_idx].ignored ? "Line ignored for replace." : "Line included for replace.");
                }
            }
            return;
        }
    }

    // Backspace: in replace mode, edits replacement query
    if (rg_replace_active && (key == NCKEY_BACKSPACE || key == 127 || key == '\b')) {
        if (!rg_replace_query.empty()) {
            rg_replace_query.pop_back();
        }
        return;
    }

    if (key == NCKEY_UP || (!rg_replace_active && (key == 'k' || key == 'K')) ||
        (ni.ctrl && (key == 'p' || key == 'P' || key == 'k' || key == 'K'))) {
        if (rg_selected_display_idx > 0) {
            rg_selected_display_idx--;
        } else if (!rg_display_lines.empty()) {
            rg_selected_display_idx = static_cast<int>(rg_display_lines.size()) - 1;
        }
        if (rg_selected_display_idx >= 0 && rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {
            int midx = rg_display_lines[rg_selected_display_idx].match_idx;
            if (midx >= 0) rg_selected_match_idx = midx;
        }
        return;
    }

    if (key == NCKEY_DOWN || (!rg_replace_active && (key == 'j' || key == 'J')) ||
        (ni.ctrl && (key == 'n' || key == 'N' || key == 'j' || key == 'J'))) {
        if (rg_selected_display_idx + 1 < static_cast<int>(rg_display_lines.size())) {
            rg_selected_display_idx++;
        } else {
            rg_selected_display_idx = 0;
        }
        if (rg_selected_display_idx >= 0 && rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {
            int midx = rg_display_lines[rg_selected_display_idx].match_idx;
            if (midx >= 0) rg_selected_match_idx = midx;
        }
        return;
    }

    if (key == NCKEY_PGUP) {
        rg_selected_display_idx = std::max(0, rg_selected_display_idx - 10);
        if (rg_selected_display_idx >= 0 && rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {
            int midx = rg_display_lines[rg_selected_display_idx].match_idx;
            if (midx >= 0) rg_selected_match_idx = midx;
        }
        return;
    }

    if (key == NCKEY_PGDOWN) {
        rg_selected_display_idx = std::min(static_cast<int>(rg_display_lines.size()) - 1, rg_selected_display_idx + 10);
        if (rg_selected_display_idx >= 0 && rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {
            int midx = rg_display_lines[rg_selected_display_idx].match_idx;
            if (midx >= 0) rg_selected_match_idx = midx;
        }
        return;
    }

    // In replace mode, printable characters type into replacement query
    if (rg_replace_active) {
        if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
            rg_replace_query += static_cast<char>(key);
            return;
        }
        if (ni.utf8[0] != '\0' && !ni.alt && !ni.ctrl) {
            rg_replace_query += reinterpret_cast<const char*>(ni.utf8);
            return;
        }
    }

    // Next / Prev File group
    if (key == ']' || key == '}') {
        for (size_t i = rg_selected_display_idx + 1; i < rg_display_lines.size(); ++i) {
            if (rg_display_lines[i].is_file_header) {
                rg_selected_display_idx = static_cast<int>(i);
                return;
            }
        }
        return;
    }
    if (key == '[' || key == '{') {
        for (int i = rg_selected_display_idx - 1; i >= 0; --i) {
            if (rg_display_lines[i].is_file_header) {
                rg_selected_display_idx = i;
                return;
            }
        }
        return;
    }

    if (key == 'r' || key == 'R') {
        run_ripgrep(rg_query);
        return;
    }
}

void VimEngine::render_rg_popup(unsigned int screen_h, unsigned int screen_w) {
    int popup_x = 0;
    int popup_y = 0;
    int popup_w = static_cast<int>(screen_w);
    int popup_h = std::max(6, static_cast<int>(screen_h) - 2);

    int header_sep_y = rg_replace_active ? (popup_y + 3) : (popup_y + 2);
    int list_start_y = header_sep_y + 1;
    int visible_rows = popup_h - (list_start_y - popup_y) - 1;

    // Background fill
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox)
    ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, header_sep_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }
    ncplane_putstr_yx(stdplane, header_sep_y, popup_x, "├");
    ncplane_putstr_yx(stdplane, header_sep_y, popup_x + popup_w - 1, "┤");

    // Title
    std::string title = rg_replace_active ? " Ripgrep Replace Mode (TAB: View / Enter: Apply) "
                                          : " Ripgrep View Mode (:vg / F11 / TAB: Replace) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    // Query & Stats line
    ncplane_set_fg_rgb8(stdplane, 240, 200, 80);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "🔍 Query:   ");
    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 14, rg_query.c_str());

    int non_ignored_cnt = 0;
    for (const auto& m : rg_flattened_matches) {
        if (!m.ignored) non_ignored_cnt++;
    }

    char stats_buf[128];
    if (rg_replace_active) {
        snprintf(stats_buf, sizeof(stats_buf), "[%d/%zu to replace | %zu files]",
                 non_ignored_cnt, rg_flattened_matches.size(), rg_groups.size());
    } else {
        snprintf(stats_buf, sizeof(stats_buf), "[%zu matches in %zu files | match %d/%zu]",
                 rg_flattened_matches.size(), rg_groups.size(),
                 rg_flattened_matches.empty() ? 0 : (rg_selected_match_idx + 1),
                 rg_flattened_matches.size());
    }
    int stats_x = popup_x + popup_w - static_cast<int>(std::string(stats_buf).size()) - 3;
    if (stats_x > popup_x + 16 + static_cast<int>(rg_query.size())) {
        ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
        ncplane_putstr_yx(stdplane, popup_y + 1, stats_x, stats_buf);
    }

    // Replace input row
    if (rg_replace_active) {
        ncplane_set_fg_rgb8(stdplane, 80, 220, 120);
        ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + 2, "🔄 Replace: ");
        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        std::string disp = rg_replace_query + "_";
        ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + 14, disp.c_str());
    }

    if (rg_selected_display_idx < rg_scroll) {
        rg_scroll = rg_selected_display_idx;
    }
    if (rg_selected_display_idx >= rg_scroll + visible_rows) {
        rg_scroll = rg_selected_display_idx - visible_rows + 1;
    }
    rg_scroll = std::max(0, rg_scroll);

    int max_x = popup_x + popup_w - 2;

    // Render grouped rows
    for (int r = 0; r < visible_rows; ++r) {
        int d_idx = rg_scroll + r;
        int draw_y = list_start_y + r;

        if (d_idx >= static_cast<int>(rg_display_lines.size())) {
            break;
        }

        const auto& dline = rg_display_lines[d_idx];
        bool is_sel = (d_idx == rg_selected_display_idx);

        if (dline.is_file_header) {
            // Group Header (Filename)
            ncplane_set_bg_rgb8(stdplane, is_sel ? 38 : 26, is_sel ? 48 : 30, is_sel ? 68 : 42);
            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, "▶ ");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, "  ");
            }

            int total_in_file = 0, incl_in_file = 0;
            for (const auto& m : rg_flattened_matches) {
                if (m.file == dline.file) {
                    total_in_file++;
                    if (!m.ignored) incl_in_file++;
                }
            }

            std::string header_text = "📁 " + dline.file;
            if (rg_replace_active) {
                if (incl_in_file == 0) {
                    ncplane_set_fg_rgb8(stdplane, 140, 140, 150);
                    header_text += "  [ALL IGNORED]";
                } else if (incl_in_file < total_in_file) {
                    ncplane_set_fg_rgb8(stdplane, 255, 180, 100);
                    header_text += "  [" + std::to_string(incl_in_file) + "/" + std::to_string(total_in_file) + " to replace]";
                } else {
                    ncplane_set_fg_rgb8(stdplane, 255, 140, 180);
                    header_text += "  [✓ ALL]";
                }
            } else {
                ncplane_set_fg_rgb8(stdplane, 255, 140, 180);
            }
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 4, header_text.c_str());
        } else {
            const auto& m = rg_flattened_matches[dline.match_idx];
            bool is_ignored = m.ignored;

            if (is_sel) {
                ncplane_set_bg_rgb8(stdplane, 40, 60, 110);
            } else {
                ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
            }

            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, "▶ ");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, "  ");
            }

            int cur_x = popup_x + 4;

            if (rg_replace_active) {
                if (is_ignored) {
                    ncplane_set_fg_rgb8(stdplane, 130, 130, 145);
                    ncplane_putstr_yx(stdplane, draw_y, cur_x, "[ ] ");
                } else {
                    ncplane_set_fg_rgb8(stdplane, 80, 220, 120);
                    ncplane_putstr_yx(stdplane, draw_y, cur_x, "[✓] ");
                }
                cur_x += 4;
            }

            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%5d:%-3d ", dline.line, dline.col);
            if (is_ignored) {
                ncplane_set_fg_rgb8(stdplane, 110, 115, 125);
            } else if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 100, 220, 255);
            } else {
                ncplane_set_fg_rgb8(stdplane, 80, 160, 200);
            }
            ncplane_putstr_yx(stdplane, draw_y, cur_x, num_buf);
            cur_x += static_cast<int>(std::string(num_buf).size());

            auto print_chunk = [&](const std::string& str, uint8_t r, uint8_t g, uint8_t b, bool struck) {
                if (cur_x >= max_x) return;
                std::string to_draw = str;
                if (cur_x + static_cast<int>(to_draw.size()) > max_x) {
                    to_draw = to_draw.substr(0, max_x - cur_x);
                }
                ncplane_set_fg_rgb8(stdplane, r, g, b);
                if (struck) ncplane_on_styles(stdplane, NCSTYLE_STRUCK);
                ncplane_putstr_yx(stdplane, draw_y, cur_x, to_draw.c_str());
                if (struck) ncplane_off_styles(stdplane, NCSTYLE_STRUCK);
                cur_x += static_cast<int>(to_draw.size());
            };

            if (rg_replace_active && !is_ignored) {
                size_t match_pos = std::string::npos;
                if (dline.col > 0 && static_cast<size_t>(dline.col - 1) < dline.text.size()) {
                    if (dline.text.compare(dline.col - 1, rg_query.size(), rg_query) == 0) {
                        match_pos = dline.col - 1;
                    }
                }
                if (match_pos == std::string::npos) {
                    match_pos = dline.text.find(rg_query);
                }
                if (match_pos == std::string::npos) {
                    auto it = std::search(dline.text.begin(), dline.text.end(),
                                          rg_query.begin(), rg_query.end(),
                                          [](char a, char b) {
                                              return std::tolower(static_cast<unsigned char>(a)) ==
                                                     std::tolower(static_cast<unsigned char>(b));
                                          });
                    if (it != dline.text.end()) {
                        match_pos = std::distance(dline.text.begin(), it);
                    }
                }

                if (match_pos != std::string::npos) {
                    std::string prefix = dline.text.substr(0, match_pos);
                    std::string matched_str = dline.text.substr(match_pos, rg_query.size());
                    std::string suffix = dline.text.substr(match_pos + rg_query.size());
                    std::string repl = rg_replace_query.empty() ? "" : rg_replace_query;

                    // Prefix (normal)
                    print_chunk(prefix, is_sel ? 255 : 210, is_sel ? 255 : 215, is_sel ? 255 : 225, false);

                    // Before: strikethrough red
                    print_chunk(matched_str, 255, 80, 80, true);

                    // After: green
                    if (!repl.empty()) {
                        print_chunk(repl, 80, 240, 120, false);
                    }

                    // Suffix (normal)
                    print_chunk(suffix, is_sel ? 255 : 210, is_sel ? 255 : 215, is_sel ? 255 : 225, false);
                } else {
                    print_chunk(dline.text, is_sel ? 255 : 210, is_sel ? 255 : 215, is_sel ? 255 : 225, false);
                }
            } else if (is_ignored) {
                print_chunk(dline.text, 125, 130, 140, false);
            } else {
                if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
                else ncplane_set_fg_rgb8(stdplane, 215, 220, 230);
                print_chunk(dline.text, is_sel ? 255 : 215, is_sel ? 255 : 220, is_sel ? 255 : 230, false);
            }
        }
    }

    // Footer actions
    std::string footer;
    if (rg_replace_active) {
        footer = " [▲/▼] Navigate  [Space] Toggle Ignore  [Enter] Apply  [TAB] View Mode  [Esc] Cancel ";
        ncplane_set_fg_rgb8(stdplane, 80, 230, 140);
    } else {
        footer = " [j/k/▲/▼] Match  [TAB] Replace Mode  [{/}] File Group  [Enter] Open  [Esc/q] Close ";
        ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
    }
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}
