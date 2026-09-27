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

void VimEngine::rebuild_git_status_rows() {
    git_status_rows.clear();

    // Section 1: Stage Changes
    git_status_rows.push_back({GitStatusRow::HEADER, 0, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.staged.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 0, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& f : git_view_data.staged) {
            git_status_rows.push_back({GitStatusRow::STAGE_FILE, 0, f.path, f.glyph, 0, "", "", "", false, "", ""});
        }
    }

    // Section 2: Unstage Changes
    git_status_rows.push_back({GitStatusRow::HEADER, 1, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.unstaged.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 1, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& f : git_view_data.unstaged) {
            git_status_rows.push_back({GitStatusRow::UNSTAGE_FILE, 1, f.path, f.glyph, 0, "", "", "", false, "", ""});
        }
    }

    // Section 3: Untracked Files
    git_status_rows.push_back({GitStatusRow::HEADER, 2, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.untracked.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 2, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& f : git_view_data.untracked) {
            git_status_rows.push_back({GitStatusRow::UNTRACKED_FILE, 2, f.path, f.glyph, 0, "", "", "", false, "", ""});
        }
    }

    // Section 4: Last Commit
    git_status_rows.push_back({GitStatusRow::HEADER, 3, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.recent_commits.empty() || git_view_data.recent_commits[0].files.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 3, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& f : git_view_data.recent_commits[0].files) {
            git_status_rows.push_back({GitStatusRow::COMMIT1_FILE, 3, f.path, f.glyph, 0, "", "", "", false, "", git_view_data.recent_commits[0].hash});
        }
    }

    // Section 5: Last Commit -1
    git_status_rows.push_back({GitStatusRow::HEADER, 4, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.recent_commits.size() < 2 || git_view_data.recent_commits[1].files.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 4, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& f : git_view_data.recent_commits[1].files) {
            git_status_rows.push_back({GitStatusRow::COMMIT2_FILE, 4, f.path, f.glyph, 0, "", "", "", false, "", git_view_data.recent_commits[1].hash});
        }
    }

    // Section 6: Stashes
    git_status_rows.push_back({GitStatusRow::HEADER, 5, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.stashes.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 5, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& s : git_view_data.stashes) {
            git_status_rows.push_back({GitStatusRow::STASH, 5, "", ' ', s.index, s.ref, s.subject, "", false, "", ""});
        }
    }

    // Section 7: Branches
    git_status_rows.push_back({GitStatusRow::HEADER, 6, "", ' ', 0, "", "", "", false, "", ""});
    if (git_view_data.branches.empty()) {
        git_status_rows.push_back({GitStatusRow::PLACEHOLDER, 6, "", ' ', 0, "", "", "", false, "", ""});
    } else {
        for (const auto& b : git_view_data.branches) {
            git_status_rows.push_back({GitStatusRow::BRANCH, 6, "", ' ', 0, "", "", b.name, b.is_current, b.reltime, ""});
        }
    }

    if (git_status_cursor >= static_cast<int>(git_status_rows.size())) {
        git_status_cursor = std::max(0, static_cast<int>(git_status_rows.size()) - 1);
    }
}

void VimEngine::refresh_git_status() {
    std::string root = detect_git_repo_root(!project_dir.empty() ? project_dir : ".");
    if (root.empty()) root = project_dir;
    git_view_data = query_git_view_data(root);
    rebuild_git_status_rows();
    refresh_git_status_right();
}

void VimEngine::refresh_git_status_right() {
    git_status_right_lines.clear();
    git_status_right_scroll_y = 0;
    if (git_status_rows.empty() || git_status_cursor < 0 || git_status_cursor >= static_cast<int>(git_status_rows.size())) {
        return;
    }

    const auto& row = git_status_rows[git_status_cursor];
    std::string root = detect_git_repo_root(!project_dir.empty() ? project_dir : ".");
    if (root.empty()) root = project_dir;

    std::vector<std::string> left;
    std::vector<std::string> right;

    if (row.kind == GitStatusRow::STAGE_FILE) {
        left = git_get_file_lines(root, "HEAD", row.path);
        right = git_get_file_lines(root, "INDEX", row.path);
    } else if (row.kind == GitStatusRow::UNSTAGE_FILE) {
        left = git_get_file_lines(root, "INDEX", row.path);
        right = git_get_file_lines(root, "WORKING", row.path);
    } else if (row.kind == GitStatusRow::UNTRACKED_FILE) {
        left.clear();
        right = git_get_file_lines(root, "WORKING", row.path);
    } else if (row.kind == GitStatusRow::COMMIT1_FILE) {
        if (!git_view_data.recent_commits.empty()) {
            std::string h = git_view_data.recent_commits[0].hash;
            left = git_get_file_lines(root, h + "~1", row.path);
            right = git_get_file_lines(root, h, row.path);
        }
    } else if (row.kind == GitStatusRow::COMMIT2_FILE) {
        if (git_view_data.recent_commits.size() >= 2) {
            std::string h = git_view_data.recent_commits[1].hash;
            left = git_get_file_lines(root, h + "~1", row.path);
            right = git_get_file_lines(root, h, row.path);
        }
    } else {
        return;
    }

    auto hunks = compute_myers_diff(left, right);
    if (hunks.empty()) {
        return;
    }

    git_status_right_lines.push_back({GitUnifiedLine::META, "diff --git a/" + row.path + " b/" + row.path});
    git_status_right_lines.push_back({GitUnifiedLine::META, "--- a/" + row.path});
    git_status_right_lines.push_back({GitUnifiedLine::META, "+++ b/" + row.path});

    const int CTX = 3;
    int last_orig_end = 0;

    for (size_t i = 0; i < hunks.size(); ++i) {
        const auto& h = hunks[i];
        int ctx_before_start = std::max(0, h.orig_start - CTX);

        if (i > 0) {
            if (ctx_before_start > last_orig_end) {
                git_status_right_lines.push_back({GitUnifiedLine::ELLIPSIS, " ..."});
            } else {
                ctx_before_start = last_orig_end;
            }
        }

        for (int l = ctx_before_start; l < h.orig_start && l < static_cast<int>(left.size()); ++l) {
            git_status_right_lines.push_back({GitUnifiedLine::CONTEXT, " " + left[l]});
        }

        char hhdr[128];
        snprintf(hhdr, sizeof(hhdr), "@@ -%d,%d +%d,%d @@",
                 h.orig_start + 1, std::max(1, h.orig_count),
                 h.cur_start + 1, std::max(1, h.cur_count));
        git_status_right_lines.push_back({GitUnifiedLine::HUNK_HDR, std::string(hhdr)});

        for (const auto& rl : h.orig_lines) {
            git_status_right_lines.push_back({GitUnifiedLine::REMOVED, "-" + rl});
        }
        for (const auto& al : h.cur_lines) {
            git_status_right_lines.push_back({GitUnifiedLine::ADDED, "+" + al});
        }

        int h_end = h.orig_start + h.orig_count;
        int ctx_after_end = std::min(static_cast<int>(left.size()), h_end + CTX);
        if (i + 1 < hunks.size()) {
            ctx_after_end = std::min(ctx_after_end, hunks[i + 1].orig_start);
        }
        for (int l = h_end; l < ctx_after_end; ++l) {
            git_status_right_lines.push_back({GitUnifiedLine::CONTEXT, " " + left[l]});
        }
        last_orig_end = ctx_after_end;
    }
}

void VimEngine::open_git_status() {
    show_git_status = true;
    git_status_cursor = 0;
    git_status_scroll_y = 0;
    git_status_msg.clear();
    git_stash_action_active = false;
    git_stash_status_msg.clear();
    refresh_git_status();
}

void VimEngine::close_git_status() {
    show_git_status = false;
    git_stash_action_active = false;
    git_status_msg.clear();
    git_stash_status_msg.clear();
}

void VimEngine::render_git_status(unsigned int screen_h, unsigned int screen_w) {
    if (screen_h < 6 || screen_w < 30) return;

    int popup_x = 0;
    int popup_y = 0;
    int popup_w = static_cast<int>(screen_w);
    int popup_h = std::max(6, static_cast<int>(screen_h) - 2);

    int list_start_y = popup_y + 1;
    int visible_rows = popup_h - 2;

    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    int left_w = std::clamp(static_cast<int>(screen_w * 0.40), 24, std::max(24, static_cast<int>(screen_w) - 24));
    int divider_x = popup_x + left_w;
    int right_x = divider_x + 1;
    int right_w = (popup_x + popup_w - 1) - right_x;

    // Borders
    ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
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
    std::string title = " Git Status View (F1 / F6) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    // Divider intersection
    ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
    ncplane_putstr_yx(stdplane, popup_y, divider_x, "┬");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, divider_x, "┴");

    // Scrolling for left list
    if (git_status_cursor < git_status_scroll_y) {
        git_status_scroll_y = git_status_cursor;
    }
    if (git_status_cursor >= git_status_scroll_y + visible_rows) {
        git_status_scroll_y = git_status_cursor - visible_rows + 1;
    }
    git_status_scroll_y = std::max(0, git_status_scroll_y);

    auto get_section_title = [&](int s_idx) -> std::string {
        switch (s_idx) {
            case 0: return "Stage Changes (" + std::to_string(git_view_data.staged.size()) + ")";
            case 1: return "Unstage Changes (" + std::to_string(git_view_data.unstaged.size()) + ")";
            case 2: return "Untracked Files (" + std::to_string(git_view_data.untracked.size()) + ")";
            case 3:
                return !git_view_data.recent_commits.empty() ?
                    ("Last Commit [" + git_view_data.recent_commits[0].hash + "] (" + std::to_string(git_view_data.recent_commits[0].files.size()) + ")") :
                    "Last Commit (0)";
            case 4:
                return git_view_data.recent_commits.size() >= 2 ?
                    ("Last Commit -1 [" + git_view_data.recent_commits[1].hash + "] (" + std::to_string(git_view_data.recent_commits[1].files.size()) + ")") :
                    "Last Commit -1 (0)";
            case 5: return "Stashes (" + std::to_string(git_view_data.stashes.size()) + ")";
            case 6: return "------ Branches ------";
            default: return "";
        }
    };

    auto set_section_color = [&](int s_idx) {
        switch (s_idx) {
            case 0: ncplane_set_fg_rgb8(stdplane, 80, 220, 100); break; // Green
            case 1: ncplane_set_fg_rgb8(stdplane, 240, 200, 80); break; // Yellow
            case 2: ncplane_set_fg_rgb8(stdplane, 80, 200, 240); break; // Cyan
            case 3:
            case 4: ncplane_set_fg_rgb8(stdplane, 100, 180, 255); break; // Blue
            case 5: ncplane_set_fg_rgb8(stdplane, 230, 130, 255); break; // Fuchsia
            case 6: ncplane_set_fg_rgb8(stdplane, 140, 150, 160); break; // Grey
            default: ncplane_set_fg_rgb8(stdplane, 220, 220, 220); break;
        }
    };

    // Draw Left panel rows
    for (int r = 0; r < visible_rows; ++r) {
        int row_idx = git_status_scroll_y + r;
        int draw_y = list_start_y + r;

        // Divider column
        ncplane_set_fg_rgb8(stdplane, 65, 75, 95);
        ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
        ncplane_putstr_yx(stdplane, draw_y, divider_x, "│");

        if (row_idx >= static_cast<int>(git_status_rows.size())) {
            continue;
        }

        const auto& row = git_status_rows[row_idx];
        bool is_cursor = (row_idx == git_status_cursor);

        if (is_cursor) {
            ncplane_set_bg_rgb8(stdplane, 45, 65, 115);
            ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        } else {
            ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
        }

        for (int c = popup_x + 1; c < divider_x; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        int text_x = popup_x + 2;
        int max_len = divider_x - text_x - 1;

        if (is_cursor) {
            ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            ncplane_putstr_yx(stdplane, draw_y, text_x, "▶ ");
            text_x += 2;
            max_len -= 2;
        } else {
            ncplane_putstr_yx(stdplane, draw_y, text_x, "  ");
            text_x += 2;
            max_len -= 2;
        }

        if (row.kind == GitStatusRow::HEADER) {
            if (!is_cursor) set_section_color(row.section_idx);
            ncplane_on_styles(stdplane, NCSTYLE_BOLD);
            std::string stitle = get_section_title(row.section_idx);
            if (static_cast<int>(stitle.size()) > max_len && max_len > 0) stitle = stitle.substr(0, max_len);
            ncplane_putstr_yx(stdplane, draw_y, text_x, stitle.c_str());
            ncplane_off_styles(stdplane, NCSTYLE_BOLD);
        } else if (row.kind == GitStatusRow::PLACEHOLDER) {
            if (!is_cursor) ncplane_set_fg_rgb8(stdplane, 100, 105, 115);
            ncplane_putstr_yx(stdplane, draw_y, text_x + 2, "(none)");
        } else if (row.kind == GitStatusRow::STAGE_FILE || row.kind == GitStatusRow::UNSTAGE_FILE ||
                   row.kind == GitStatusRow::COMMIT1_FILE || row.kind == GitStatusRow::COMMIT2_FILE) {
            if (!is_cursor) {
                if (row.glyph == 'M') ncplane_set_fg_rgb8(stdplane, 240, 200, 80);
                else if (row.glyph == 'A') ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                else if (row.glyph == 'D') ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                else ncplane_set_fg_rgb8(stdplane, 180, 190, 205);
            }
            std::string glyph_str = std::string(1, row.glyph) + "  ";
            ncplane_putstr_yx(stdplane, draw_y, text_x + 1, glyph_str.c_str());

            if (!is_cursor) ncplane_set_fg_rgb8(stdplane, 220, 225, 235);
            std::string p = row.path;
            if (static_cast<int>(p.size()) > max_len - 3 && max_len > 3) p = "..." + p.substr(p.size() - (max_len - 6));
            ncplane_putstr_yx(stdplane, draw_y, text_x + 4, p.c_str());
        } else if (row.kind == GitStatusRow::UNTRACKED_FILE) {
            if (!is_cursor) ncplane_set_fg_rgb8(stdplane, 80, 200, 240);
            ncplane_putstr_yx(stdplane, draw_y, text_x + 1, "?  ");
            if (!is_cursor) ncplane_set_fg_rgb8(stdplane, 210, 215, 225);
            std::string p = row.path;
            if (static_cast<int>(p.size()) > max_len - 3 && max_len > 3) p = "..." + p.substr(p.size() - (max_len - 6));
            ncplane_putstr_yx(stdplane, draw_y, text_x + 4, p.c_str());
        } else if (row.kind == GitStatusRow::STASH) {
            if (!is_cursor) ncplane_set_fg_rgb8(stdplane, 230, 130, 255);
            std::string sline = row.stash_ref + "  " + row.stash_subject;
            if (static_cast<int>(sline.size()) > max_len && max_len > 0) sline = sline.substr(0, max_len);
            ncplane_putstr_yx(stdplane, draw_y, text_x + 1, sline.c_str());
        } else if (row.kind == GitStatusRow::BRANCH) {
            if (!is_cursor) {
                if (row.is_current_branch) ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                else ncplane_set_fg_rgb8(stdplane, 200, 205, 215);
            }
            std::string btext = (row.is_current_branch ? "* " : "  ") + row.branch_name;
            if (!row.reltime.empty()) btext += "  (" + row.reltime + ")";
            if (static_cast<int>(btext.size()) > max_len && max_len > 0) btext = btext.substr(0, max_len);
            ncplane_putstr_yx(stdplane, draw_y, text_x, btext.c_str());
        }
    }

    // Draw Right panel (Unified diff)
    for (int r = 0; r < visible_rows; ++r) {
        int draw_y = list_start_y + r;
        int d_idx = git_status_right_scroll_y + r;

        ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
        for (int c = right_x; c < popup_x + popup_w - 1; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        if (d_idx >= static_cast<int>(git_status_right_lines.size())) {
            continue;
        }

        const auto& dl = git_status_right_lines[d_idx];
        if (dl.type == GitUnifiedLine::REMOVED) {
            ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
            ncplane_set_bg_rgb8(stdplane, 38, 24, 26);
        } else if (dl.type == GitUnifiedLine::ADDED) {
            ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
            ncplane_set_bg_rgb8(stdplane, 20, 36, 26);
        } else if (dl.type == GitUnifiedLine::HUNK_HDR) {
            ncplane_set_fg_rgb8(stdplane, 80, 200, 240);
            ncplane_set_bg_rgb8(stdplane, 22, 28, 38);
            ncplane_on_styles(stdplane, NCSTYLE_BOLD);
        } else if (dl.type == GitUnifiedLine::ELLIPSIS || dl.type == GitUnifiedLine::META) {
            ncplane_set_fg_rgb8(stdplane, 130, 140, 155);
            ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
        } else {
            ncplane_set_fg_rgb8(stdplane, 210, 215, 225);
            ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
        }

        for (int c = right_x; c < popup_x + popup_w - 1; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        std::string txt = dl.text;
        int max_w = (popup_x + popup_w - 1) - (right_x + 1);
        if (static_cast<int>(txt.size()) > max_w && max_w > 0) {
            txt = txt.substr(0, max_w);
        }
        ncplane_putstr_yx(stdplane, draw_y, right_x + 1, txt.c_str());

        if (dl.type == GitUnifiedLine::HUNK_HDR) {
            ncplane_off_styles(stdplane, NCSTYLE_BOLD);
        }
    }

    // Footer Status row
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    if (git_stash_action_active) {
        std::string prompt_str = " " + git_stash_action_ref + "   ";
        if (!git_stash_status_msg.empty()) {
            prompt_str += git_stash_status_msg + "  —  ";
        }
        prompt_str += "[y]pop   [d]drop   [q]uit ";
        ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 2, prompt_str.c_str());

        int cur_x = popup_x + 2 + static_cast<int>(prompt_str.size());
        notcurses_cursor_enable(nc, popup_y + popup_h - 1, cur_x);
    } else {
        std::string footer_left = " git status ";
        if (!git_status_msg.empty()) {
            footer_left = " " + git_status_msg + " ";
        }
        std::string footer_right = " [J/K] section  [s] stage  [z] stash  [Enter] open  [r] refresh  [q] close ";
        ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 2, footer_left.c_str());

        int rx = popup_x + popup_w - 1 - static_cast<int>(footer_right.size());
        if (rx > popup_x + 2 + static_cast<int>(footer_left.size())) {
            ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
            ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, rx, footer_right.c_str());
        }
    }
}

void VimEngine::open_hunk_diff() {
    auto& buf = active_buf();
    if (!buf.is_git_repo || !buf.git_tracked) {
        set_info_msg("No HEAD version for this file");
        return;
    }

    hunk_diff_is_delta = false;
    hunk_diff_left_buf = nullptr;
    hunk_diff_right_buf = nullptr;
    hunk_diff_left_name = !buf.name.empty() ? buf.name : "buffer";
    hunk_diff_right_name = "HEAD";

    hunk_diff_head_lines = buf.git_base_lines;
    hunk_diff_head_syntax = std::make_shared<SyntaxHighlighter>();
    hunk_diff_head_syntax->init_for_file(!buf.file_path.empty() ? buf.file_path : buf.name);
    hunk_diff_head_syntax->update_text(hunk_diff_head_lines);

    hunk_diff_diff = compute_aligned_diff(buf.lines, hunk_diff_head_lines);
    hunk_diff_focus = "left";
    hunk_diff_status_msg.clear();

    manual_left_active = false;
    manual_left_start = -1;
    manual_left_end = -1;
    manual_right_active = false;
    manual_right_start = -1;
    manual_right_end = -1;

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

void VimEngine::open_delta_diff(std::shared_ptr<TextBuffer> left_buf, std::shared_ptr<TextBuffer> right_buf) {
    if (!left_buf || !right_buf) {
        set_info_msg("Delta diff requires two files");
        return;
    }

    show_filepicker = false;
    show_settings_popup = false;
    show_git_hunk_popup = false;
    show_whichkey_popup = false;
    show_buffer_list = false;
    show_rg_popup = false;
    show_git_status = false;

    hunk_diff_is_delta = true;
    hunk_diff_left_buf = left_buf;
    hunk_diff_right_buf = right_buf;
    hunk_diff_left_name = !left_buf->name.empty() ? left_buf->name : left_buf->file_path;
    hunk_diff_right_name = !right_buf->name.empty() ? right_buf->name : right_buf->file_path;

    hunk_diff_head_lines = right_buf->lines;
    hunk_diff_head_syntax = std::make_shared<SyntaxHighlighter>();
    hunk_diff_head_syntax->init_for_file(!right_buf->file_path.empty() ? right_buf->file_path : right_buf->name);
    hunk_diff_head_syntax->update_text(hunk_diff_head_lines);

    hunk_diff_diff = compute_aligned_diff(left_buf->lines, right_buf->lines);
    hunk_diff_focus = "left";
    hunk_diff_status_msg.clear();

    manual_left_active = false;
    manual_left_start = -1;
    manual_left_end = -1;
    manual_right_active = false;
    manual_right_start = -1;
    manual_right_end = -1;

    hunk_diff_cursor_row = 0;
    if (!hunk_diff_diff.hunks.empty()) {
        hunk_diff_cursor_row = hunk_diff_diff.hunks.front().first_row;
    }
    hunk_diff_scroll_y = 0;
    show_hunk_diff = true;

    set_info_msg("Delta: " + hunk_diff_left_name + " ↔ " + hunk_diff_right_name +
                 " (" + std::to_string(hunk_diff_diff.hunks.size()) + " hunks)");
}

void VimEngine::open_file_diff(const std::string& path1, const std::string& path2) {
    if (path1.empty() || path2.empty()) {
        set_info_msg("Delta diff requires two files: :delta <file1> <file2>");
        return;
    }

    std::string full_p1 = path1;
    std::string full_p2 = path2;
    std::string root = !project_dir.empty() ? project_dir : ".";

    std::error_code ec;
    if (fs::exists(fs::path(root) / path1, ec)) {
        full_p1 = (fs::path(root) / path1).lexically_normal().string();
    }
    if (fs::exists(fs::path(root) / path2, ec)) {
        full_p2 = (fs::path(root) / path2).lexically_normal().string();
    }

    std::shared_ptr<TextBuffer> b1 = nullptr;
    std::shared_ptr<TextBuffer> b2 = nullptr;

    for (const auto& b : buffers) {
        if (b->file_path == full_p1 || b->name == path1 || b->file_path == path1) {
            b1 = b;
        }
        if (b->file_path == full_p2 || b->name == path2 || b->file_path == path2) {
            b2 = b;
        }
    }

    if (!b1) {
        b1 = TextBuffer::from_file(full_p1);
        buffers.push_back(b1);
    }
    if (!b2) {
        b2 = TextBuffer::from_file(full_p2);
        buffers.push_back(b2);
    }

    open_delta_diff(b1, b2);
}

void VimEngine::render_hunk_diff(unsigned int screen_h, unsigned int screen_w) {
    if (screen_h < 6 || screen_w < 30) return;

    int popup_x = 0;
    int popup_y = 0;
    int popup_w = static_cast<int>(screen_w);
    int popup_h = std::max(6, static_cast<int>(screen_h) - 2);

    int header_sep_y = popup_y + 2;
    int list_start_y = header_sep_y + 1;
    int visible_rows = popup_h - (list_start_y - popup_y) - 1;

    // Background fill (following F11 vg popup style)
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox) in F11 soft cyan/blue
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

    // Title on top border (following F11 vg title style)
    std::string title = hunk_diff_is_delta ?
        (" Delta Diff: " + hunk_diff_left_name + " ↔ " + hunk_diff_right_name + " ") :
        " Hunk Diff View (F5 / :diff) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    // Header info (row popup_y + 1)
    auto& buf = (hunk_diff_is_delta && hunk_diff_left_buf) ? *hunk_diff_left_buf : active_buf();
    std::string disp_file = !buf.name.empty() ? buf.name : "buffer";
    ncplane_set_fg_rgb8(stdplane, 240, 200, 80);
    if (hunk_diff_is_delta) {
        ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "📄 Compare: ");
        ncplane_set_fg_rgb8(stdplane, 100, 220, 255);
        ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 14, hunk_diff_left_name.c_str());
        int mid_x = popup_x + 14 + static_cast<int>(hunk_diff_left_name.size()) + 1;
        ncplane_set_fg_rgb8(stdplane, 180, 180, 190);
        ncplane_putstr_yx(stdplane, popup_y + 1, mid_x, "↔");
        ncplane_set_fg_rgb8(stdplane, 255, 140, 180);
        ncplane_putstr_yx(stdplane, popup_y + 1, mid_x + 2, hunk_diff_right_name.c_str());
    } else {
        ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "📄 File:    ");
        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 14, disp_file.c_str());
    }

    const auto& diff = hunk_diff_diff;
    int total_rows = static_cast<int>(diff.rows.size());

    int current_hunk_id = 0;
    if (hunk_diff_cursor_row >= 0 && hunk_diff_cursor_row < total_rows) {
        int h_idx = diff.rows[hunk_diff_cursor_row].hunk_idx;
        if (h_idx >= 0 && h_idx < static_cast<int>(diff.hunks.size())) {
            current_hunk_id = diff.hunks[h_idx].id + 1;
        }
    }

    std::string focus_str;
    if (hunk_diff_is_delta) {
        focus_str = (hunk_diff_focus == "left") ? (hunk_diff_left_name + " [left]")
                                                : (hunk_diff_right_name + " [right]");
    } else {
        focus_str = (hunk_diff_focus == "left") ? "Working" : "HEAD (read-only)";
    }
    char stats_buf[200];
    std::string marker_info;
    if (manual_left_active && manual_right_active) {
        marker_info = " | M: L " + std::to_string(std::min(manual_left_start, manual_left_end) + 1) + ".." +
                      std::to_string(std::max(manual_left_start, manual_left_end) + 1) + ", R " +
                      std::to_string(std::min(manual_right_start, manual_right_end) + 1) + ".." +
                      std::to_string(std::max(manual_right_start, manual_right_end) + 1);
    } else if (manual_left_active) {
        marker_info = " | M: L " + std::to_string(std::min(manual_left_start, manual_left_end) + 1) + ".." +
                      std::to_string(std::max(manual_left_start, manual_left_end) + 1);
    } else if (manual_right_active) {
        marker_info = " | M: R " + std::to_string(std::min(manual_right_start, manual_right_end) + 1) + ".." +
                      std::to_string(std::max(manual_right_start, manual_right_end) + 1);
    }

    if (!hunk_diff_status_msg.empty()) {
        snprintf(stats_buf, sizeof(stats_buf), "[hunk %d/%zu | Focus: %s%s | %s]",
                 current_hunk_id, diff.hunks.size(), focus_str.c_str(), marker_info.c_str(), hunk_diff_status_msg.c_str());
    } else {
        snprintf(stats_buf, sizeof(stats_buf), "[hunk %d/%zu | Focus: %s%s (Tab: switch)]",
                 current_hunk_id, diff.hunks.size(), focus_str.c_str(), marker_info.c_str());
    }

    int stats_x = popup_x + popup_w - static_cast<int>(std::string(stats_buf).size()) - 3;
    if (stats_x > popup_x + 16 + static_cast<int>(disp_file.size())) {
        ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
        ncplane_putstr_yx(stdplane, popup_y + 1, stats_x, stats_buf);
    }

    // Panel layout inside rounded border:
    // Left border: column popup_x
    // Left panel: from popup_x + 1, width left_w
    // Center divider: column divider_x
    // Right panel: from right_x, width right_w
    // Right border: column popup_x + popup_w - 1
    int inner_w = popup_w - 2;
    int left_w = (inner_w - 1) / 2;
    int divider_x = popup_x + 1 + left_w;
    int right_x = divider_x + 1;
    int right_w = (popup_x + popup_w - 1) - right_x;

    // Divider intersection glyphs at header separator and footer border
    ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
    ncplane_putstr_yx(stdplane, header_sep_y, divider_x, "┬");

    // Viewport scrolling
    if (hunk_diff_cursor_row < hunk_diff_scroll_y) {
        hunk_diff_scroll_y = hunk_diff_cursor_row;
    }
    if (hunk_diff_cursor_row >= hunk_diff_scroll_y + visible_rows) {
        hunk_diff_scroll_y = hunk_diff_cursor_row - visible_rows + 1;
    }
    hunk_diff_scroll_y = std::max(0, hunk_diff_scroll_y);

    // Draw content rows
    for (int r = 0; r < visible_rows; ++r) {
        int row_idx = hunk_diff_scroll_y + r;
        int draw_y = list_start_y + r;

        if (row_idx >= total_rows) {
            ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
            for (int c = popup_x + 1; c < popup_x + popup_w - 1; ++c) {
                if (c == divider_x) {
                    ncplane_set_fg_rgb8(stdplane, 65, 75, 95);
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

        bool in_left_marker = manual_left_active &&
            (row_idx >= std::min(manual_left_start, manual_left_end) &&
             row_idx <= std::max(manual_left_start, manual_left_end));

        bool in_right_marker = manual_right_active &&
            (row_idx >= std::min(manual_right_start, manual_right_end) &&
             row_idx <= std::max(manual_right_start, manual_right_end));

        uint8_t left_bg_r = 18, left_bg_g = 20, left_bg_b = 26;
        uint8_t right_bg_r = 18, right_bg_g = 20, right_bg_b = 26;

        if (is_cursor_row) {
            if (hunk_diff_focus == "left") {
                if (in_left_marker) {
                    left_bg_r = 65; left_bg_g = 45; left_bg_b = 95;
                } else if (is_hunk) {
                    if (arow.left_idx >= 0) {
                        left_bg_r = 30; left_bg_g = 56; left_bg_b = 38;
                    } else {
                        left_bg_r = 50; left_bg_g = 30; left_bg_b = 34;
                    }
                } else {
                    left_bg_r = 40; left_bg_g = 60; left_bg_b = 110;
                }

                if (in_right_marker) {
                    right_bg_r = 45; right_bg_g = 28; right_bg_b = 65;
                } else if (is_hunk) {
                    if (arow.right_idx >= 0) {
                        right_bg_r = 38; right_bg_g = 24; right_bg_b = 26;
                    } else {
                        right_bg_r = 18; right_bg_g = 30; right_bg_b = 22;
                    }
                } else {
                    right_bg_r = 26; right_bg_g = 32; right_bg_b = 46;
                }
            } else {
                if (in_left_marker) {
                    left_bg_r = 45; left_bg_g = 28; left_bg_b = 65;
                } else if (is_hunk) {
                    if (arow.left_idx >= 0) {
                        left_bg_r = 20; left_bg_g = 36; left_bg_b = 26;
                    } else {
                        left_bg_r = 30; left_bg_g = 20; left_bg_b = 22;
                    }
                } else {
                    left_bg_r = 26; left_bg_g = 32; left_bg_b = 46;
                }

                if (in_right_marker) {
                    right_bg_r = 65; right_bg_g = 45; right_bg_b = 95;
                } else if (is_hunk) {
                    if (arow.right_idx >= 0) {
                        right_bg_r = 58; right_bg_g = 32; right_bg_b = 36;
                    } else {
                        right_bg_r = 26; right_bg_g = 46; right_bg_b = 32;
                    }
                } else {
                    right_bg_r = 40; right_bg_g = 60; right_bg_b = 110;
                }
            }
        } else {
            if (in_left_marker) {
                left_bg_r = 45; left_bg_g = 28; left_bg_b = 65;
            } else if (is_hunk) {
                if (arow.left_idx >= 0) {
                    left_bg_r = 20; left_bg_g = 36; left_bg_b = 26; // Dark green for working
                } else {
                    left_bg_r = 30; left_bg_g = 20; left_bg_b = 22;
                }
            }

            if (in_right_marker) {
                right_bg_r = 45; right_bg_g = 28; right_bg_b = 65;
            } else if (is_hunk) {
                if (arow.right_idx >= 0) {
                    right_bg_r = 38; right_bg_g = 24; right_bg_b = 26; // Dark red for HEAD
                } else {
                    right_bg_r = 18; right_bg_g = 30; right_bg_b = 22;
                }
            }
        }

        // Fill left panel
        ncplane_set_bg_rgb8(stdplane, left_bg_r, left_bg_g, left_bg_b);
        for (int c = popup_x + 1; c < divider_x; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        // Left pointer & line number
        int l_start_x = popup_x + 1;
        if (is_cursor_row && hunk_diff_focus == "left") {
            ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            ncplane_putstr_yx(stdplane, draw_y, l_start_x, "▶");
        } else if (in_left_marker) {
            ncplane_set_fg_rgb8(stdplane, 220, 140, 255);
            ncplane_putstr_yx(stdplane, draw_y, l_start_x, "M");
        } else {
            ncplane_putstr_yx(stdplane, draw_y, l_start_x, " ");
        }

        char l_num[16];
        if (arow.left_idx >= 0) {
            snprintf(l_num, sizeof(l_num), "%4d ", arow.left_idx + 1);
            if (in_left_marker) {
                ncplane_set_fg_rgb8(stdplane, 220, 140, 255);
            } else if (is_cursor_row && hunk_diff_focus == "left") {
                ncplane_set_fg_rgb8(stdplane, 100, 220, 255);
            } else if (is_hunk) {
                ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
            } else {
                ncplane_set_fg_rgb8(stdplane, 110, 115, 125);
            }
            ncplane_putstr_yx(stdplane, draw_y, l_start_x + 1, l_num);
        } else {
            ncplane_putstr_yx(stdplane, draw_y, l_start_x + 1, "     ");
        }

        auto draw_syntax_line = [&](int start_x, int max_w, const std::string& text, const std::vector<SyntaxStyle>& styles) {
            int len = std::min(static_cast<int>(text.size()), max_w);
            if (len <= 0) return;
            int idx = 0;
            while (idx < len) {
                uint8_t cur_r = 220, cur_g = 220, cur_b = 220;
                if (idx < static_cast<int>(styles.size())) {
                    cur_r = styles[idx].r;
                    cur_g = styles[idx].g;
                    cur_b = styles[idx].b;
                }
                int next_idx = idx + 1;
                while (next_idx < len) {
                    uint8_t nr = 220, ng = 220, nb = 220;
                    if (next_idx < static_cast<int>(styles.size())) {
                        nr = styles[next_idx].r;
                        ng = styles[next_idx].g;
                        nb = styles[next_idx].b;
                    }
                    if (nr != cur_r || ng != cur_g || nb != cur_b) break;
                    next_idx++;
                }
                ncplane_set_fg_rgb8(stdplane, cur_r, cur_g, cur_b);
                std::string chunk = text.substr(idx, next_idx - idx);
                ncplane_putstr_yx(stdplane, draw_y, start_x + idx, chunk.c_str());
                idx = next_idx;
            }
        };

        // Left text content with syntax coloring
        int left_text_x = l_start_x + 6;
        int left_text_max_w = divider_x - left_text_x;
        if (left_text_max_w > 0) {
            if (arow.left_idx < 0) {
                ncplane_set_fg_rgb8(stdplane, 85, 120, 175);
                ncplane_putstr_yx(stdplane, draw_y, left_text_x, "~");
            } else {
                const std::string& line = diff.left_lines[arow.left_idx];
                std::vector<SyntaxStyle> l_styles;
                if (buf.syntax) {
                    l_styles = buf.syntax->get_line_styles(arow.left_idx, line);
                }
                draw_syntax_line(left_text_x, left_text_max_w, line, l_styles);
            }
        }

        // Center divider column
        ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
        if (is_cursor_row) {
            ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            if (hunk_diff_focus == "left") {
                ncplane_putstr_yx(stdplane, draw_y, divider_x, "▌");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, divider_x, "▐");
            }
        } else if (in_left_marker || in_right_marker) {
            ncplane_set_fg_rgb8(stdplane, 220, 140, 255);
            ncplane_putstr_yx(stdplane, draw_y, divider_x, "◈");
        } else if (is_hunk) {
            ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
            ncplane_putstr_yx(stdplane, draw_y, divider_x, "◆");
        } else {
            ncplane_set_fg_rgb8(stdplane, 65, 75, 95);
            ncplane_putstr_yx(stdplane, draw_y, divider_x, "│");
        }

        // Fill right panel
        ncplane_set_bg_rgb8(stdplane, right_bg_r, right_bg_g, right_bg_b);
        for (int c = right_x; c < popup_x + popup_w - 1; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, c, ' ');
        }

        // Right pointer & line number
        if (is_cursor_row && hunk_diff_focus == "right") {
            ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
            ncplane_putstr_yx(stdplane, draw_y, right_x, "▶");
        } else if (in_right_marker) {
            ncplane_set_fg_rgb8(stdplane, 220, 140, 255);
            ncplane_putstr_yx(stdplane, draw_y, right_x, "M");
        } else {
            ncplane_putstr_yx(stdplane, draw_y, right_x, " ");
        }

        char r_num[16];
        if (arow.right_idx >= 0) {
            snprintf(r_num, sizeof(r_num), "%4d ", arow.right_idx + 1);
            if (in_right_marker) {
                ncplane_set_fg_rgb8(stdplane, 220, 140, 255);
            } else if (is_cursor_row && hunk_diff_focus == "right") {
                ncplane_set_fg_rgb8(stdplane, 100, 220, 255);
            } else if (is_hunk) {
                ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
            } else {
                ncplane_set_fg_rgb8(stdplane, 110, 115, 125);
            }
            ncplane_putstr_yx(stdplane, draw_y, right_x + 1, r_num);
        } else {
            ncplane_putstr_yx(stdplane, draw_y, right_x + 1, "     ");
        }

        // Right text content with syntax coloring
        int right_text_x = right_x + 6;
        int right_text_max_w = (popup_x + popup_w - 1) - right_text_x;
        if (right_text_max_w > 0) {
            if (arow.right_idx < 0) {
                ncplane_set_fg_rgb8(stdplane, 85, 120, 175);
                ncplane_putstr_yx(stdplane, draw_y, right_text_x, "~");
            } else {
                const std::string& line = diff.right_lines[arow.right_idx];
                if (config.settings.hunk_diff_right_syntax || hunk_diff_is_delta) {
                    std::vector<SyntaxStyle> r_styles;
                    if (hunk_diff_head_syntax) {
                        r_styles = hunk_diff_head_syntax->get_line_styles(arow.right_idx, line);
                    } else if (buf.syntax) {
                        r_styles = buf.syntax->get_line_styles(-1, line);
                    }
                    draw_syntax_line(right_text_x, right_text_max_w, line, r_styles);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 215, 220, 230);
                    std::string disp = line;
                    if (static_cast<int>(disp.size()) > right_text_max_w) {
                        disp = disp.substr(0, right_text_max_w);
                    }
                    ncplane_putstr_yx(stdplane, draw_y, right_text_x, disp.c_str());
                }
            }
        }
    }

    // Footer actions on bottom border (following F11 vg footer style)
    std::string footer = " [Tab] Switch  [m/M] Mark  [a] Push  [A] Pull  [l/L] Hunk  [u] Undo  [d] Del  [y] Yank  [p] Paste  [w] Write  [q] Close ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
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
    filepicker_git_status.clear();
    std::string root = !project_dir.empty() ? project_dir : ".";

    std::string repo_root = detect_git_repo_root(root);
    if (!repo_root.empty()) {
        GitViewData gdata = query_git_view_data(repo_root);
        if (gdata.is_repo) {
            for (const auto& f : gdata.staged) {
                filepicker_git_status[f.path] = f.glyph;
            }
            for (const auto& f : gdata.unstaged) {
                filepicker_git_status[f.path] = f.glyph;
            }
            for (const auto& f : gdata.untracked) {
                filepicker_git_status[f.path] = '?';
            }
        }
    }

    for (const auto& b : buffers) {
        if (b->modified) {
            std::string rel = b->name;
            if (!b->file_path.empty()) {
                std::error_code ec;
                std::string r = fs::relative(b->file_path, !repo_root.empty() ? repo_root : root, ec).string();
                if (!ec && !r.empty() && r.rfind("..", 0) != 0) {
                    rel = r;
                }
            }
            filepicker_git_status[rel] = 'M';
        }
    }

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

            std::string fn = filepicker_filtered_files[idx];
            char marker = ' ';
            auto st_it = filepicker_git_status.find(fn);
            if (st_it != filepicker_git_status.end()) {
                marker = st_it->second;
            }

            if (marker != ' ') {
                if (marker == 'M') ncplane_set_fg_rgb8(stdplane, 240, 200, 80);
                else if (marker == 'A') ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                else if (marker == 'D') ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                else if (marker == '?') ncplane_set_fg_rgb8(stdplane, 80, 200, 240);
                else ncplane_set_fg_rgb8(stdplane, 220, 225, 235);

                char m_buf[3] = {marker, ' ', '\0'};
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 5, m_buf);
            } else {
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 5, "  ");
            }

            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            else ncplane_set_fg_rgb8(stdplane, 210, 215, 225);

            int max_len = popup_w - 9;
            if (static_cast<int>(fn.size()) > max_len) {
                fn = "..." + fn.substr(fn.size() - max_len + 3);
            }
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 7, fn.c_str());
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
