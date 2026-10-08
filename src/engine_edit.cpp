#include "engine.hpp"
#include "action.hpp"
#include "command.hpp"
#include "autocomplete.hpp"
#include "keymap.hpp"
#include "log.hpp"
#include "util/base64.hpp"
#include "util/bracket_match.hpp"
#include "util/indent.hpp"
#include "util/keymap_json.hpp"
#include "util/path_line_col.hpp"
#include "dot_command.hpp"
#include "bracket_jump.hpp"
#include "clipboard_os.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern int g_hunk_marker_style;

namespace fs = std::filesystem;
using namespace Keymap;

void VimEngine::handle_key_input(const ncinput& ni, uint32_t key) {
    if (Keymap::is_modifier_key(key)) {
        return;
    }

    key = Keymap::resolve_shifted(ni, key);

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

    // 1. Mini help overlay (F12)
    if (is_fkey(ni, key, 12)) {
        show_mini_help = !show_mini_help;
        return;
    }
    if (show_mini_help) {
        if (is_esc(ni, key) || key == 'q' || key == 'Q') {
            show_mini_help = false;
        }
        return;
    }

    // 2. Git Status view (F1)
    if (is_fkey(ni, key, 1)) {
        if (show_git_status) {
            close_git_status();
        } else {
            show_filepicker = false;
            show_settings_popup = false;
            show_git_hunk_popup = false;
            show_whichkey_popup = false;
            show_buffer_list = false;
            show_rg_popup = false;
            show_hunk_diff = false;
            open_git_status();
        }
        return;
    }
    if (show_git_status) {
        handle_git_status_input(ni, key);
        return;
    }

    // 3. Side-by-side Hunk Diff / Delta View (F5)
    if (is_fkey(ni, key, 5)) {
        if (show_hunk_diff) {
            close_hunk_diff();
        } else {
            show_filepicker = false;
            show_settings_popup = false;
            show_git_hunk_popup = false;
            show_whichkey_popup = false;
            show_buffer_list = false;
            show_rg_popup = false;
            open_hunk_diff();
        }
        return;
    }
    if (show_hunk_diff) {
        handle_hunk_diff_input(ni, key);
        return;
    }

    // 3b. Theme selection popup (F6)
    if (is_fkey(ni, key, 6)) {
        if (show_theme_popup) {
            close_theme_popup();
        } else {
            open_theme_popup();
        }
        return;
    }
    if (show_theme_popup) {
        handle_theme_popup_input(ni, key);
        return;
    }

    // 3c. Comment toggle (F2)
    if (!popup_active && is_fkey(ni, key, 2)) {
        toggle_line_comments();
        return;
    }

    // 5. Git Hunk popup (F4)
    if (is_fkey(ni, key, 4)) {
        if (show_git_hunk_popup) {
            show_git_hunk_popup = false;
        } else {
            show_whichkey_popup = false;
            open_git_hunk_popup();
        }
        return;
    }
    if (show_git_hunk_popup) {
        handle_git_hunk_popup(ni, key);
        return;
    }

    // 6. Settings popup (F9)
    if (is_fkey(ni, key, 9)) {
        show_settings_popup = !show_settings_popup;
        if (show_settings_popup) {
            show_git_hunk_popup = false;
            show_whichkey_popup = false;
        }
        return;
    }
    if (show_settings_popup) {
        handle_settings_popup(ni, key);
        return;
    }

    // 7. Ripgrep popup (F11)
    if (is_fkey(ni, key, 11)) {
        if (show_rg_popup) {
            show_rg_popup = false;
            rg_query_active = false;
        } else {
            show_filepicker = false;
            show_settings_popup = false;
            show_git_hunk_popup = false;
            show_whichkey_popup = false;
            if (!rg_groups.empty()) {
                show_rg_popup = true;
                rg_query_active = false;
                set_info_msg("Ripgrep: \"" + rg_query + "\" (" + std::to_string(rg_flattened_matches.size()) + " matches)");
            } else {
                std::string c_word = get_word_under_cursor();
                if (!c_word.empty()) {
                    run_ripgrep(c_word);
                } else {
                    show_rg_popup = true;
                    rg_query_active = true;
                    rg_query_input.clear();
                    set_info_msg("Ripgrep: Enter search pattern...");
                }
            }
        }
        return;
    }
    if (show_rg_popup) {
        handle_rg_popup_input(ni, key);
        return;
    }

    // 8. Buffer list popup
    if (show_buffer_list) {
        handle_buffer_list_input(ni, key);
        return;
    }

    // 9. File picker popup
    if (show_filepicker) {
        handle_filepicker_input(ni, key);
        return;
    }

    // 9b. Workspace list popup
    if (show_workspace_list) {
        handle_workspace_list_input(ni, key);
        return;
    }

    // 9c. Incremental buffer search prompt ('/')
    if (search_active) {
        handle_search_input(ni, key);
        return;
    }

    // 10. Leader key WhichKey popup / Window Ops popup / Leader P submode
    if (leader_pending || ctrl_w_pending || leader_p_pending) {
        handle_whichkey_popup(ni, key);
        return;
    }

    // 10b. Global OS Bracket Paste via Ctrl-Shift-V
    if (Keymap::is_ctrl_shift(ni, key, 'v')) {
        if (mode == Mode::COMMAND) {
            handle_command_mode(ni, key);
        } else if (!popup_active) {
            paste_from_clipboard(true);
        }
        return;
    }

    // 11. Global shortcuts (Alt combinations, window cycling, etc.)
    // If the active mode has a custom keymap in keymap.json for this key chord,
    // let modal dispatch handle it instead of built-in global shortcuts!
    if (mode != Mode::COMMAND && keymap.has_mapping(mode, ni, key)) {
        // Let modal dispatch in Step 12 handle user's keymap
    } else if (handle_global_shortcuts(ni, key)) {
        return;
    }

    // 12. Modal dispatch
    switch (mode) {
        case Mode::NORMAL:
            handle_normal_mode(ni, key);
            break;
        case Mode::INSERT:
            handle_insert_mode(ni, key);
            break;
        case Mode::VISUAL:
        case Mode::VISUAL_BLOCK:
            handle_visual_mode(ni, key);
            break;
        case Mode::COMMAND:
            handle_command_mode(ni, key);
            break;
    }
}

void VimEngine::handle_git_hunk_popup(const ncinput& ni, uint32_t key) {
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();

    if (is_esc(ni, key) || is_fkey(ni, key, 4) || key == 'q' || key == 'Q') {
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

    if (is_fkey(ni, key, 2) || key == NCKEY_UP || key == 'k' || key == 'K' ||
        key == 'L' || (ni.shift && (key == 'l' || ni.id == 'l' || ni.id == 'L'))) {
        active_hunk_idx = (active_hunk_idx + static_cast<int>(hunks.size()) - 1) % hunks.size();
    } else if (is_fkey(ni, key, 3) || key == NCKEY_DOWN || key == 'j' || key == 'J' || key == 'l') {
        active_hunk_idx = (active_hunk_idx + 1) % hunks.size();
    }
}

void VimEngine::handle_git_status_input(const ncinput& ni, uint32_t key) {
    std::string root = detect_git_repo_root(!project_dir.empty() ? project_dir : ".");
    if (root.empty()) root = project_dir;

    if (git_stash_action_active) {
        if (!git_stash_status_msg.empty() && key != 'y' && key != 'd' && key != 'q' && !is_esc(ni, key)) {
            git_stash_status_msg.clear();
        }

        if (key == 'q' || key == 'Q' || is_esc(ni, key)) {
            git_stash_action_active = false;
            git_stash_status_msg.clear();
            return;
        }

        if (key == 'y' || key == 'Y') {
            auto res = git_stash_pop_info(root, git_stash_action_idx);
            git_stash_action_active = false;
            git_stash_status_msg.clear();
            refresh_git_status();
            git_status_msg = res.summary;
            return;
        }

        if (key == 'd' || key == 'D') {
            auto res = git_stash_drop_info(root, git_stash_action_idx);
            git_stash_action_active = false;
            git_stash_status_msg.clear();
            refresh_git_status();
            git_status_msg = res.summary;
            return;
        }
        return;
    }

    if (!git_status_msg.empty()) {
        git_status_msg.clear();
    }

    if (key == 'q' || key == 'Q' || is_esc(ni, key)) {
        close_git_status();
        return;
    }

    if (key == 'j' || key == NCKEY_DOWN) {
        if (git_status_cursor + 1 < static_cast<int>(git_status_rows.size())) {
            git_status_cursor++;
            refresh_git_status_right();
        }
        return;
    }

    if (key == 'k' || key == NCKEY_UP) {
        if (git_status_cursor > 0) {
            git_status_cursor--;
            refresh_git_status_right();
        }
        return;
    }

    if (key == 'J' || key == 'L' || (ni.shift && (key == 'j' || key == 'l'))) {
        for (size_t i = git_status_cursor + 1; i < git_status_rows.size(); ++i) {
            if (git_status_rows[i].kind == GitStatusRow::HEADER) {
                git_status_cursor = static_cast<int>(i);
                refresh_git_status_right();
                return;
            }
        }
        return;
    }

    if (key == 'K' || key == 'l' || (ni.shift && key == 'k')) {
        for (int i = git_status_cursor - 1; i >= 0; --i) {
            if (git_status_rows[i].kind == GitStatusRow::HEADER) {
                git_status_cursor = i;
                refresh_git_status_right();
                return;
            }
        }
        return;
    }

    if (key == 'g' || key == NCKEY_HOME) {
        git_status_cursor = 0;
        refresh_git_status_right();
        return;
    }

    if (key == 'G' || key == NCKEY_END) {
        git_status_cursor = git_status_rows.empty() ? 0 : static_cast<int>(git_status_rows.size()) - 1;
        refresh_git_status_right();
        return;
    }

    if (key == 'r' || key == 'R') {
        refresh_git_status();
        git_status_msg = "Refreshed git status";
        return;
    }

    if (key == 'z') {
        if (git_is_clean(root)) {
            git_status_msg = "Nothing to stash";
            return;
        }
        auto res = git_stash_push_info(root);
        refresh_git_status();
        git_status_msg = res.summary;
        return;
    }

    if (key == 's') {
        if (git_status_rows.empty() || git_status_cursor < 0 || git_status_cursor >= static_cast<int>(git_status_rows.size())) return;
        const auto& row = git_status_rows[git_status_cursor];
        if (row.kind == GitStatusRow::STAGE_FILE) {
            git_unstage_file(root, row.path);
            refresh_git_status();
            git_status_msg = "Unstaged " + row.path;
        } else if (row.kind == GitStatusRow::UNSTAGE_FILE || row.kind == GitStatusRow::UNTRACKED_FILE) {
            git_stage_file(root, row.path);
            refresh_git_status();
            git_status_msg = "Staged " + row.path;
        } else {
            git_status_msg = "Cannot stage/unstage in this section";
        }
        return;
    }

    if (is_enter(ni, key)) {
        if (git_status_rows.empty() || git_status_cursor < 0 || git_status_cursor >= static_cast<int>(git_status_rows.size())) return;
        const auto& row = git_status_rows[git_status_cursor];

        if (row.kind == GitStatusRow::STAGE_FILE ||
            row.kind == GitStatusRow::UNSTAGE_FILE ||
            row.kind == GitStatusRow::UNTRACKED_FILE ||
            row.kind == GitStatusRow::COMMIT1_FILE ||
            row.kind == GitStatusRow::COMMIT2_FILE) {

            std::string full_path = (fs::path(root) / row.path).lexically_normal().string();
            save_window_position(active_win(), active_buf());

            size_t found_idx = buffers.size();
            for (size_t i = 0; i < buffers.size(); ++i) {
                if (buffers[i]->file_path == full_path || buffers[i]->name == row.path || buffers[i]->file_path == row.path) {
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
            close_git_status();
            set_info_msg("\"" + active_buf().name + "\" [" + std::to_string(active_buf().lines.size()) + " lines]");
            return;
        }

        if (row.kind == GitStatusRow::BRANCH) {
            if (!git_is_clean(root)) {
                git_status_msg = "Working tree not clean — commit or stash first";
                return;
            }
            if (git_checkout_branch(root, row.branch_name)) {
                refresh_git_status();
                git_status_msg = "Switched to branch '" + row.branch_name + "'";
            } else {
                git_status_msg = "Failed to switch to branch '" + row.branch_name + "'";
            }
            return;
        }

        if (row.kind == GitStatusRow::STASH) {
            git_stash_action_active = true;
            git_stash_action_ref = row.stash_ref;
            git_stash_action_idx = row.stash_idx;
            git_stash_status_msg.clear();
            return;
        }
    }
}

void VimEngine::handle_filepicker_input(const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key)) {
        show_filepicker = false;
        set_info_msg("");
        return;
    }

    if (is_enter(ni, key)) {
        if (filepicker_tree_mode) {
            if (!filepicker_filtered_tree.empty() &&
                filepicker_selected_idx >= 0 &&
                filepicker_selected_idx < static_cast<int>(filepicker_filtered_tree.size())) {

                const auto& entry = filepicker_filtered_tree[filepicker_selected_idx];
                std::string root = !project_dir.empty() ? project_dir : ".";

                if (entry.is_parent) {
                    fs::path cur = fs::path(root);
                    if (!filepicker_cur_dir.empty()) {
                        cur = fs::path(root) / filepicker_cur_dir;
                    }
                    fs::path parent = cur.parent_path();
                    std::error_code ec;
                    std::string rel = fs::relative(parent, root, ec).string();
                    if (ec || rel == ".") rel = "";
                    filepicker_cur_dir = rel;
                    filepicker_query.clear();
                    filepicker_selected_idx = 0;
                    filepicker_scroll = 0;
                    scan_filepicker_tree();
                    filter_filepicker_files();
                    return;
                } else if (entry.is_dir) {
                    std::string dir_name = entry.name;
                    while (!dir_name.empty() && dir_name.back() == '/') dir_name.pop_back();

                    if (filepicker_cur_dir.empty()) {
                        filepicker_cur_dir = dir_name;
                    } else {
                        filepicker_cur_dir = filepicker_cur_dir + "/" + dir_name;
                    }
                    filepicker_query.clear();
                    filepicker_selected_idx = 0;
                    filepicker_scroll = 0;
                    scan_filepicker_tree();
                    filter_filepicker_files();
                    return;
                } else {
                    std::string full_path = entry.full_path;
                    save_window_position(active_win(), active_buf());

                    size_t found_idx = buffers.size();
                    for (size_t i = 0; i < buffers.size(); ++i) {
                        if (buffers[i]->file_path == full_path) {
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
                    return;
                }
            }
            return;
        }

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

    int max_items = filepicker_tree_mode ? static_cast<int>(filepicker_filtered_tree.size())
                                         : static_cast<int>(filepicker_filtered_files.size());

    if (key == NCKEY_UP || is_ctrl(ni, key, 'p') || is_ctrl(ni, key, 'k')) {
        if (filepicker_selected_idx > 0) {
            filepicker_selected_idx--;
        } else if (max_items > 0) {
            filepicker_selected_idx = max_items - 1;
        }
        return;
    }

    if (key == NCKEY_DOWN || is_ctrl(ni, key, 'n') || is_ctrl(ni, key, 'j')) {
        if (filepicker_selected_idx + 1 < max_items) {
            filepicker_selected_idx++;
        } else {
            filepicker_selected_idx = 0;
        }
        return;
    }

    if (key == '\t' || key == NCKEY_TAB || ni.id == '\t' || ni.id == NCKEY_TAB) {
        filepicker_tree_mode = !filepicker_tree_mode;
        filepicker_query.clear();
        filepicker_selected_idx = 0;
        filepicker_scroll = 0;

        if (filepicker_tree_mode) {
            scan_filepicker_tree();
            filter_filepicker_files();
            set_info_msg("Directory Tree (Tab: Files)");
        } else {
            filter_filepicker_files();
            set_info_msg("Find File (Tab: Tree)");
        }
        return;
    }

    if (is_backspace(ni, key)) {
        if (!filepicker_query.empty()) {
            filepicker_query.pop_back();
            filepicker_selected_idx = 0;
            filepicker_scroll = 0;
            filter_filepicker_files();
        } else if (filepicker_tree_mode && !filepicker_cur_dir.empty()) {
            size_t slash = filepicker_cur_dir.find_last_of("/\\");
            if (slash != std::string::npos) {
                filepicker_cur_dir = filepicker_cur_dir.substr(0, slash);
            } else {
                filepicker_cur_dir.clear();
            }
            filepicker_selected_idx = 0;
            filepicker_scroll = 0;
            scan_filepicker_tree();
            filter_filepicker_files();
        }
        return;
    }

    if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
        filepicker_query += static_cast<char>(key);
        filepicker_selected_idx = 0;
        filepicker_scroll = 0;
        filter_filepicker_files();
        return;
    }
}

void VimEngine::handle_rg_popup_input(const ncinput& ni, uint32_t key) {
    if (rg_query_active) {
        if (is_esc(ni, key) || is_fkey(ni, key, 11)) {
            rg_query_active = false;
            rg_query_input.clear();
            if (rg_groups.empty() && rg_query.empty()) {
                show_rg_popup = false;
            }
            set_info_msg(rg_groups.empty() ? "" : "Search cancelled.");
            return;
        }

        if (is_enter(ni, key)) {
            if (!rg_query_input.empty()) {
                std::string q = rg_query_input;
                rg_query_active = false;
                rg_query_input.clear();
                run_ripgrep(q);
            } else if (!rg_query.empty()) {
                rg_query_active = false;
                run_ripgrep(rg_query);
            } else {
                set_info_msg("Please enter a search pattern.");
            }
            return;
        }

        if (is_backspace(ni, key)) {
            if (!rg_query_input.empty()) {
                rg_query_input.pop_back();
            }
            return;
        }

        if (is_ctrl(ni, key, 'u')) {
            rg_query_input.clear();
            return;
        }

        if (is_ctrl(ni, key, 'w')) {
            while (!rg_query_input.empty() && std::isspace(static_cast<unsigned char>(rg_query_input.back()))) {
                rg_query_input.pop_back();
            }
            while (!rg_query_input.empty() && !std::isspace(static_cast<unsigned char>(rg_query_input.back()))) {
                rg_query_input.pop_back();
            }
            return;
        }

        if (Keymap::is_ctrl_shift(ni, key, 'v')) {
            std::string clip = get_system_clipboard();
            for (char c : clip) {
                if (c == '\r' || c == '\n') break;
                rg_query_input += c;
            }
            return;
        }

        if (key == ' ') {
            rg_query_input += ' ';
            return;
        }

        if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
            rg_query_input += static_cast<char>(key);
            return;
        }

        if (ni.utf8[0] != '\0' && !ni.alt && !ni.ctrl) {
            rg_query_input += reinterpret_cast<const char*>(ni.utf8);
            return;
        }

        return;
    }

    if (key == '/' && !rg_replace_active) {
        rg_query_active = true;
        rg_query_input.clear();
        set_info_msg("Ripgrep: Enter search pattern...");
        return;
    }

    if (is_esc(ni, key) || is_fkey(ni, key, 11) || ((key == 'q' || key == 'Q') && !rg_replace_active)) {
        if (rg_replace_active) {
            rg_replace_active = false;
            return;
        }
        show_rg_popup = false;
        rg_replace_active = false;
        rg_query_active = false;
        set_info_msg("");
        return;
    }

    if (key == '\t' || key == NCKEY_TAB) {
        rg_replace_active = !rg_replace_active;
        return;
    }

    if (is_enter(ni, key)) {
        if (rg_replace_active) {
            apply_rg_replace();
        } else {
            open_selected_rg_match();
        }
        return;
    }

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

    if (rg_replace_active && is_backspace(ni, key)) {
        if (!rg_replace_query.empty()) {
            rg_replace_query.pop_back();
        }
        return;
    }

    if (key == NCKEY_UP || (!rg_replace_active && (key == 'k' || key == 'K')) ||
        is_ctrl(ni, key, 'p') || is_ctrl(ni, key, 'k')) {
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
        is_ctrl(ni, key, 'n') || is_ctrl(ni, key, 'j')) {
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
        if (key == NCKEY_PGDOWN) {
            rg_selected_display_idx = std::min(static_cast<int>(rg_display_lines.size()) - 1, rg_selected_display_idx + 10);
            if (rg_selected_display_idx >= 0 && rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {
                int midx = rg_display_lines[rg_selected_display_idx].match_idx;
                if (midx >= 0) rg_selected_match_idx = midx;
            }
            return;
        }

        if (rg_replace_active) {
            if (Keymap::is_ctrl_shift(ni, key, 'v')) {
                std::string clip = get_system_clipboard();
                for (char c : clip) {
                    if (c == '\r' || c == '\n') break;
                    rg_replace_query += c;
                }
                return;
            }
            if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
                rg_replace_query += static_cast<char>(key);
                return;
            }
            if (ni.utf8[0] != '\0' && !ni.alt && !ni.ctrl) {
                rg_replace_query += reinterpret_cast<const char*>(ni.utf8);
                return;
            }
        }
    }

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

void VimEngine::handle_buffer_list_input(const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key) || is_alt(ni, key, 'b')) {
        show_buffer_list = false;
        set_info_msg("");
        return;
    }

    if (buffers.empty()) {
        show_buffer_list = false;
        return;
    }

    int total_filtered = static_cast<int>(buffer_list_filtered_indices.size());

    if (is_enter(ni, key)) {
        if (!buffer_list_filtered_indices.empty() &&
            buffer_list_selected_idx >= 0 &&
            buffer_list_selected_idx < total_filtered) {
            size_t target_idx = buffer_list_filtered_indices[buffer_list_selected_idx];
            show_buffer_list = false;
            switch_to_buffer(target_idx);
        } else {
            show_buffer_list = false;
        }
        return;
    }

    if (key == NCKEY_UP || is_ctrl(ni, key, 'p') || is_ctrl(ni, key, 'k') ||
        (ni.shift && (key == '\t' || key == NCKEY_TAB))) {
        if (total_filtered > 0) {
            if (buffer_list_selected_idx > 0) {
                buffer_list_selected_idx--;
            } else {
                buffer_list_selected_idx = total_filtered - 1;
            }
        }
        return;
    }

    if (key == NCKEY_DOWN || is_ctrl(ni, key, 'n') || is_ctrl(ni, key, 'j') || key == '\t') {
        if (total_filtered > 0) {
            if (buffer_list_selected_idx + 1 < total_filtered) {
                buffer_list_selected_idx++;
            } else {
                buffer_list_selected_idx = 0;
            }
        }
        return;
    }

    if (is_ctrl(ni, key, 'd') || is_ctrl(ni, key, 'x') || key == NCKEY_DEL) {
        if (buffers.size() <= 1) {
            set_info_msg("Cannot close the last remaining buffer.");
            return;
        }
        if (buffer_list_filtered_indices.empty() ||
            buffer_list_selected_idx < 0 ||
            buffer_list_selected_idx >= total_filtered) {
            return;
        }

        size_t to_remove = buffer_list_filtered_indices[buffer_list_selected_idx];
        std::string name = buffers[to_remove]->name;
        buffers.erase(buffers.begin() + to_remove);
        for (auto& w : windows) {
            if (w.buffer_idx == to_remove) {
                w.buffer_idx = (to_remove > 0) ? to_remove - 1 : 0;
                restore_window_position(w, *buffers[w.buffer_idx]);
            } else if (w.buffer_idx > to_remove) {
                w.buffer_idx--;
            }
        }

        filter_buffer_list();
        set_info_msg("Closed buffer: " + name);
        return;
    }

    if (is_backspace(ni, key)) {
        if (!buffer_list_query.empty()) {
            buffer_list_query.pop_back();
            filter_buffer_list();
        }
        return;
    }

    if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
        buffer_list_query += static_cast<char>(key);
        filter_buffer_list();
        return;
    }
}

void VimEngine::handle_whichkey_popup(const ncinput& ni, uint32_t key) {
    auto& win = active_win();
    auto& buf = active_buf();

    if (leader_p_pending) {
        leader_p_pending = false;
        leader_pending = false;
        show_whichkey_popup = false;
        if (is_esc(ni, key)) {
            set_info_msg("");
            return;
        }
        std::string second_k = Keymap::key_to_string(ni, key);
        std::string full_chord = "<Space>p" + second_k;
        const std::unordered_map<std::string, std::string>* target_map =
            (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) ? &keymap.visual_map : &keymap.normal_map;
        auto it = target_map->find(full_chord);
        if (it != target_map->end()) {
            keymap.execute_action(*this, mode, it->second);
            return;
        }
        it = keymap.normal_map.find(full_chord);
        if (it != keymap.normal_map.end()) {
            keymap.execute_action(*this, mode, it->second);
            return;
        }
        if (key == 'p' || key == 'P') {
            paste_full_replace();
            return;
        }
        if (key == 'v' || key == 'V') {
            paste_from_clipboard(true);
            return;
        }
        set_info_msg("Paste cancelled.");
        return;
    }

    bool was_ctrl_w = ctrl_w_pending || (whichkey_mode == WhichKeyMode::WINDOW);
    leader_pending = false;
    ctrl_w_pending = false;
    show_whichkey_popup = false;

    if (was_ctrl_w) {
        whichkey_mode = WhichKeyMode::LEADER;
        if (is_esc(ni, key)) {
            set_info_msg("");
            return;
        }

        std::string k_str = Keymap::key_to_string(ni, key);
        if (!k_str.empty()) {
            std::string chord = "<C-w>" + k_str;
            auto it = keymap.normal_map.find(chord);
            if (it != keymap.normal_map.end()) {
                keymap.execute_action(*this, Mode::NORMAL, it->second);
                return;
            }
        }

        switch (key) {
            case 'q':
            case 'c':
                close_active_window();
                break;
            case 'v':
                split_window(SplitType::VERTICAL);
                break;
            case 's':
                split_window(SplitType::HORIZONTAL);
                break;
            case 'w':
            case 23:
                active_win_idx = (active_win_idx + 1) % windows.size();
                set_info_msg("Focused Window #" + std::to_string(windows[active_win_idx].id));
                break;
            case 'W':
                active_win_idx = (active_win_idx + windows.size() - 1) % windows.size();
                set_info_msg("Focused Window #" + std::to_string(windows[active_win_idx].id));
                break;
            case 'o':
                if (windows.size() > 1) {
                    Window current = active_win();
                    windows = {current};
                    active_win_idx = 0;
                    split_mode = SplitType::NONE;
                    set_info_msg("Closed other windows.");
                } else {
                    set_info_msg("Already only one window.");
                }
                break;
            case 'h':
            case 'k':
            case NCKEY_LEFT:
            case NCKEY_UP:
                active_win_idx = (active_win_idx + windows.size() - 1) % windows.size();
                set_info_msg("Focused Window #" + std::to_string(windows[active_win_idx].id));
                break;
            case 'l':
            case 'j':
            case NCKEY_RIGHT:
            case NCKEY_DOWN:
                active_win_idx = (active_win_idx + 1) % windows.size();
                set_info_msg("Focused Window #" + std::to_string(windows[active_win_idx].id));
                break;
            default:
                if (key >= 32 && key < 127) {
                    set_info_msg("Window Ops: Unmapped key [" + std::string(1, static_cast<char>(key)) + "]");
                }
                break;
        }
        return;
    }

    whichkey_mode = WhichKeyMode::LEADER;

    if (keymap.handle_whichkey(*this, ni, key)) {
        return;
    }

    if (is_esc(ni, key) || key == ' ') {
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
        case 'y':
            copy_selection_to_clipboard();
            break;
        case 'p':
            leader_p_pending = true;
            show_whichkey_popup = true;
            set_info_msg("Paste [Space-p]: [p] Full Replace from Clipboard  [v] Paste from Clipboard  [Esc] Cancel");
            return;
        case 'w':
            if (buf.save_to_file("")) {
                save_window_position(win, buf);
                save_positions();
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
        case 'd':
            open_hunk_diff();
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

void VimEngine::handle_settings_popup(const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key) || is_fkey(ni, key, 9) || key == 'q' || key == 'Q') {
        show_settings_popup = false;
        config.save();
        set_info_msg("Settings applied.");
        return;
    }

    const int total_items = 8;
    if (key == NCKEY_UP || key == 'k' || key == 'K') {
        settings_selected_idx = (settings_selected_idx + total_items - 1) % total_items;
    } else if (key == NCKEY_DOWN || key == 'j' || key == 'J') {
        settings_selected_idx = (settings_selected_idx + 1) % total_items;
    } else if (is_enter(ni, key) || key == ' ' ||
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
            case 5: {
                if (key == NCKEY_LEFT || key == 'h') {
                    if (config.settings.scroll_offset <= 0) config.settings.scroll_offset = 10;
                    else config.settings.scroll_offset--;
                } else {
                    if (config.settings.scroll_offset >= 10) config.settings.scroll_offset = 0;
                    else config.settings.scroll_offset++;
                }
                break;
            }
            case 6:
                config.settings.hunk_diff_right_syntax = !config.settings.hunk_diff_right_syntax;
                break;
            case 7:
                config.settings.search_wrap = !config.settings.search_wrap;
                break;
        }
        config.save();
        for (auto& w : windows) {
            if (w.buffer_idx < buffers.size()) {
                update_window_scroll(w, *buffers[w.buffer_idx]);
            }
        }
    }
}








