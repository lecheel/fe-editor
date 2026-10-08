#include "action.hpp"
#include "engine.hpp"
#include "bracket_jump.hpp"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

void ActionRegistry::init_default_actions() {
    register_action("save", {"write", "w"}, "File", "Save active buffer to disk",
        [](ActionContext& ctx) {
            if (ctx.engine.active_buf().save_to_file()) {
                ctx.engine.save_window_position(ctx.engine.active_win(), ctx.engine.active_buf());
                ctx.engine.save_positions();
                ctx.engine.get_config().save();
                ctx.engine.set_info_msg("\"" + ctx.engine.active_buf().name + "\" written");
            } else {
                ctx.engine.set_info_msg("E212: Can't open file for writing");
            }
            return true;
        }
    );

    register_action("quit", {"q"}, "Window", "Quit editor",
        [](ActionContext& ctx) {
            ctx.engine.execute_command("q");
            return true;
        }
    );

    register_action("copy_clipboard", {"leader_y", "clipboard_copy", "copy_os", "copy"}, "Edit", "Copy line or selection to system clipboard (OSC 52)",
        [](ActionContext& ctx) {
            ctx.engine.copy_selection_to_clipboard();
            return true;
        }
    );

    register_action("full_replace_paste", {"pp", "replace_paste", "paste_replace"}, "Edit", "Full replace paste from clipboard (OSC 52)",
        [](ActionContext& ctx) {
            ctx.engine.paste_full_replace();
            return true;
        }
    );

    register_action("paste_clipboard", {"pv", "bracket_paste", "paste_os", "paste"}, "Edit", "Paste from clipboard at cursor (OSC 52)",
        [](ActionContext& ctx) {
            ctx.engine.paste_from_clipboard(true);
            return true;
        }
    );

    register_action("undo", {"u"}, "Edit", "Undo last modification",
        [](ActionContext& ctx) {
            if (ctx.engine.active_buf().undo(ctx.engine.active_win().cursors)) {
                ctx.engine.active_win().clamp_all_cursors(ctx.engine.active_buf(), ctx.mode);
                ctx.engine.update_window_scroll(ctx.engine.active_win(), ctx.engine.active_buf());
                ctx.engine.set_info_msg("Undo applied.");
            } else {
                ctx.engine.set_info_msg("Already at oldest change.");
            }
            return true;
        }
    );

    register_action("redo", {"U"}, "Edit", "Redo last undone modification",
        [](ActionContext& ctx) {
            if (ctx.engine.active_buf().redo(ctx.engine.active_win().cursors)) {
                ctx.engine.active_win().clamp_all_cursors(ctx.engine.active_buf(), ctx.mode);
                ctx.engine.update_window_scroll(ctx.engine.active_win(), ctx.engine.active_buf());
                ctx.engine.set_info_msg("Redo applied.");
            } else {
                ctx.engine.set_info_msg("Already at newest change.");
            }
            return true;
        }
    );

    register_action("filepicker", {"find_file", "picker"}, "Search", "Open file picker",
        [](ActionContext& ctx) {
            ctx.engine.open_filepicker();
            return true;
        }
    );

    register_action("git_status", {"git", "gitview", "gs"}, "Git", "Open git status view (F1)",
        [](ActionContext& ctx) {
            ctx.engine.open_git_status();
            return true;
        }
    );

    register_action("hunk_diff", {"diff", "delta"}, "Git", "Toggle side-by-side git hunk diff (F5)",
        [](ActionContext& ctx) {
            ctx.engine.open_hunk_diff();
            return true;
        }
    );

    register_action("hunk_next", {"next_hunk"}, "Git", "Jump to next git hunk",
        [](ActionContext& ctx) {
            ctx.engine.jump_to_next_hunk();
            return true;
        }
    );

    register_action("hunk_prev", {"prev_hunk"}, "Git", "Jump to previous git hunk",
        [](ActionContext& ctx) {
            ctx.engine.jump_to_prev_hunk();
            return true;
        }
    );

    register_action("hunk_popup", {}, "Git", "Open git hunk diff popup (F4)",
        [](ActionContext& ctx) {
            ctx.engine.open_git_hunk_popup();
            return true;
        }
    );

    register_action("revert_hunk", {}, "Git", "Revert active git hunk",
        [](ActionContext& ctx) {
            ctx.engine.revert_active_hunk();
            return true;
        }
    );

    register_action("buffer_list", {"buffers", "ls"}, "Buffer", "Open buffer list popup (Alt-b)",
        [](ActionContext& ctx) {
            ctx.engine.open_buffer_list();
            return true;
        }
    );

    register_action("next_buffer", {"bn"}, "Buffer", "Switch to next buffer",
        [](ActionContext& ctx) {
            ctx.engine.next_buffer();
            return true;
        }
    );

    register_action("prev_buffer", {"bp"}, "Buffer", "Switch to previous buffer",
        [](ActionContext& ctx) {
            ctx.engine.prev_buffer();
            return true;
        }
    );

    register_action("settings", {"options"}, "View", "Open settings popup (F9)",
        [](ActionContext& ctx) {
            ctx.engine.handle_key_input(ncinput{}, NCKEY_F09);
            return true;
        }
    );

    register_action("theme_select", {"theme_popup", "themes"}, "View", "Open theme selection popup (F6)",
        [](ActionContext& ctx) {
            ctx.engine.open_theme_popup();
            return true;
        }
    );

    register_action("ws_list", {"ws", "workspaces", "workspace"}, "Workspace", "Open workspace slots list (Alt-0 / :ws)",
        [](ActionContext& ctx) {
            ctx.engine.open_workspace_list();
            return true;
        }
    );

    register_action("ws_save", {}, "Workspace", "Save current session into active workspace slot",
        [](ActionContext& ctx) {
            if (ctx.engine.workspace_slot >= 0) {
                ctx.engine.save_workspace(ctx.engine.workspace_slot);
            } else {
                ctx.engine.open_workspace_list();
            }
            return true;
        }
    );

    register_action("ws_clear", {}, "Workspace", "Clear active workspace marker",
        [](ActionContext& ctx) {
            ctx.engine.clear_active_workspace();
            return true;
        }
    );

    register_action("ws_load_0", {"ws0"}, "Workspace", "Load workspace slot 0 (Alt-0)",
        [](ActionContext& ctx) {
            ctx.engine.load_workspace(0);
            return true;
        }
    );

    register_action("ws_load_1", {"ws1"}, "Workspace", "Load workspace slot 1 (Alt-1)",
        [](ActionContext& ctx) {
            ctx.engine.load_workspace(1);
            return true;
        }
    );

    register_action("ws_load_2", {"ws2"}, "Workspace", "Load workspace slot 2 (Alt-2)",
        [](ActionContext& ctx) {
            ctx.engine.load_workspace(2);
            return true;
        }
    );

    register_action("ws_load_3", {"ws3"}, "Workspace", "Load workspace slot 3 (Alt-3)",
        [](ActionContext& ctx) {
            ctx.engine.load_workspace(3);
            return true;
        }
    );

    register_action("ws_load_4", {"ws4"}, "Workspace", "Load workspace slot 4 (Alt-4)",
        [](ActionContext& ctx) {
            ctx.engine.load_workspace(4);
            return true;
        }
    );

    register_action("search_next", {"n"}, "Search", "Jump to next search match (n)",
        [](ActionContext& ctx) {
            ctx.engine.search_jump_next();
            return true;
        }
    );

    register_action("search_prev", {"N"}, "Search", "Jump to previous search match (N)",
        [](ActionContext& ctx) {
            ctx.engine.search_jump_prev();
            return true;
        }
    );

    register_action("ripgrep", {"grep", "vg"}, "Search", "Search project with ripgrep (F11)",
        [](ActionContext& ctx) {
            std::string w = ctx.engine.get_word_under_cursor();
            if (!w.empty()) ctx.engine.run_ripgrep(w);
            else ctx.engine.execute_command("vg");
            return true;
        }
    );

    register_action("help", {}, "View", "Toggle mini help overlay (F12)",
        [](ActionContext& ctx) {
            ctx.engine.handle_key_input(ncinput{}, NCKEY_F12);
            return true;
        }
    );

    register_action("split_h", {"split_horizontal", "sp"}, "Window", "Split window horizontally (Alt-s)",
        [](ActionContext& ctx) {
            ctx.engine.split_window(SplitType::HORIZONTAL);
            return true;
        }
    );

    register_action("split_v", {"split_vertical", "vsp"}, "Window", "Split window vertically (Alt-v)",
        [](ActionContext& ctx) {
            ctx.engine.split_window(SplitType::VERTICAL);
            return true;
        }
    );

    register_action("close_window", {}, "Window", "Close active split window (Alt-x)",
        [](ActionContext& ctx) {
            ctx.engine.close_active_window();
            return true;
        }
    );

    register_action("indent", {}, "Edit", "Reindent entire buffer",
        [](ActionContext& ctx) {
            ctx.engine.execute_command("indent");
            return true;
        }
    );

    register_action("line_start", {"0", "^", "<home>"}, "Navigation", "Move cursor to line start",
        [](ActionContext& ctx) {
            for (auto& c : ctx.engine.active_win().cursors) c.x = 0;
            return true;
        }
    );

    register_action("line_end", {"$", "<end>"}, "Navigation", "Move cursor to line end",
        [](ActionContext& ctx) {
            for (auto& c : ctx.engine.active_win().cursors) {
                c.x = ctx.engine.active_win().get_max_x(ctx.engine.active_buf(), c.y, ctx.mode);
            }
            return true;
        }
    );

    register_action("match_bracket", {"%", "bracket_match", "matchpair"}, "Navigation", "Jump to matching bracket (%)",
        [](ActionContext& ctx) {
            jump_matching_bracket(ctx.engine, ctx.mode);
            return true;
        }
    );

    register_action("top_of_file", {"top", "gg"}, "Navigation", "Jump to top of file (gg)",
        [](ActionContext& ctx) {
            for (auto& c : ctx.engine.active_win().cursors) { c.y = 0; c.x = 0; }
            ctx.engine.active_win().clamp_all_cursors(ctx.engine.active_buf(), ctx.mode);
            ctx.engine.update_window_scroll(ctx.engine.active_win(), ctx.engine.active_buf());
            ctx.engine.set_info_msg("Top of file (gg)");
            return true;
        }
    );

    register_action("end_of_file", {"bottom", "g", "G"}, "Navigation", "Jump to end of file (G)",
        [](ActionContext& ctx) {
            int ly = std::max(0, static_cast<int>(ctx.engine.active_buf().lines.size()) - 1);
            for (auto& c : ctx.engine.active_win().cursors) { c.y = ly; c.x = 0; }
            ctx.engine.active_win().clamp_all_cursors(ctx.engine.active_buf(), ctx.mode);
            ctx.engine.update_window_scroll(ctx.engine.active_win(), ctx.engine.active_buf());
            ctx.engine.set_info_msg("End of file (G)");
            return true;
        }
    );

    register_action("delete_line", {"dd"}, "Edit", "Delete line at cursor (dd)",
        [](ActionContext& ctx) {
            auto& win = ctx.engine.active_win();
            auto& buf = ctx.engine.active_buf();
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
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, ctx.mode);
            win.deduplicate_cursors();
            ctx.engine.update_window_scroll(win, buf);
            ctx.engine.set_info_msg("Line deleted.");
            return true;
        }
    );

    register_action("delete_char", {"x", "del"}, "Edit",
        "Delete char under cursor (x/Del); in insert mode at EOL, join next line",
        [](ActionContext& ctx) {
            auto& win = ctx.engine.active_win();
            auto& buf = ctx.engine.active_buf();
            buf.push_undo(win.cursors);

            // Top-to-bottom, left-to-right: when we join line y with line y+1
            // the removal of index y+1 only affects cursors that come later in
            // the list, which we have not touched yet.
            std::sort(win.cursors.begin(), win.cursors.end(),
                      [](const Cursor& a, const Cursor& b) {
                          if (a.y != b.y) return a.y < b.y;
                          return a.x < b.x;
                      });

            std::string deleted_text;
            bool any_deleted = false;
            bool any_joined = false;
            for (size_t i = 0; i < win.cursors.size(); ++i) {
                Cursor& c = win.cursors[i];
                if (c.y < 0 || c.y >= static_cast<int>(buf.lines.size())) continue;
                std::string& line = buf.lines[c.y];
                if (c.x < static_cast<int>(line.size())) {
                    // Cursor is over a real character: forward-delete it.
                    deleted_text += line[c.x];
                    line.erase(c.x, 1);
                    any_deleted = true;
                } else if (ctx.mode == Mode::INSERT && c.y + 1 < static_cast<int>(buf.lines.size())) {
                    // Insert-mode Del at end-of-line: join with the next line.
                    // (Normal-mode x deliberately does NOT join, to match Vim.)
                    line += buf.lines[c.y + 1];
                    buf.lines.erase(buf.lines.begin() + c.y + 1);
                    for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                        if (win.cursors[j].y > c.y) win.cursors[j].y -= 1;
                    }
                    any_joined = true;
                }
            }

            if (any_deleted || any_joined) {
                ctx.engine.yank_reg.is_linewise = false;
                ctx.engine.yank_reg.text = deleted_text;
                ctx.engine.yank_reg.lines = {deleted_text};
                buf.modified = true;
                buf.version++;
                buf.invalidate_hunks();
                if (buf.syntax) buf.syntax->update_text(buf.lines);
            }
            std::sort(win.cursors.begin(), win.cursors.end());
            win.clamp_all_cursors(buf, ctx.mode);
            win.deduplicate_cursors();
            ctx.engine.update_window_scroll(win, buf);
            ctx.engine.set_info_msg(any_joined ? "Deleted to next line (Del)" : "Deleted char (x)");
            return true;
        }
    );

    register_action("delete_char_before", {"X"}, "Edit", "Delete character before cursor (X)",
        [](ActionContext& ctx) {
            auto& win = ctx.engine.active_win();
            auto& buf = ctx.engine.active_buf();
            buf.push_undo(win.cursors);
            std::sort(win.cursors.begin(), win.cursors.end());
            std::string deleted_text;
            bool any_deleted = false;
            for (auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    std::string& line = buf.lines[c.y];
                    if (!line.empty() && c.x > 0 && c.x <= static_cast<int>(line.size())) {
                        deleted_text += line[c.x - 1];
                        line.erase(c.x - 1, 1);
                        c.x--;
                        any_deleted = true;
                    }
                }
            }
            if (any_deleted) {
                ctx.engine.yank_reg.is_linewise = false;
                ctx.engine.yank_reg.text = deleted_text;
                ctx.engine.yank_reg.lines = {deleted_text};
                buf.modified = true;
                buf.version++;
                buf.invalidate_hunks();
                if (buf.syntax) buf.syntax->update_text(buf.lines);
            }
            win.clamp_all_cursors(buf, ctx.mode);
            win.deduplicate_cursors();
            ctx.engine.update_window_scroll(win, buf);
            ctx.engine.set_info_msg("Deleted char before (X)");
            return true;
        }
    );

    register_action("comment_toggle", {"comment", "toggle_comment", "gc"}, "Edit",
        "Toggle line comments on current line or selection (F2)",
        [](ActionContext& ctx) {
            ctx.engine.toggle_line_comments();
            return true;
        }
    );
}