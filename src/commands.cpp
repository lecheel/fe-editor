#include "command.hpp"
#include "engine.hpp"
#include "util/indent.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

REGISTER_COMMAND(
    export_func,
    (std::vector<std::string>{"export_func", "export_actions", "funcs", "actions"}),
    "Export bindable functions for keymap (:export_func [json|buffer])",
    ([](CommandContext& ctx) {
        const auto& actions = ActionRegistry::instance().get_canonical_actions();
        std::string mode = ctx.argv.empty() ? "buffer" : ctx.argv[0];

        if (mode == "json") {
            std::vector<std::string> out_lines = {
                "{",
                "  \"actions\": ["
            };
            size_t idx = 0;
            for (auto it = actions.begin(); it != actions.end(); ++it, ++idx) {
                const auto& act = it->second;
                out_lines.push_back("    {");
                out_lines.push_back("      \"name\": \"" + act->canonical_name + "\",");
                out_lines.push_back("      \"category\": \"" + act->category + "\",");
                out_lines.push_back("      \"description\": \"" + act->description + "\",");
                std::string a_str = "      \"aliases\": [";
                for (size_t a = 0; a < act->aliases.size(); ++a) {
                    a_str += "\"" + act->aliases[a] + "\"" + (a + 1 < act->aliases.size() ? ", " : "");
                }
                a_str += "]";
                out_lines.push_back(a_str);
                out_lines.push_back(std::string("    }") + (idx + 1 < actions.size() ? "," : ""));
            }
            out_lines.push_back("  ]");
            out_lines.push_back("}");

            std::string buf_name = "exported_actions.json";
            ctx.engine.execute_command("e " + buf_name);
            ctx.engine.active_buf().lines = std::move(out_lines);
            if (ctx.engine.active_buf().syntax) {
                ctx.engine.active_buf().syntax->init_for_file("actions.json");
                ctx.engine.active_buf().syntax->update_text(ctx.engine.active_buf().lines);
            }
            ctx.engine.active_win().cursors = {{0, 0}};
            ctx.engine.update_window_scroll(ctx.engine.active_win(), ctx.engine.active_buf());
            ctx.engine.set_info_msg("Exported " + std::to_string(actions.size()) + " actions to " + buf_name);
            return;
        }

        std::vector<std::string> doc_lines = {
            "# Bindable Functions Reference for keymap.json",
            "# Usage: add to ~/.config/fe/keymap.json under \"normal\", \"insert\", or \"visual\":",
            "#   \"<chord>\": \"<action_name>\"",
            ""
        };

        std::string current_category;
        for (const auto& pair : actions) {
            const auto& act = pair.second;
            if (act->category != current_category) {
                current_category = act->category;
                doc_lines.push_back("## " + current_category);
            }
            std::string line = "  - `" + act->canonical_name + "`";
            if (!act->aliases.empty()) {
                line += " (aliases: ";
                for (size_t i = 0; i < act->aliases.size(); ++i) {
                    line += act->aliases[i] + (i + 1 < act->aliases.size() ? ", " : "");
                }
                line += ")";
            }
            line += " - " + act->description;
            doc_lines.push_back(line);
        }

        std::string buf_name = "actions_reference.md";
        ctx.engine.execute_command("e " + buf_name);
        ctx.engine.active_buf().lines = std::move(doc_lines);
        if (ctx.engine.active_buf().syntax) {
            ctx.engine.active_buf().syntax->init_for_file("actions.md");
            ctx.engine.active_buf().syntax->update_text(ctx.engine.active_buf().lines);
        }
        ctx.engine.active_win().cursors = {{0, 0}};
        ctx.engine.update_window_scroll(ctx.engine.active_win(), ctx.engine.active_buf());
        ctx.engine.set_info_msg("Exported " + std::to_string(actions.size()) + " actions to " + buf_name);
    })
);

REGISTER_COMMAND(
    copy_clipboard,
    (std::vector<std::string>{"cp", "copy", "clipboard"}),
    "Copy current line or selection to system clipboard (:cp)",
    [](CommandContext& ctx) {
        ctx.engine.copy_selection_to_clipboard();
    }
);

REGISTER_COMMAND(
    full_replace_paste,
    (std::vector<std::string>{"pp", "replace_paste", "pastereplace"}),
    "Replace entire buffer with clipboard contents (:pp)",
    [](CommandContext& ctx) {
        ctx.engine.paste_full_replace();
    }
);

REGISTER_COMMAND(
    paste_clipboard,
    (std::vector<std::string>{"pv", "paste", "bracketpaste"}),
    "Paste from clipboard at cursor (:pv)",
    [](CommandContext& ctx) {
        ctx.engine.paste_from_clipboard(true);
    }
);

REGISTER_COMMAND(
    nohl,
    (std::vector<std::string>{"nohl", "noh", "nohlsearch"}),
    "Clear search pattern highlights (:noh)",
    [](CommandContext& ctx) {
        ctx.engine.clear_search_highlights();
    }
);

REGISTER_COMMAND(
    workspace,
    (std::vector<std::string>{"ws", "workspace", "workspaces"}),
    "Open workspace manager popup (:ws [save|load|clear <slot>])",
    ([](CommandContext& ctx) {
        if (!ctx.argv.empty()) {
            std::string sub = ctx.argv[0];
            if (sub == "save" && ctx.argv.size() >= 2) {
                int slot = std::clamp(std::stoi(ctx.argv[1]), 0, 4);
                ctx.engine.save_workspace(slot);
                return;
            } else if (sub == "load" && ctx.argv.size() >= 2) {
                int slot = std::clamp(std::stoi(ctx.argv[1]), 0, 4);
                ctx.engine.load_workspace(slot);
                return;
            } else if (sub == "clear") {
                ctx.engine.clear_active_workspace();
                return;
            }
        }
        ctx.engine.open_workspace_list();
    })
);

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
    keymap,
    (std::vector<std::string>{"keymap", "keybind", "bind"}),
    "Open or reload ~/.config/fe/keymap.json (:keymap [reload])",
    [](CommandContext& ctx) {
        if (!ctx.argv.empty() && (ctx.argv[0] == "reload" || ctx.argv[0] == "r")) {
            ctx.engine.keymap.load(ctx.engine.get_config().get_config_dir());
            ctx.engine.set_info_msg("Reloaded keymap from " + ctx.engine.keymap.keymap_path);
        } else {
            std::string path = ctx.engine.keymap.keymap_path;
            if (path.empty()) {
                path = (fs::path(ctx.engine.get_config().get_config_dir()) / "keymap.json").string();
            }
            ctx.engine.execute_command("e " + path);
            ctx.engine.set_info_msg("Opened keymap: " + path + " (use :keymap reload after saving)");
        }
    }
);

REGISTER_COMMAND(
    theme,
    (std::vector<std::string>{"theme", "colorscheme", "cs"}),
    "Set or show editor theme (:theme [name])",
    [](CommandContext& ctx) {
        if (ctx.argv.empty()) {
            ctx.engine.open_theme_popup();
            return;
        }
        std::string name = ctx.argv[0];
        if (HelixTheme::instance().load_theme(name, ctx.engine.get_config().get_config_dir())) {
            ctx.engine.get_config().settings.theme = name;
            ctx.engine.get_config().save();
            for (auto& b : ctx.engine.get_buffers()) {
                if (b->syntax) {
                    b->syntax->update_text(b->lines);
                }
            }
            ctx.engine.set_info_msg("Theme set to: " + name);
        } else {
            ctx.engine.set_info_msg("Could not load theme: " + name);
        }
    }
);

REGISTER_COMMAND(
    indent,
    (std::vector<std::string>{"indent", "retab"}),
    "Reindent entire file according to filetype",
    [](CommandContext& ctx) {
        auto& win = ctx.engine.active_win();
        auto& buf = ctx.engine.active_buf();
        buf.push_undo(win.cursors);
        std::string lang = buf.syntax ? buf.syntax->get_language() : "";
        if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
        for (int y = 0; y < static_cast<int>(buf.lines.size()); ++y) {
            std::string ind = compute_line_indent(buf.lines, y, lang);
            size_t p = 0;
            while (p < buf.lines[y].size() && (buf.lines[y][p] == ' ' || buf.lines[y][p] == '\t')) p++;
            buf.lines[y] = ind + buf.lines[y].substr(p);
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        ctx.engine.update_window_scroll(win, buf);
        ctx.engine.set_info_msg("Reindented entire file (" + std::to_string(buf.lines.size()) + " lines)");
    }
);

REGISTER_COMMAND(
    togglecomment,
    (std::vector<std::string>{"comment", "togglecomment", "gc"}),
    "Toggle line comments on current line or selection (:comment)",
    [](CommandContext& ctx) {
        ctx.engine.toggle_line_comments();
    }
);

REGISTER_COMMAND(
    hunkdiff,
    (std::vector<std::string>{"hunkdiff", "diff"}),
    "Toggle side-by-side diff against HEAD (F5) or compare files (:diff file1 file2)",
    [](CommandContext& ctx) {
        if (ctx.argv.size() >= 2) {
            ctx.engine.open_file_diff(ctx.argv[0], ctx.argv[1]);
        } else if (ctx.argv.size() == 1) {
            auto& b = ctx.engine.active_buf();
            std::string left = !b.file_path.empty() ? b.file_path : b.name;
            ctx.engine.open_file_diff(left, ctx.argv[0]);
        } else {
            ctx.engine.open_hunk_diff();
        }
    }
);

REGISTER_COMMAND(
    delta,
    (std::vector<std::string>{"delta", "diffsplit", "vdiff"}),
    "Compare two files side-by-side (:delta file1 file2)",
    [](CommandContext& ctx) {
        if (ctx.argv.size() >= 2) {
            ctx.engine.open_file_diff(ctx.argv[0], ctx.argv[1]);
        } else if (ctx.argv.size() == 1) {
            auto& b = ctx.engine.active_buf();
            std::string left = !b.file_path.empty() ? b.file_path : b.name;
            ctx.engine.open_file_diff(left, ctx.argv[0]);
        } else {
            ctx.engine.open_hunk_diff();
        }
    }
);

REGISTER_COMMAND(
    gitview,
    (std::vector<std::string>{"git", "gitstatus", "gitview", "gs"}),
    "Open git status view (F1)",
    [](CommandContext& ctx) {
        ctx.engine.open_git_status();
    }
);