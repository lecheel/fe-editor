#include "engine.hpp"
#include "keymap.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace Keymap;

void VimEngine::close_hunk_diff() {
    show_hunk_diff = false;
    auto& buf = (hunk_diff_is_delta && hunk_diff_left_buf) ? *hunk_diff_left_buf : active_buf();
    auto& win = active_win();
    if (hunk_diff_cursor_row >= 0 && hunk_diff_cursor_row < static_cast<int>(hunk_diff_diff.rows.size())) {
        int l_idx = hunk_diff_diff.rows[hunk_diff_cursor_row].left_idx;
        if (l_idx >= 0 && l_idx < static_cast<int>(buf.lines.size())) {
            win.cursors = {{l_idx, 0}};
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
        }
    }
    hunk_diff_is_delta = false;
    hunk_diff_left_buf = nullptr;
    hunk_diff_right_buf = nullptr;
    set_info_msg("");
}

void VimEngine::handle_hunk_diff_input(const ncinput& ni, uint32_t key) {
    auto& buf = (hunk_diff_is_delta && hunk_diff_left_buf) ? *hunk_diff_left_buf : active_buf();
    auto& win = active_win();
    const auto& diff = hunk_diff_diff;

    if (is_esc(ni, key)) {
        if (manual_left_active || manual_right_active) {
            manual_left_active = false;
            manual_right_active = false;
            manual_left_start = -1;
            manual_left_end = -1;
            manual_right_start = -1;
            manual_right_end = -1;
            hunk_diff_status_msg = "Manual markers cleared";
            return;
        }
        close_hunk_diff();
        return;
    }

    if (key == 'q' || key == 'Q') {
        close_hunk_diff();
        return;
    }

    if (key == 'm') {
        if (hunk_diff_focus == "left") {
            manual_left_start = hunk_diff_cursor_row;
            manual_left_end = hunk_diff_cursor_row;
            manual_left_active = true;
            hunk_diff_status_msg = "Left marker start: row " + std::to_string(hunk_diff_cursor_row + 1) + " (move & press M for end)";
        } else {
            manual_right_start = hunk_diff_cursor_row;
            manual_right_end = hunk_diff_cursor_row;
            manual_right_active = true;
            hunk_diff_status_msg = "Right marker start: row " + std::to_string(hunk_diff_cursor_row + 1) + " (move & press M for end)";
        }
        return;
    }

    if (key == 'M' || (ni.shift && (key == 'm' || ni.id == 'm' || ni.id == 'M'))) {
        if (hunk_diff_focus == "left") {
            if (manual_left_start < 0) manual_left_start = hunk_diff_cursor_row;
            manual_left_end = hunk_diff_cursor_row;
            manual_left_active = true;
            int r1 = std::min(manual_left_start, manual_left_end) + 1;
            int r2 = std::max(manual_left_start, manual_left_end) + 1;
            hunk_diff_status_msg = "Left marker set: rows " + std::to_string(r1) + ".." + std::to_string(r2) + " (press 'a' to merge)";
        } else {
            if (manual_right_start < 0) manual_right_start = hunk_diff_cursor_row;
            manual_right_end = hunk_diff_cursor_row;
            manual_right_active = true;
            int r1 = std::min(manual_right_start, manual_right_end) + 1;
            int r2 = std::max(manual_right_start, manual_right_end) + 1;
            hunk_diff_status_msg = "Right marker set: rows " + std::to_string(r1) + ".." + std::to_string(r2) + " (press 'a' to merge)";
        }
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

    if (key == 'l' || is_fkey(ni, key, 3)) {
        hunk_diff_cursor_row = diff.next_hunk_row(hunk_diff_cursor_row);
        hunk_diff_status_msg.clear();
        return;
    }

    if (key == 'L' || (ni.shift && (key == 'l' || ni.id == 'l' || ni.id == 'L')) || is_fkey(ni, key, 2)) {
        hunk_diff_cursor_row = diff.prev_hunk_row(hunk_diff_cursor_row);
        hunk_diff_status_msg.clear();
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
            hunk_diff_status_msg = "Yanked line from " + (hunk_diff_is_delta ? hunk_diff_left_name : "Working");
        } else {
            if (row.right_idx < 0 || row.right_idx >= static_cast<int>(diff.right_lines.size())) {
                hunk_diff_status_msg = "Cannot yank padding row";
                return;
            }
            std::string line = diff.right_lines[row.right_idx];
            yank_reg.is_linewise = true;
            yank_reg.lines = {line};
            yank_reg.text = line + "\n";
            hunk_diff_status_msg = "Yanked line from " + (hunk_diff_is_delta ? hunk_diff_right_name : "HEAD");
        }
        return;
    }

    if (key == 'p') {
        if (hunk_diff_focus == "right" && !hunk_diff_is_delta) {
            hunk_diff_status_msg = "Cannot paste into HEAD: HEAD is read-only";
            return;
        }
        if (yank_reg.lines.empty() || !yank_reg.is_linewise) {
            hunk_diff_status_msg = "Clipboard empty or not line content";
            return;
        }
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;
        const auto& row = diff.rows[hunk_diff_cursor_row];

        if (hunk_diff_focus == "right" && hunk_diff_is_delta && hunk_diff_right_buf) {
            int target_y = -1;
            if (row.right_idx >= 0) {
                target_y = row.right_idx + 1;
            } else {
                for (int r = hunk_diff_cursor_row - 1; r >= 0; --r) {
                    if (diff.rows[r].right_idx >= 0) {
                        target_y = diff.rows[r].right_idx + 1;
                        break;
                    }
                }
                if (target_y == -1) target_y = 0;
            }
            hunk_diff_right_buf->push_undo(win.cursors);
            int ins_pos = std::clamp(target_y, 0, static_cast<int>(hunk_diff_right_buf->lines.size()));
            hunk_diff_right_buf->lines.insert(hunk_diff_right_buf->lines.begin() + ins_pos, yank_reg.lines.begin(), yank_reg.lines.end());
            hunk_diff_right_buf->modified = true;
            hunk_diff_right_buf->version++;
            hunk_diff_right_buf->invalidate_hunks();
            if (hunk_diff_right_buf->syntax) hunk_diff_right_buf->syntax->update_text(hunk_diff_right_buf->lines);
            hunk_diff_head_lines = hunk_diff_right_buf->lines;
            if (hunk_diff_head_syntax) hunk_diff_head_syntax->update_text(hunk_diff_head_lines);
            hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
            hunk_diff_status_msg = "Pasted line into " + hunk_diff_right_name;
            return;
        }

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
        hunk_diff_status_msg = "Pasted line below cursor in " + (hunk_diff_is_delta ? hunk_diff_left_name : "working buffer");
        return;
    }

    if (key == 'd') {
        if (hunk_diff_focus == "right" && !hunk_diff_is_delta) {
            hunk_diff_status_msg = "Cannot delete from HEAD: HEAD is read-only";
            return;
        }
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;
        const auto& row = diff.rows[hunk_diff_cursor_row];

        if (hunk_diff_focus == "right" && hunk_diff_is_delta && hunk_diff_right_buf) {
            if (row.right_idx < 0 || row.right_idx >= static_cast<int>(hunk_diff_right_buf->lines.size())) {
                hunk_diff_status_msg = "Cannot delete padding row";
                return;
            }
            hunk_diff_right_buf->push_undo(win.cursors);
            int del_y = row.right_idx;
            if (hunk_diff_right_buf->lines.size() > 1) {
                hunk_diff_right_buf->lines.erase(hunk_diff_right_buf->lines.begin() + del_y);
            } else {
                hunk_diff_right_buf->lines[0] = "";
            }
            hunk_diff_right_buf->modified = true;
            hunk_diff_right_buf->version++;
            hunk_diff_right_buf->invalidate_hunks();
            if (hunk_diff_right_buf->syntax) hunk_diff_right_buf->syntax->update_text(hunk_diff_right_buf->lines);
            hunk_diff_head_lines = hunk_diff_right_buf->lines;
            if (hunk_diff_head_syntax) hunk_diff_head_syntax->update_text(hunk_diff_head_lines);
            hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
            hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
            hunk_diff_status_msg = "Deleted line from " + hunk_diff_right_name;
            return;
        }

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
        hunk_diff_status_msg = "Deleted line from " + (hunk_diff_is_delta ? hunk_diff_left_name : "working buffer");
        return;
    }

    if (key == 'a' || key == 'A') {
        if (hunk_diff_cursor_row < 0 || hunk_diff_cursor_row >= static_cast<int>(diff.rows.size())) return;

        bool is_push = (key == 'a');
        bool left_to_right = (hunk_diff_focus == "left" && is_push) ||
                             (hunk_diff_focus == "right" && !is_push);

        if (left_to_right && (!hunk_diff_is_delta || !hunk_diff_right_buf)) {
            hunk_diff_status_msg = "Cannot modify HEAD: HEAD is read-only";
            return;
        }

        bool in_left_marker = manual_left_active &&
            (hunk_diff_cursor_row >= std::min(manual_left_start, manual_left_end) &&
             hunk_diff_cursor_row <= std::max(manual_left_start, manual_left_end));

        bool in_right_marker = manual_right_active &&
            (hunk_diff_cursor_row >= std::min(manual_right_start, manual_right_end) &&
             hunk_diff_cursor_row <= std::max(manual_right_start, manual_right_end));

        bool in_marker = (hunk_diff_focus == "left" && in_left_marker) ||
                         (hunk_diff_focus == "right" && in_right_marker);

        if (in_marker) {
            int l_r1 = manual_left_active ? std::min(manual_left_start, manual_left_end)
                                          : std::min(manual_right_start, manual_right_end);
            int l_r2 = manual_left_active ? std::max(manual_left_start, manual_left_end)
                                          : std::max(manual_right_start, manual_right_end);

            int r_r1 = manual_right_active ? std::min(manual_right_start, manual_right_end)
                                           : std::min(manual_left_start, manual_left_end);
            int r_r2 = manual_right_active ? std::max(manual_right_start, manual_right_end)
                                           : std::max(manual_left_start, manual_left_end);

            int left_line_start = -1;
            int left_line_end = -1;
            for (int r = l_r1; r <= l_r2 && r < static_cast<int>(diff.rows.size()); ++r) {
                int idx = diff.rows[r].left_idx;
                if (idx >= 0) {
                    if (left_line_start == -1 || idx < left_line_start) left_line_start = idx;
                    if (left_line_end == -1 || idx > left_line_end) left_line_end = idx;
                }
            }
            if (left_line_start == -1) {
                for (int r = l_r1 - 1; r >= 0; --r) {
                    if (diff.rows[r].left_idx >= 0) {
                        left_line_start = diff.rows[r].left_idx + 1;
                        left_line_end = left_line_start - 1;
                        break;
                    }
                }
                if (left_line_start == -1) {
                    left_line_start = 0;
                    left_line_end = -1;
                }
            }

            std::vector<std::string> right_lines_to_merge;
            for (int r = r_r1; r <= r_r2 && r < static_cast<int>(diff.rows.size()); ++r) {
                int idx = diff.rows[r].right_idx;
                if (idx >= 0 && idx < static_cast<int>(diff.right_lines.size())) {
                    right_lines_to_merge.push_back(diff.right_lines[idx]);
                }
            }

            buf.push_undo(win.cursors);
            int erase_cnt = (left_line_end >= left_line_start) ? (left_line_end - left_line_start + 1) : 0;
            if (left_line_start < static_cast<int>(buf.lines.size()) && erase_cnt > 0) {
                int actual_erase = std::min(erase_cnt, static_cast<int>(buf.lines.size()) - left_line_start);
                buf.lines.erase(buf.lines.begin() + left_line_start, buf.lines.begin() + left_line_start + actual_erase);
            }
            if (!right_lines_to_merge.empty()) {
                int ins_pos = std::clamp(left_line_start, 0, static_cast<int>(buf.lines.size()));
                buf.lines.insert(buf.lines.begin() + ins_pos, right_lines_to_merge.begin(), right_lines_to_merge.end());
            }
            if (buf.lines.empty()) buf.lines.push_back("");
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
            manual_left_active = false;
            manual_right_active = false;
            hunk_diff_status_msg = "Manual merge: Replaced with " + std::to_string(right_lines_to_merge.size()) + " lines from HEAD";
            hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
            return;
        }

        const auto& row = diff.rows[hunk_diff_cursor_row];
        if (row.hunk_idx == -1 || row.hunk_idx >= static_cast<int>(diff.hunks.size())) {
            hunk_diff_status_msg = "No hunk at cursor to apply";
            return;
        }
        const auto& hunk = diff.hunks[row.hunk_idx];

        if (left_to_right && hunk_diff_is_delta && hunk_diff_right_buf) {
            hunk_diff_right_buf->push_undo(win.cursors);
            int r_start = hunk.right_start;
            int r_count = hunk.right_count;
            if (r_start < static_cast<int>(hunk_diff_right_buf->lines.size())) {
                int erase_cnt = std::min(r_count, static_cast<int>(hunk_diff_right_buf->lines.size()) - r_start);
                hunk_diff_right_buf->lines.erase(hunk_diff_right_buf->lines.begin() + r_start,
                                                 hunk_diff_right_buf->lines.begin() + r_start + erase_cnt);
            }
            if (!hunk.left_lines.empty()) {
                int ins_pos = std::min(r_start, static_cast<int>(hunk_diff_right_buf->lines.size()));
                hunk_diff_right_buf->lines.insert(hunk_diff_right_buf->lines.begin() + ins_pos,
                                                  hunk.left_lines.begin(), hunk.left_lines.end());
            }
            if (hunk_diff_right_buf->lines.empty()) hunk_diff_right_buf->lines.push_back("");
            hunk_diff_right_buf->modified = true;
            hunk_diff_right_buf->version++;
            hunk_diff_right_buf->invalidate_hunks();
            if (hunk_diff_right_buf->syntax) hunk_diff_right_buf->syntax->update_text(hunk_diff_right_buf->lines);
            hunk_diff_head_lines = hunk_diff_right_buf->lines;
            if (hunk_diff_head_syntax) hunk_diff_head_syntax->update_text(hunk_diff_head_lines);
            hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
            hunk_diff_status_msg = "Pushed hunk #" + std::to_string(hunk.id + 1) + " from " +
                                   hunk_diff_left_name + " → " + hunk_diff_right_name;
            hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
            return;
        }

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
        hunk_diff_status_msg = hunk_diff_is_delta ?
            ("Pulled hunk #" + std::to_string(hunk.id + 1) + " from " + hunk_diff_right_name + " → " + hunk_diff_left_name) :
            ("Applied hunk #" + std::to_string(hunk.id + 1) + " from HEAD to working");
        hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
        return;
    }

    if (key == 'u') {
        if (hunk_diff_is_delta && hunk_diff_right_buf && hunk_diff_focus == "right") {
            if (hunk_diff_right_buf->undo(win.cursors)) {
                hunk_diff_head_lines = hunk_diff_right_buf->lines;
                if (hunk_diff_head_syntax) hunk_diff_head_syntax->update_text(hunk_diff_head_lines);
                hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
                hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
                hunk_diff_status_msg = "Undo applied to " + hunk_diff_right_name;
            } else {
                hunk_diff_status_msg = "Already at oldest change for " + hunk_diff_right_name;
            }
            return;
        }

        if (buf.undo(win.cursors)) {
            hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
            hunk_diff_cursor_row = std::clamp(hunk_diff_cursor_row, 0, std::max(0, static_cast<int>(hunk_diff_diff.rows.size()) - 1));
            hunk_diff_status_msg = "Undo applied to " + (hunk_diff_is_delta ? hunk_diff_left_name : "working");
        } else {
            hunk_diff_status_msg = "Already at oldest change";
        }
        return;
    }

    if (key == 'w' || is_ctrl(ni, key, 's')) {
        if (hunk_diff_is_delta && hunk_diff_right_buf && hunk_diff_focus == "right") {
            if (hunk_diff_right_buf->save_to_file()) {
                hunk_diff_status_msg = "\"" + hunk_diff_right_buf->name + "\" written";
            } else {
                hunk_diff_status_msg = "E212: Can't open file for writing";
            }
        } else {
            if (buf.save_to_file()) {
                hunk_diff_status_msg = "\"" + buf.name + "\" written";
            } else {
                hunk_diff_status_msg = "E212: Can't open file for writing";
            }
        }
        return;
    }
}