#include "engine.hpp"
#include "action.hpp"
#include "command.hpp"
#include "autocomplete.hpp"
#include "keymap.hpp"
#include "log.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>

extern int g_hunk_marker_style;

namespace fs = std::filesystem;
using namespace Keymap;

namespace {

struct KeymapJsonToken {
    enum Type { LBRACE, RBRACE, COLON, COMMA, STRING, NUMBER, OTHER } type;
    std::string value;
};

std::vector<KeymapJsonToken> tokenize_keymap_json(const std::string& text) {
    std::vector<KeymapJsonToken> tokens;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if (c == '{') {
            tokens.push_back({KeymapJsonToken::LBRACE, "{"});
            i++;
        } else if (c == '}') {
            tokens.push_back({KeymapJsonToken::RBRACE, "}"});
            i++;
        } else if (c == ':') {
            tokens.push_back({KeymapJsonToken::COLON, ":"});
            i++;
        } else if (c == ',') {
            tokens.push_back({KeymapJsonToken::COMMA, ","});
            i++;
        } else if (c == '"') {
            i++;
            std::string s;
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    s += text[i + 1];
                    i += 2;
                } else if (text[i] == '"') {
                    i++;
                    break;
                } else {
                    s += text[i++];
                }
            }
            tokens.push_back({KeymapJsonToken::STRING, s});
        } else {
            i++;
        }
    }
    return tokens;
}

} // namespace

void KeymapConfig::load(const std::string& config_dir) {
    keymap_path = (fs::path(config_dir) / "keymap.json").string();
    normal_map.clear();
    insert_map.clear();
    visual_map.clear();

    std::error_code ec;
    if (!fs::exists(keymap_path, ec)) {
        if (fs::exists("keymap.json", ec)) {
            keymap_path = "keymap.json";
        } else {
            fs::create_directories(config_dir, ec);
            std::ofstream out(keymap_path);
            if (out.is_open()) {
                out << "{\n"
                    << "  \"normal\": {\n"
                    << "    \"<C-s>\": \":w\",\n"
                    << "    \"<Space>w\": \":w\",\n"
                    << "    \"<Space>q\": \":q\",\n"
                    << "    \"<Space>f\": \"filepicker\",\n"
                    << "    \"<Space>g\": \"ripgrep\",\n"
                    << "    \"<Space>d\": \"hunk_diff\",\n"
                    << "    \"<Space>u\": \"undo\",\n"
                    << "    \"<A-0>\": \"ws_list\",\n"
                    << "    \"H\": \"0\",\n"
                    << "    \"L\": \"$\"\n"
                    << "  },\n"
                    << "  \"insert\": {\n"
                    << "    \"<C-s>\": \":w\",\n"
                    << "    \"<A-u>\": \"undo\",\n"
                    << "    \"<A-d>\": \"delete_line\"\n"
                    << "  },\n"
                    << "  \"visual\": {\n"
                    << "    \"<C-s>\": \":w\",\n"
                    << "    \"H\": \"0\",\n"
                    << "    \"L\": \"$\"\n"
                    << "  }\n"
                    << "}\n";
            }
        }
    }

    std::ifstream in(keymap_path);
    if (!in.is_open()) return;

    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    auto tokens = tokenize_keymap_json(content);

    std::string current_mode_section;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == KeymapJsonToken::STRING &&
            i + 2 < tokens.size() &&
            tokens[i + 1].type == KeymapJsonToken::COLON &&
            tokens[i + 2].type == KeymapJsonToken::LBRACE) {
            current_mode_section = tokens[i].value;
            for (char& c : current_mode_section) c = std::tolower(static_cast<unsigned char>(c));
            i += 2;
            continue;
        }

        if (tokens[i].type == KeymapJsonToken::RBRACE) {
            current_mode_section.clear();
            continue;
        }

        if (!current_mode_section.empty() &&
            tokens[i].type == KeymapJsonToken::STRING &&
            i + 2 < tokens.size() &&
            tokens[i + 1].type == KeymapJsonToken::COLON &&
            tokens[i + 2].type == KeymapJsonToken::STRING) {

            std::string key_str = Keymap::normalize_key_chord(tokens[i].value);
            std::string action_str = tokens[i + 2].value;

            if (current_mode_section == "normal" || current_mode_section == "norm" || current_mode_section == "n") {
                normal_map[key_str] = action_str;
            } else if (current_mode_section == "insert" || current_mode_section == "ins" || current_mode_section == "i") {
                insert_map[key_str] = action_str;
            } else if (current_mode_section == "visual" || current_mode_section == "vis" || current_mode_section == "v" ||
                       current_mode_section == "visual_block") {
                visual_map[key_str] = action_str;
            }
            i += 2;
        }
    }
}

void ActionRegistry::init_default_actions() {
    register_action("save", {"write", "w"}, "File", "Save active buffer to disk",
        [](ActionContext& ctx) {
            if (ctx.engine.active_buf().save_to_file()) {
                ctx.engine.save_window_position(ctx.engine.active_win(), ctx.engine.active_buf());
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

    register_action("git_status", {"git", "gitview", "gs"}, "Git", "Open git status view (F1/F6)",
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

    register_action("delete_char", {"x", "del"}, "Edit", "Delete character under cursor (x/Del)",
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
                    if (!line.empty() && c.x < static_cast<int>(line.size())) {
                        deleted_text += line[c.x];
                        line.erase(c.x, 1);
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
            ctx.engine.set_info_msg("Deleted char (x)");
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
}

bool KeymapConfig::execute_action(VimEngine& engine, Mode mode, const std::string& action) {
    if (action.empty()) return false;

    if (action.front() == ':') {
        engine.execute_command(action.substr(1));
        return true;
    }

    std::string act = action;
    for (char& c : act) c = std::tolower(static_cast<unsigned char>(c));

    if (ActionRegistry::instance().execute(act, engine, mode)) {
        return true;
    }

    engine.execute_command(action);
    return true;
}

bool KeymapConfig::has_mapping(Mode mode, const ncinput& ni, uint32_t key) const {
    std::string k_str = Keymap::key_to_string(ni, key);
    if (k_str.empty()) return false;

    const std::unordered_map<std::string, std::string>* target_map = nullptr;
    if (mode == Mode::NORMAL) target_map = &normal_map;
    else if (mode == Mode::INSERT) target_map = &insert_map;
    else if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) target_map = &visual_map;

    if (!target_map) return false;
    return target_map->find(k_str) != target_map->end();
}

bool KeymapConfig::handle_key(VimEngine& engine, Mode mode, const ncinput& ni, uint32_t key) {
    std::string k_str = Keymap::key_to_string(ni, key);
    if (k_str.empty()) return false;

    const std::unordered_map<std::string, std::string>* target_map = nullptr;
    if (mode == Mode::NORMAL) target_map = &normal_map;
    else if (mode == Mode::INSERT) target_map = &insert_map;
    else if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) target_map = &visual_map;

    if (!target_map) return false;

    auto it = target_map->find(k_str);
    if (it != target_map->end()) {
        return execute_action(engine, mode, it->second);
    }
    return false;
}

bool KeymapConfig::handle_whichkey(VimEngine& engine, const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key)) {
        engine.set_info_msg("");
        return true;
    }

    std::string k_str = Keymap::key_to_string(ni, key);
    if (k_str.empty()) return false;

    std::string chord = "<Space>" + k_str;
    auto it = normal_map.find(chord);
    if (it != normal_map.end()) {
        execute_action(engine, Mode::NORMAL, it->second);
        return true;
    }
    return false;
}

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

            std::string buf_name = "*exported_actions.json*";
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

        std::string buf_name = "*actions_reference.md*";
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

struct IndentInfo {
    bool use_tabs{false};
    int tab_size{4};
    std::string unit{"    "};
};

inline IndentInfo get_indent_info_for_lang(const std::string& lang) {
    IndentInfo info;
    if (lang == "go") {
        info.use_tabs = true;
        info.tab_size = 4;
        info.unit = "\t";
    } else if (lang == "json" || lang == "yaml" || lang == "yml" ||
               lang == "html" || lang == "css" || lang == "toml" ||
               lang == "javascript" || lang == "typescript" || lang == "lua") {
        info.use_tabs = false;
        info.tab_size = 2;
        info.unit = "  ";
    } else {
        info.use_tabs = false;
        info.tab_size = 4;
        info.unit = "    ";
    }
    return info;
}

bool VimEngine::handle_global_shortcuts(const ncinput& ni, uint32_t key) {
    if (is_alt(ni, key, 'q')) {
        running = false;
        return true;
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

void VimEngine::handle_key_input(const ncinput& ni, uint32_t key) {
    if (Keymap::is_modifier_key(key)) {
        return;
    }

    if (!ni.ctrl && !ni.alt && ni.shift) {
        uint32_t raw = (key >= 32 && key < 127) ? key : ni.id;
        if (raw >= 32 && raw < 127) {
            key = static_cast<uint32_t>(Keymap::get_shifted_ascii(static_cast<char>(raw)));
        }
    }

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

    // 2. Git Status view (F1 / F6)
    if (is_fkey(ni, key, 1) || is_fkey(ni, key, 6)) {
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

    // 4. Hunk navigation shortcuts (F2 / F3)
    if (is_fkey(ni, key, 2)) {
        jump_to_prev_hunk();
        return;
    }
    if (is_fkey(ni, key, 3)) {
        jump_to_next_hunk();
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
        } else {
            show_filepicker = false;
            show_settings_popup = false;
            show_git_hunk_popup = false;
            show_whichkey_popup = false;
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

    // 10. Leader key WhichKey popup / Window Ops popup
    if (leader_pending || ctrl_w_pending) {
        handle_whichkey_popup(ni, key);
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
            if (!git_is_clean(root)) {
                git_stash_status_msg = "Working tree not clean — commit or stash first";
                return;
            }
            if (git_stash_pop(root, git_stash_action_idx)) {
                git_stash_action_active = false;
                git_stash_status_msg.clear();
                refresh_git_status();
                git_status_msg = "Popped " + git_stash_action_ref;
            } else {
                git_stash_status_msg = "Failed to pop " + git_stash_action_ref;
            }
            return;
        }

        if (key == 'd' || key == 'D') {
            if (!git_is_clean(root)) {
                git_stash_status_msg = "Working tree not clean — commit or stash first";
                return;
            }
            if (git_stash_drop(root, git_stash_action_idx)) {
                git_stash_action_active = false;
                git_stash_status_msg.clear();
                refresh_git_status();
                git_status_msg = "Dropped " + git_stash_action_ref;
            } else {
                git_stash_status_msg = "Failed to drop " + git_stash_action_ref;
            }
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
        if (git_stash_push(root)) {
            refresh_git_status();
            git_status_msg = "Stashed working changes";
        } else {
            git_status_msg = "Failed to stash changes";
        }
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

void VimEngine::handle_filepicker_input(const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key)) {
        show_filepicker = false;
        set_info_msg("");
        return;
    }

    if (is_enter(ni, key)) {
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

    if (key == NCKEY_UP || is_ctrl(ni, key, 'p') || is_ctrl(ni, key, 'k')) {
        if (filepicker_selected_idx > 0) {
            filepicker_selected_idx--;
        } else if (!filepicker_filtered_files.empty()) {
            filepicker_selected_idx = static_cast<int>(filepicker_filtered_files.size()) - 1;
        }
        return;
    }

    if (key == NCKEY_DOWN || is_ctrl(ni, key, 'n') || is_ctrl(ni, key, 'j')) {
        if (filepicker_selected_idx + 1 < static_cast<int>(filepicker_filtered_files.size())) {
            filepicker_selected_idx++;
        } else {
            filepicker_selected_idx = 0;
        }
        return;
    }

    if (is_backspace(ni, key)) {
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

void VimEngine::handle_rg_popup_input(const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key) || is_fkey(ni, key, 11) || ((key == 'q' || key == 'Q') && !rg_replace_active)) {
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
        rg_selected_display_idx = std::min(static_cast<int>(rg_display_lines.size()) - 1, rg_selected_display_idx + 10);
        if (rg_selected_display_idx >= 0 && rg_selected_display_idx < static_cast<int>(rg_display_lines.size())) {
            int midx = rg_display_lines[rg_selected_display_idx].match_idx;
            if (midx >= 0) rg_selected_match_idx = midx;
        }
        return;
    }

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

    const int total_items = 7;
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
        }
        config.save();
        for (auto& w : windows) {
            if (w.buffer_idx < buffers.size()) {
                update_window_scroll(w, *buffers[w.buffer_idx]);
            }
        }
    }
}

inline std::string compute_line_indent(const std::vector<std::string>& lines, int line_idx, const std::string& lang) {
    if (line_idx <= 0 || lines.empty()) return "";
    IndentInfo info = get_indent_info_for_lang(lang);

    int prev_idx = line_idx - 1;
    while (prev_idx >= 0) {
        bool all_space = true;
        for (char c : lines[prev_idx]) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                all_space = false;
                break;
            }
        }
        if (!all_space) break;
        prev_idx--;
    }
    if (prev_idx < 0) return "";

    const std::string& prev = lines[prev_idx];
    size_t p = 0;
    while (p < prev.size() && (prev[p] == ' ' || prev[p] == '\t')) p++;
    std::string base_indent = prev.substr(0, p);
    std::string trimmed_prev = prev.substr(p);

    if (lang == "python" || lang == "bash") {
        size_t h = trimmed_prev.find('#');
        if (h != std::string::npos) trimmed_prev = trimmed_prev.substr(0, h);
    } else {
        size_t c = trimmed_prev.find("//");
        if (c != std::string::npos) trimmed_prev = trimmed_prev.substr(0, c);
    }
    while (!trimmed_prev.empty() && std::isspace(static_cast<unsigned char>(trimmed_prev.back()))) {
        trimmed_prev.pop_back();
    }

    if (!trimmed_prev.empty()) {
        char b = trimmed_prev.back();
        if (b == '{' || b == '(' || b == '[' || (lang == "python" && b == ':')) {
            base_indent += info.unit;
        }
    }

    if (line_idx < static_cast<int>(lines.size())) {
        const std::string& cur = lines[line_idx];
        size_t cp = 0;
        while (cp < cur.size() && (cur[cp] == ' ' || cur[cp] == '\t')) cp++;
        std::string trimmed_cur = cur.substr(cp);

        if (!trimmed_cur.empty() && (trimmed_cur[0] == '}' || trimmed_cur[0] == ')' || trimmed_cur[0] == ']')) {
            if (base_indent.size() >= info.unit.size() &&
                base_indent.compare(base_indent.size() - info.unit.size(), info.unit.size(), info.unit) == 0) {
                base_indent.erase(base_indent.size() - info.unit.size());
            } else if (base_indent.size() >= static_cast<size_t>(info.tab_size)) {
                base_indent.erase(base_indent.size() - info.tab_size);
            }
        } else if (lang == "python" && !trimmed_cur.empty()) {
            if (trimmed_cur.rfind("elif", 0) == 0 ||
                trimmed_cur.rfind("else:", 0) == 0 ||
                trimmed_cur.rfind("except", 0) == 0 ||
                trimmed_cur.rfind("finally:", 0) == 0) {
                if (base_indent.size() >= info.unit.size()) {
                    base_indent.erase(base_indent.size() - info.unit.size());
                }
            }
        }
    }

    return base_indent;
}

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
    DB,
    DE,
    D_CARET,
    D_ZERO,
    D_TOP,
    D_END,
    D_DOLLAR,
    X,
    CAP_X,
    DJ,
    DK
};

DotCommand s_last_dot_cmd = DotCommand::NONE;
bool s_d_pending = false;
bool s_dg_pending = false;
bool s_y_pending = false;
bool s_g_pending = false;
bool s_visual_g_pending = false;
bool s_equal_pending = false;
bool s_greater_pending = false;
bool s_less_pending = false;

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
    } else if (cmd == DotCommand::DB) {
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x > 0) {
                    int p = std::min(c.x, static_cast<int>(line.size()));
                    while (p > 0 && std::isspace(static_cast<unsigned char>(line[p - 1]))) p--;
                    auto is_word = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
                    if (p > 0) {
                        bool word = is_word(line[p - 1]);
                        while (p > 0 && is_word(line[p - 1]) == word && !std::isspace(static_cast<unsigned char>(line[p - 1]))) p--;
                    }
                    line.erase(p, c.x - p);
                    c.x = p;
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
        if (!is_repeat) s_last_dot_cmd = DotCommand::DB;
        engine.set_info_msg(is_repeat ? "Repeated: db" : "Deleted word backward (db)");
    } else if (cmd == DotCommand::DE) {
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                int len = static_cast<int>(line.size());
                if (c.x < len) {
                    int p = c.x;
                    auto is_word = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
                    if (std::isspace(static_cast<unsigned char>(line[p]))) {
                        while (p < len && std::isspace(static_cast<unsigned char>(line[p]))) p++;
                    } else {
                        bool word = is_word(line[p]);
                        while (p < len && is_word(line[p]) == word && !std::isspace(static_cast<unsigned char>(line[p]))) p++;
                    }
                    if (p > c.x) {
                        line.erase(c.x, p - c.x);
                    }
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
        if (!is_repeat) s_last_dot_cmd = DotCommand::DE;
        engine.set_info_msg(is_repeat ? "Repeated: de" : "Deleted to word end (de)");
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
    } else if (cmd == DotCommand::D_ZERO || cmd == DotCommand::D_TOP) {
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
        engine.set_info_msg(is_repeat ? "Repeated: dgg" : "Deleted to top of file (dgg)");
    } else if (cmd == DotCommand::DJ) {
        buf.push_undo(win.cursors);
        int cur_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        if (cur_y >= 0 && cur_y < static_cast<int>(buf.lines.size())) {
            int count = std::min(2, static_cast<int>(buf.lines.size()) - cur_y);
            buf.lines.erase(buf.lines.begin() + cur_y, buf.lines.begin() + cur_y + count);
            if (buf.lines.empty()) buf.lines.push_back("");
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::DJ;
        engine.set_info_msg(is_repeat ? "Repeated: dj" : "Deleted 2 lines (dj)");
    } else if (cmd == DotCommand::DK) {
        buf.push_undo(win.cursors);
        int cur_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        int start_y = std::max(0, cur_y - 1);
        int count = std::min(2, static_cast<int>(buf.lines.size()) - start_y);
        if (start_y < static_cast<int>(buf.lines.size())) {
            buf.lines.erase(buf.lines.begin() + start_y, buf.lines.begin() + start_y + count);
            if (buf.lines.empty()) buf.lines.push_back("");
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::DK;
        engine.set_info_msg(is_repeat ? "Repeated: dk" : "Deleted 2 lines (dk)");
    } else if (cmd == DotCommand::X) {
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        std::string deleted_text;
        bool any_deleted = false;
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (!line.empty() && c.x < static_cast<int>(line.size())) {
                    deleted_text += line[c.x];
                    line.erase(c.x, 1);
                    any_deleted = true;
                }
            }
        }
        if (any_deleted) {
            engine.yank_reg.is_linewise = false;
            engine.yank_reg.text = deleted_text;
            engine.yank_reg.lines = {deleted_text};
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
        }
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::X;
        engine.set_info_msg(is_repeat ? "Repeated: x" : "Deleted char (x)");
    } else if (cmd == DotCommand::CAP_X) {
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
            engine.yank_reg.is_linewise = false;
            engine.yank_reg.text = deleted_text;
            engine.yank_reg.lines = {deleted_text};
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
        }
        win.clamp_all_cursors(buf, Mode::NORMAL);
        win.deduplicate_cursors();
        engine.update_window_scroll(win, buf);
        if (!is_repeat) s_last_dot_cmd = DotCommand::CAP_X;
        engine.set_info_msg(is_repeat ? "Repeated: X" : "Deleted char before (X)");
    } else if (cmd == DotCommand::D_END) {
        buf.push_undo(win.cursors);
        int target_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        target_y = std::clamp(target_y, 0, static_cast<int>(buf.lines.size()) - 1);

        engine.yank_reg.is_linewise = true;
        engine.yank_reg.lines.clear();
        engine.yank_reg.text.clear();
        for (int y = target_y; y < static_cast<int>(buf.lines.size()); ++y) {
            engine.yank_reg.lines.push_back(buf.lines[y]);
            engine.yank_reg.text += buf.lines[y] + "\n";
        }

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
        std::string deleted_text;
        bool any_deleted = false;
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x < static_cast<int>(line.size())) {
                    deleted_text = line.substr(c.x);
                    line.erase(c.x);
                    any_deleted = true;
                }
            }
        }
        if (any_deleted) {
            engine.yank_reg.is_linewise = false;
            engine.yank_reg.text = deleted_text;
            engine.yank_reg.lines = {deleted_text};
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
        }
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
            for (auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    c.x = Keymap::utf8_prev_char(buf.lines[c.y], c.x);
                } else {
                    c.x = std::max(0, c.x - 1);
                }
            }
            return true;
        case NCKEY_RIGHT:
            for (auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    int max_x = win.get_max_x(buf, c.y, mode);
                    c.x = std::min(max_x, Keymap::utf8_next_char(buf.lines[c.y], c.x));
                }
            }
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

    if (!s_d_pending && !s_dg_pending && !s_y_pending && !s_g_pending &&
        !s_equal_pending && !s_greater_pending && !s_less_pending) {
        if (keymap.handle_key(*this, mode, ni, key)) {
            return;
        }
    }

    if (s_d_pending) {
        if (Keymap::is_modifier_key(key)) {
            return;
        }
        s_d_pending = false;
        if (key == 'd') {
            execute_dot_command(*this, DotCommand::DD, false);
            return;
        } else if (key == 'w') {
            execute_dot_command(*this, DotCommand::DW, false);
            return;
        } else if (key == 'b') {
            execute_dot_command(*this, DotCommand::DB, false);
            return;
        } else if (key == 'e') {
            execute_dot_command(*this, DotCommand::DE, false);
            return;
        } else if (key == 'j' || key == NCKEY_DOWN) {
            execute_dot_command(*this, DotCommand::DJ, false);
            return;
        } else if (key == 'k' || key == NCKEY_UP) {
            execute_dot_command(*this, DotCommand::DK, false);
            return;
        } else if (key == 'x' || key == 'l' || key == ' ' || key == NCKEY_DEL) {
            execute_dot_command(*this, DotCommand::X, false);
            return;
        } else if (key == 'h' || key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
            execute_dot_command(*this, DotCommand::CAP_X, false);
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

    if (s_equal_pending) {
        s_equal_pending = false;
        if (key == '=' || key == 'e') {
            buf.push_undo(win.cursors);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            std::set<int> lines_to_indent;
            for (const auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    lines_to_indent.insert(c.y);
                }
            }
            for (int y : lines_to_indent) {
                std::string ind = compute_line_indent(buf.lines, y, lang);
                size_t p = 0;
                while (p < buf.lines[y].size() && (buf.lines[y][p] == ' ' || buf.lines[y][p] == '\t')) p++;
                buf.lines[y] = ind + buf.lines[y].substr(p);
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Reindented line(s) (==)");
            return;
        } else if (key == 'G' || ni.id == 'G' || (ni.shift && (key == 'g' || ni.id == 'g'))) {
            buf.push_undo(win.cursors);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            int start_y = win.cursors.empty() ? 0 : win.cursors.front().y;
            int end_y = static_cast<int>(buf.lines.size()) - 1;
            for (int y = start_y; y <= end_y; ++y) {
                std::string ind = compute_line_indent(buf.lines, y, lang);
                size_t p = 0;
                while (p < buf.lines[y].size() && (buf.lines[y][p] == ' ' || buf.lines[y][p] == '\t')) p++;
                buf.lines[y] = ind + buf.lines[y].substr(p);
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Reindented to end of file (=G)");
            return;
        } else {
            set_info_msg("");
            if (key == NCKEY_ESC) return;
        }
    }

    if (s_greater_pending) {
        s_greater_pending = false;
        if (key == '>' || (ni.shift && (key == '.' || ni.id == '.'))) {
            buf.push_undo(win.cursors);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            IndentInfo info = get_indent_info_for_lang(lang);
            std::set<int> lines_to_shift;
            for (const auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    lines_to_shift.insert(c.y);
                }
            }
            for (int y : lines_to_shift) {
                buf.lines[y] = info.unit + buf.lines[y];
            }
            for (auto& c : win.cursors) {
                c.x += static_cast<int>(info.unit.size());
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Indented line (>>)");
            return;
        } else {
            set_info_msg("");
            if (key == NCKEY_ESC) return;
        }
    }

    if (s_less_pending) {
        s_less_pending = false;
        if (key == '<' || (ni.shift && (key == ',' || ni.id == ','))) {
            buf.push_undo(win.cursors);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            IndentInfo info = get_indent_info_for_lang(lang);
            std::set<int> lines_to_shift;
            for (const auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    lines_to_shift.insert(c.y);
                }
            }
            for (int y : lines_to_shift) {
                std::string& l = buf.lines[y];
                if (l.rfind(info.unit, 0) == 0) {
                    l.erase(0, info.unit.size());
                } else if (!l.empty() && l[0] == '\t') {
                    l.erase(0, 1);
                } else {
                    size_t sp = 0;
                    while (sp < l.size() && l[sp] == ' ' && sp < static_cast<size_t>(info.tab_size)) sp++;
                    if (sp > 0) l.erase(0, sp);
                }
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Unindented line (<<)");
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
        ctrl_w_pending = false;
        show_whichkey_popup = false;
        whichkey_mode = WhichKeyMode::LEADER;
        leader_start_time = std::chrono::steady_clock::now();
        return;
    }

    if (key == 23 || is_ctrl(ni, key, 'w')) {
        ctrl_w_pending = true;
        leader_pending = false;
        ctrl_w_start_time = std::chrono::steady_clock::now();
        show_whichkey_popup = true;
        whichkey_mode = WhichKeyMode::WINDOW;
        set_info_msg("Window [Ctrl-w]: [q] Close  [v] V-Split  [s] H-Split  [w] Next");
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
        cmd_cursor_pos = 0;
        cmd_history_idx = -1;
        cmd_history_draft.clear();
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
            for (auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    c.x = Keymap::utf8_prev_char(buf.lines[c.y], c.x);
                } else {
                    c.x = std::max(0, c.x - 1);
                }
            }
            break;
        case 'l':
            jump_to_next_hunk();
            break;
        case 'L':
            jump_to_prev_hunk();
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
        case 'x':
        case NCKEY_DEL:
            execute_dot_command(*this, DotCommand::X, false);
            break;
        case 'X':
            execute_dot_command(*this, DotCommand::CAP_X, false);
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
        case 'o': {
            buf.push_undo(win.cursors);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            IndentInfo info = get_indent_info_for_lang(lang);

            for (auto& c : win.cursors) {
                std::string indent = "";
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    const std::string& cur = buf.lines[c.y];
                    size_t p = 0;
                    while (p < cur.size() && (cur[p] == ' ' || cur[p] == '\t')) p++;
                    indent = cur.substr(0, p);
                    std::string trimmed = cur.substr(p);
                    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
                    if (!trimmed.empty()) {
                        char b = trimmed.back();
                        if (b == '{' || b == '(' || b == '[' || (lang == "python" && b == ':')) {
                            indent += info.unit;
                        }
                    }
                }
                buf.lines.insert(buf.lines.begin() + c.y + 1, indent);
                c.y++;
                c.x = static_cast<int>(indent.size());
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, Mode::INSERT);
            update_window_scroll(win, buf);
            mode = Mode::INSERT;
            break;
        }
        case 'O': {
            buf.push_undo(win.cursors);
            for (auto& c : win.cursors) {
                std::string indent = "";
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    const std::string& cur = buf.lines[c.y];
                    size_t p = 0;
                    while (p < cur.size() && (cur[p] == ' ' || cur[p] == '\t')) p++;
                    indent = cur.substr(0, p);
                }
                buf.lines.insert(buf.lines.begin() + c.y, indent);
                c.x = static_cast<int>(indent.size());
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            win.clamp_all_cursors(buf, Mode::INSERT);
            update_window_scroll(win, buf);
            mode = Mode::INSERT;
            break;
        }
        case '=':
            s_equal_pending = true;
            set_info_msg("=");
            break;
        case '>':
            s_greater_pending = true;
            set_info_msg(">");
            break;
        case '<':
            s_less_pending = true;
            set_info_msg("<");
            break;
    }
}

void VimEngine::handle_visual_mode(const ncinput& ni, uint32_t key) {
    AutocompleteState::instance().reset();
    auto& win = active_win();
    auto& buf = active_buf();

    if (!s_visual_g_pending) {
        if (keymap.handle_key(*this, mode, ni, key)) {
            return;
        }
    }

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
        cmd_cursor_pos = 0;
        cmd_history_idx = -1;
        cmd_history_draft.clear();
        return;
    }

    if (handle_navigation(ni, key)) return;

    switch (key) {
        case 'h':
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
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
        case 'x':
        case NCKEY_DEL: {
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
        case '=': {
            buf.push_undo(win.cursors);
            Cursor primary = win.cursors.front();
            int min_y = std::min(win.visual_anchor.y, primary.y);
            int max_y = std::max(win.visual_anchor.y, primary.y);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);

            for (int y = min_y; y <= max_y && y < static_cast<int>(buf.lines.size()); ++y) {
                std::string ind = compute_line_indent(buf.lines, y, lang);
                size_t p = 0;
                while (p < buf.lines[y].size() && (buf.lines[y][p] == ' ' || buf.lines[y][p] == '\t')) p++;
                buf.lines[y] = ind + buf.lines[y].substr(p);
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Reindented selection (=)");
            break;
        }
        case '>': {
            buf.push_undo(win.cursors);
            Cursor primary = win.cursors.front();
            int min_y = std::min(win.visual_anchor.y, primary.y);
            int max_y = std::max(win.visual_anchor.y, primary.y);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            IndentInfo info = get_indent_info_for_lang(lang);

            for (int y = min_y; y <= max_y && y < static_cast<int>(buf.lines.size()); ++y) {
                buf.lines[y] = info.unit + buf.lines[y];
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Indented selection (>)");
            break;
        }
        case '<': {
            buf.push_undo(win.cursors);
            Cursor primary = win.cursors.front();
            int min_y = std::min(win.visual_anchor.y, primary.y);
            int max_y = std::max(win.visual_anchor.y, primary.y);
            std::string lang = buf.syntax ? buf.syntax->get_language() : "";
            if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
            IndentInfo info = get_indent_info_for_lang(lang);

            for (int y = min_y; y <= max_y && y < static_cast<int>(buf.lines.size()); ++y) {
                std::string& l = buf.lines[y];
                if (l.rfind(info.unit, 0) == 0) {
                    l.erase(0, info.unit.size());
                } else if (!l.empty() && l[0] == '\t') {
                    l.erase(0, 1);
                } else {
                    size_t sp = 0;
                    while (sp < l.size() && l[sp] == ' ' && sp < static_cast<size_t>(info.tab_size)) sp++;
                    if (sp > 0) l.erase(0, sp);
                }
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            update_window_scroll(win, buf);
            set_info_msg("Unindented selection (<)");
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
    cmd_cursor_pos = static_cast<int>(cmd_buffer.size());
}

void VimEngine::close_cmd_completion() {
    show_cmd_completion = false;
    cmd_completion_candidates.clear();
    cmd_completion_prefix.clear();
    cmd_completion_base_cmd.clear();
    cmd_completion_selected_idx = 0;
    cmd_completion_scroll_row = 0;
    cmd_cursor_pos = static_cast<int>(cmd_buffer.size());
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

    if (is_backspace(ni, key)) {
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

    if (!ni.alt && !ni.ctrl) {
        std::string ch = Keymap::get_input_text(ni, key);
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
    LOGD("handle_command_mode key=%u id=%u utf8=%02x %02x cmd_buffer='%s' pos=%d",
         key, ni.id, (unsigned)ni.utf8[0], (unsigned)ni.utf8[1], cmd_buffer.c_str(), cmd_cursor_pos);

    if (show_cmd_completion) {
        handle_cmd_completion_input(ni, key);
        return;
    }

    cmd_cursor_pos = std::clamp(cmd_cursor_pos, 0, static_cast<int>(cmd_buffer.size()));

    // Cancel command mode: Esc or Ctrl-C
    if (key == NCKEY_ESC || is_ctrl(ni, key, 'c')) {
        mode = Mode::NORMAL;
        cmd_buffer.clear();
        cmd_cursor_pos = 0;
        cmd_history_idx = -1;
        cmd_history_draft.clear();
        set_info_msg("");
        return;
    }

    // Trigger completion
    if (key == '\t' || key == NCKEY_TAB) {
        trigger_cmd_completion();
        return;
    }

    // Execute command on Enter
    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        close_cmd_completion();
        std::string to_exec = cmd_buffer;
        if (!to_exec.empty()) {
            if (cmd_history.empty() || cmd_history.back() != to_exec) {
                cmd_history.push_back(to_exec);
                save_cmd_history();
            }
        }
        cmd_history_idx = -1;
        cmd_history_draft.clear();
        cmd_buffer.clear();
        cmd_cursor_pos = 0;
        mode = Mode::NORMAL;
        execute_command(to_exec);
        return;
    }

    // History navigation: Up / Ctrl-P (older)
    if (key == NCKEY_UP || is_ctrl(ni, key, 'p')) {
        if (!cmd_history.empty()) {
            if (cmd_history_idx == -1) {
                cmd_history_draft = cmd_buffer;
                cmd_history_idx = static_cast<int>(cmd_history.size()) - 1;
            } else if (cmd_history_idx > 0) {
                cmd_history_idx--;
            }
            cmd_buffer = cmd_history[cmd_history_idx];
            cmd_cursor_pos = static_cast<int>(cmd_buffer.size());
        }
        return;
    }

    // History navigation: Down / Ctrl-N (newer)
    if (key == NCKEY_DOWN || is_ctrl(ni, key, 'n')) {
        if (cmd_history_idx != -1) {
            if (cmd_history_idx + 1 < static_cast<int>(cmd_history.size())) {
                cmd_history_idx++;
                cmd_buffer = cmd_history[cmd_history_idx];
            } else {
                cmd_history_idx = -1;
                cmd_buffer = cmd_history_draft;
            }
            cmd_cursor_pos = static_cast<int>(cmd_buffer.size());
        }
        return;
    }

    // Start of line: Ctrl-A / Home
    if (key == NCKEY_HOME || is_ctrl(ni, key, 'a')) {
        cmd_cursor_pos = 0;
        return;
    }

    // End of line: Ctrl-E / End
    if (key == NCKEY_END || is_ctrl(ni, key, 'e')) {
        cmd_cursor_pos = static_cast<int>(cmd_buffer.size());
        return;
    }

    // Move left 1 character: Left arrow / Ctrl-B
    if (key == NCKEY_LEFT || is_ctrl(ni, key, 'b')) {
        if (cmd_cursor_pos > 0) cmd_cursor_pos--;
        return;
    }

    // Move right 1 character: Right arrow / Ctrl-F
    if (key == NCKEY_RIGHT || is_ctrl(ni, key, 'f')) {
        if (cmd_cursor_pos < static_cast<int>(cmd_buffer.size())) cmd_cursor_pos++;
        return;
    }

    // Word backward: Alt-B
    if (is_alt(ni, key, 'b')) {
        while (cmd_cursor_pos > 0 && std::isspace(static_cast<unsigned char>(cmd_buffer[cmd_cursor_pos - 1]))) {
            cmd_cursor_pos--;
        }
        while (cmd_cursor_pos > 0 && !std::isspace(static_cast<unsigned char>(cmd_buffer[cmd_cursor_pos - 1]))) {
            cmd_cursor_pos--;
        }
        return;
    }

    // Word forward: Alt-F
    if (is_alt(ni, key, 'f')) {
        int len = static_cast<int>(cmd_buffer.size());
        while (cmd_cursor_pos < len && !std::isspace(static_cast<unsigned char>(cmd_buffer[cmd_cursor_pos]))) {
            cmd_cursor_pos++;
        }
        while (cmd_cursor_pos < len && std::isspace(static_cast<unsigned char>(cmd_buffer[cmd_cursor_pos]))) {
            cmd_cursor_pos++;
        }
        return;
    }

    // Backspace: delete character before cursor (or exit if empty)
    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b' || is_ctrl(ni, key, 'h')) {
        if (cmd_cursor_pos > 0) {
            cmd_buffer.erase(cmd_cursor_pos - 1, 1);
            cmd_cursor_pos--;
        } else if (cmd_buffer.empty()) {
            mode = Mode::NORMAL;
            set_info_msg("");
        }
        return;
    }

    // Delete character under cursor: Delete / Ctrl-D
    if (key == NCKEY_DEL || is_ctrl(ni, key, 'd')) {
        if (cmd_cursor_pos < static_cast<int>(cmd_buffer.size())) {
            cmd_buffer.erase(cmd_cursor_pos, 1);
        } else if (cmd_buffer.empty() && is_ctrl(ni, key, 'd')) {
            mode = Mode::NORMAL;
            set_info_msg("");
        }
        return;
    }

    // Kill to start of line: Ctrl-U
    if (is_ctrl(ni, key, 'u')) {
        if (cmd_cursor_pos > 0) {
            cmd_buffer.erase(0, cmd_cursor_pos);
            cmd_cursor_pos = 0;
        }
        return;
    }

    // Kill to end of line: Ctrl-K
    if (is_ctrl(ni, key, 'k')) {
        if (cmd_cursor_pos < static_cast<int>(cmd_buffer.size())) {
            cmd_buffer.erase(cmd_cursor_pos);
        }
        return;
    }

    // Kill previous word: Ctrl-W
    if (is_ctrl(ni, key, 'w')) {
        if (cmd_cursor_pos > 0) {
            int p = cmd_cursor_pos;
            while (p > 0 && std::isspace(static_cast<unsigned char>(cmd_buffer[p - 1]))) p--;
            while (p > 0 && !std::isspace(static_cast<unsigned char>(cmd_buffer[p - 1]))) p--;
            cmd_buffer.erase(p, cmd_cursor_pos - p);
            cmd_cursor_pos = p;
        }
        return;
    }

    // Printable character insertion at cursor
    if (!ni.ctrl && !ni.alt) {
        std::string ins = Keymap::get_input_text(ni, key);
        if (!ins.empty()) {
            cmd_buffer.insert(cmd_cursor_pos, ins);
            cmd_cursor_pos += static_cast<int>(ins.size());
            return;
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

    if (keymap.handle_key(*this, mode, ni, key)) {
        return;
    }

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

    if (key == '\t' || key == NCKEY_TAB) {
        ac.reset();
        buf.push_undo(win.cursors);
        std::string lang = buf.syntax ? buf.syntax->get_language() : "";
        if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
        IndentInfo info = get_indent_info_for_lang(lang);

        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                int ins_pos = std::clamp(c.x, 0, static_cast<int>(line.size()));
                line.insert(ins_pos, info.unit);
                c.x += static_cast<int>(info.unit.size());
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.deduplicate_cursors();
        update_window_scroll(win, buf);
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        ac.reset();
        if (info_msg.rfind("Autocomplete", 0) == 0) set_info_msg("");
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());

        std::string lang = buf.syntax ? buf.syntax->get_language() : "";
        if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
        IndentInfo indent_info = get_indent_info_for_lang(lang);

        for (int i = static_cast<int>(win.cursors.size()) - 1; i >= 0; --i) {
            int cy = win.cursors[i].y;
            int cx = win.cursors[i].x;
            std::string& cur = buf.lines[cy];

            size_t lead_len = 0;
            while (lead_len < cur.size() && (cur[lead_len] == ' ' || cur[lead_len] == '\t')) lead_len++;
            std::string line_indent = cur.substr(0, lead_len);

            std::string before = (cx <= static_cast<int>(cur.size())) ? cur.substr(0, cx) : cur;
            std::string after = (cx < static_cast<int>(cur.size())) ? cur.substr(cx) : "";

            std::string trimmed_before = before;
            while (!trimmed_before.empty() && std::isspace(static_cast<unsigned char>(trimmed_before.back()))) trimmed_before.pop_back();
            std::string trimmed_after = after;
            size_t ap = 0;
            while (ap < trimmed_after.size() && std::isspace(static_cast<unsigned char>(trimmed_after[ap]))) ap++;
            trimmed_after = trimmed_after.substr(ap);

            bool pair_split = false;
            if (!trimmed_before.empty() && !trimmed_after.empty()) {
                char b = trimmed_before.back();
                char a = trimmed_after.front();
                if ((b == '{' && a == '}') || (b == '(' && a == ')') || (b == '[' && a == ']')) {
                    pair_split = true;
                }
            }

            bool increase_indent = false;
            if (!trimmed_before.empty()) {
                char b = trimmed_before.back();
                if (b == '{' || b == '(' || b == '[') {
                    increase_indent = true;
                } else if (lang == "python" && b == ':') {
                    increase_indent = true;
                }
            }

            if (pair_split) {
                cur = before;
                std::string mid = line_indent + indent_info.unit;
                std::string end = line_indent + after;
                buf.lines.insert(buf.lines.begin() + cy + 1, mid);
                buf.lines.insert(buf.lines.begin() + cy + 2, end);

                win.cursors[i].y = cy + 1;
                win.cursors[i].x = static_cast<int>(mid.size());
                for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                    win.cursors[j].y += 2;
                }
            } else {
                std::string next_indent = line_indent;
                if (increase_indent) {
                    next_indent += indent_info.unit;
                }
                if (!increase_indent && !trimmed_after.empty() &&
                    (trimmed_after[0] == '}' || trimmed_after[0] == ')' || trimmed_after[0] == ']')) {
                    if (next_indent.size() >= indent_info.unit.size()) {
                        next_indent.erase(next_indent.size() - indent_info.unit.size());
                    }
                }

                cur = before;
                std::string new_line = next_indent + after;
                buf.lines.insert(buf.lines.begin() + cy + 1, new_line);

                win.cursors[i].y = cy + 1;
                win.cursors[i].x = static_cast<int>(next_indent.size());
                for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                    win.cursors[j].y++;
                }
            }
        }
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.deduplicate_cursors();
        update_window_scroll(win, buf);
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        std::sort(win.cursors.begin(), win.cursors.end());
        std::string lang = buf.syntax ? buf.syntax->get_language() : "";
        if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
        IndentInfo info = get_indent_info_for_lang(lang);

        for (auto& c : win.cursors) {
            if (c.x > 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                int del_len = 1;
                if (!info.use_tabs && c.x >= info.tab_size) {
                    bool all_spaces_before = true;
                    for (int k = 0; k < c.x; ++k) {
                        if (line[k] != ' ') {
                            all_spaces_before = false;
                            break;
                        }
                    }
                    if (all_spaces_before && (c.x % info.tab_size == 0)) {
                        del_len = info.tab_size;
                    }
                }
                if (del_len == 1) {
                    int prev_p = Keymap::utf8_prev_char(line, c.x);
                    del_len = std::max(1, c.x - prev_p);
                }
                line.erase(c.x - del_len, del_len);
                c.x -= del_len;
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
        std::string ins = Keymap::get_input_text(ni, key);
        if (ins.empty()) return;

        std::sort(win.cursors.begin(), win.cursors.end());

        if (ins == "}" || ins == ")" || ins == "]") {
            for (auto& c : win.cursors) {
                if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                    std::string& line = buf.lines[c.y];
                    bool only_spaces = true;
                    for (int k = 0; k < c.x && k < static_cast<int>(line.size()); ++k) {
                        if (line[k] != ' ' && line[k] != '\t') {
                            only_spaces = false;
                            break;
                        }
                    }
                    std::string lang = buf.syntax ? buf.syntax->get_language() : "";
                    if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
                    IndentInfo info = get_indent_info_for_lang(lang);
                    if (only_spaces && c.x >= static_cast<int>(info.unit.size())) {
                        if (line.compare(c.x - info.unit.size(), info.unit.size(), info.unit) == 0) {
                            line.erase(c.x - info.unit.size(), info.unit.size());
                            c.x -= static_cast<int>(info.unit.size());
                        }
                    }
                }
            }
        }

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

void VimEngine::open_workspace_list() {
    show_filepicker = false;
    show_settings_popup = false;
    show_git_hunk_popup = false;
    show_whichkey_popup = false;
    show_buffer_list = false;
    show_rg_popup = false;
    show_hunk_diff = false;
    show_git_status = false;
    close_cmd_completion();
    leader_pending = false;

    workspace_cursor = (workspace_slot >= 0 && workspace_slot < 5) ? workspace_slot : 0;
    workspace_editing = false;
    workspace_edit_draft.clear();
    workspace_status_msg.clear();
    show_workspace_list = true;
    set_info_msg("Workspaces (Alt-0 / :ws): [Enter] Load  [s] Save  [c] Clear  [d] Delete  [e] Name");
}

void VimEngine::close_workspace_list() {
    show_workspace_list = false;
    workspace_editing = false;
    workspace_edit_draft.clear();
    workspace_status_msg.clear();
    set_info_msg("");
}

bool VimEngine::load_workspace(int slot, bool show_msg) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());
    WorkspaceSnapshot snap;
    if (!storage.load_snapshot(slot, snap) || snap.buffers.empty()) {
        if (show_msg) {
            set_info_msg("Workspace " + std::to_string(slot) + " is empty");
        }
        return false;
    }

    show_filepicker = false;
    show_settings_popup = false;
    show_git_hunk_popup = false;
    show_whichkey_popup = false;
    show_buffer_list = false;
    show_rg_popup = false;
    show_hunk_diff = false;
    show_git_status = false;
    show_mini_help = false;
    show_cmd_completion = false;
    show_workspace_list = false;
    workspace_editing = false;
    mode = Mode::NORMAL;

    buffers.clear();
    for (const auto& path : snap.buffers) {
        buffers.push_back(TextBuffer::from_file(path));
    }
    if (buffers.empty()) {
        buffers.push_back(std::make_shared<TextBuffer>("untitled", std::vector<std::string>{""}));
    }

    windows.clear();
    for (size_t wi = 0; wi < snap.windows.size(); ++wi) {
        const auto& wsnap = snap.windows[wi];
        size_t b_idx = 0;
        for (size_t bi = 0; bi < buffers.size(); ++bi) {
            if (buffers[bi]->file_path == wsnap.file || buffers[bi]->name == wsnap.file) {
                b_idx = bi;
                break;
            }
        }
        Window w;
        w.id = next_win_id++;
        w.buffer_idx = b_idx;
        w.scroll_y = wsnap.scroll_y;
        w.scroll_x = wsnap.scroll_x;
        int max_y = std::max(0, static_cast<int>(buffers[b_idx]->lines.size()) - 1);
        int cy = std::clamp(wsnap.scroll_y, 0, max_y);
        w.cursors = {{cy, 0}};
        w.clamp_all_cursors(*buffers[b_idx], mode);
        windows.push_back(w);
    }

    if (windows.empty()) {
        Window w;
        w.id = next_win_id++;
        w.buffer_idx = 0;
        windows.push_back(w);
    }

    active_win_idx = std::clamp(snap.active_idx, 0, static_cast<int>(windows.size()) - 1);
    split_mode = (windows.size() > 1) ? SplitType::VERTICAL : SplitType::NONE;
    layout_windows();

    workspace_slot = slot;
    workspace_active = true;
    storage.set_last_active(slot);

    if (show_msg) {
        set_info_msg("Loaded workspace " + std::to_string(slot) +
                     (snap.description.empty() ? "" : (" (" + snap.description + ")")));
    }
    return true;
}

bool VimEngine::save_workspace(int slot, bool show_msg) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());

    WorkspaceSnapshot snap;
    snap.description = storage.get_description(slot);
    snap.active_idx = static_cast<int>(active_win_idx);

    for (const auto& b : buffers) {
        std::string p = !b->file_path.empty() ? b->file_path : b->name;
        if (!p.empty()) {
            snap.buffers.push_back(p);
        }
    }

    for (const auto& w : windows) {
        if (w.buffer_idx < buffers.size()) {
            const auto& b = buffers[w.buffer_idx];
            WorkspaceWindowSnapshot wsnap;
            wsnap.file = !b->file_path.empty() ? b->file_path : b->name;
            wsnap.scroll_y = w.scroll_y;
            wsnap.scroll_x = w.scroll_x;
            snap.windows.push_back(wsnap);
        }
    }

    bool ok = storage.save_snapshot(slot, snap);
    if (ok) {
        workspace_slot = slot;
        workspace_active = true;
        storage.set_last_active(slot);
        if (show_msg) {
            set_info_msg("Saved workspace " + std::to_string(slot));
        }
    }
    return ok;
}

bool VimEngine::delete_workspace(int slot) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());
    bool ok = storage.delete_snapshot(slot);
    if (workspace_slot == slot) {
        workspace_slot = -1;
        workspace_active = false;
        storage.set_last_active(-1);
    }
    return ok;
}

bool VimEngine::clear_active_workspace() {
    WorkspaceStorage storage(config.get_config_dir());
    workspace_slot = -1;
    workspace_active = false;
    storage.set_last_active(-1);
    set_info_msg("Cleared active workspace");
    return true;
}

bool VimEngine::rename_workspace(int slot, const std::string& desc) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());
    return storage.set_description(slot, desc);
}

std::string VimEngine::get_workspace_description(int slot) {
    if (slot < 0 || slot >= 5) return "";
    WorkspaceStorage storage(config.get_config_dir());
    return storage.get_description(slot);
}

void VimEngine::handle_workspace_list_input(const ncinput& ni, uint32_t key) {
    if (workspace_editing) {
        if (is_esc(ni, key)) {
            workspace_editing = false;
            workspace_edit_draft.clear();
            workspace_status_msg = "Rename cancelled";
            return;
        }
        if (is_enter(ni, key)) {
            rename_workspace(workspace_cursor, workspace_edit_draft);
            workspace_editing = false;
            workspace_status_msg = "Renamed workspace " + std::to_string(workspace_cursor);
            workspace_edit_draft.clear();
            return;
        }
        if (is_backspace(ni, key)) {
            if (!workspace_edit_draft.empty()) {
                workspace_edit_draft.pop_back();
            }
            return;
        }
        std::string ins = Keymap::get_input_text(ni, key);
        if (!ins.empty()) {
            workspace_edit_draft += ins;
            return;
        }
        return;
    }

    if (is_esc(ni, key) || key == 'q' || key == 'Q') {
        close_workspace_list();
        return;
    }

    if (key == 'j' || key == NCKEY_DOWN || is_ctrl(ni, key, 'n')) {
        workspace_cursor = (workspace_cursor + 1) % 5;
        workspace_status_msg.clear();
        return;
    }

    if (key == 'k' || key == NCKEY_UP || is_ctrl(ni, key, 'p')) {
        workspace_cursor = (workspace_cursor + 4) % 5;
        workspace_status_msg.clear();
        return;
    }

    if (key == 'g' || key == NCKEY_HOME) {
        workspace_cursor = 0;
        workspace_status_msg.clear();
        return;
    }

    if (key == 'G' || key == NCKEY_END) {
        workspace_cursor = 4;
        workspace_status_msg.clear();
        return;
    }

    if (is_enter(ni, key)) {
        WorkspaceStorage storage(config.get_config_dir());
        WorkspaceSnapshot snap;
        if (!storage.load_snapshot(workspace_cursor, snap) || snap.buffers.empty()) {
            workspace_status_msg = "Workspace " + std::to_string(workspace_cursor) + " is empty";
            return;
        }
        load_workspace(workspace_cursor);
        return;
    }

    if (key == 's' || key == 'S') {
        save_workspace(workspace_cursor);
        workspace_status_msg = "Saved workspace " + std::to_string(workspace_cursor);
        return;
    }

    if (key == 'c' || key == 'C') {
        if (workspace_cursor != workspace_slot) {
            workspace_status_msg = "Cursor must be on active workspace (" +
                                  (workspace_slot >= 0 ? std::to_string(workspace_slot) : "none") + ") to clear";
            return;
        }
        clear_active_workspace();
        workspace_status_msg = "Cleared active workspace";
        return;
    }

    if (key == 'd' || key == 'D') {
        delete_workspace(workspace_cursor);
        workspace_status_msg = "Deleted workspace " + std::to_string(workspace_cursor);
        return;
    }

    if (key == 'e' || key == 'E') {
        workspace_editing = true;
        workspace_edit_draft = get_workspace_description(workspace_cursor);
        workspace_status_msg.clear();
        return;
    }
}