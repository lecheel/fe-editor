#include "engine.hpp"
#include "autocomplete.hpp"
#include "keymap.hpp"
#include "log.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace Keymap;

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
        if (visual_save_mode == Mode::VISUAL || visual_save_mode == Mode::VISUAL_BLOCK) {
            mode = visual_save_mode;
            visual_save_mode = Mode::NORMAL;
            set_info_msg(mode == Mode::VISUAL_BLOCK ? "-- VISUAL BLOCK --" : "-- VISUAL --");
        } else {
            mode = Mode::NORMAL;
            set_info_msg("");
        }
        visual_range_start_y = -1;
        visual_range_end_y = -1;
        cmd_buffer.clear();
        cmd_cursor_pos = 0;
        cmd_history_idx = -1;
        cmd_history_draft.clear();
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
        visual_save_mode = Mode::NORMAL;
        execute_command(to_exec);
        visual_range_start_y = -1;
        visual_range_end_y = -1;
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
            if (visual_save_mode == Mode::VISUAL || visual_save_mode == Mode::VISUAL_BLOCK) {
                mode = visual_save_mode;
                visual_save_mode = Mode::NORMAL;
                set_info_msg(mode == Mode::VISUAL_BLOCK ? "-- VISUAL BLOCK --" : "-- VISUAL --");
            } else {
                mode = Mode::NORMAL;
                set_info_msg("");
            }
            visual_range_start_y = -1;
            visual_range_end_y = -1;
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

    // Ctrl-Shift-V paste into command buffer
    if (Keymap::is_ctrl_shift(ni, key, 'v')) {
        std::string clip = get_system_clipboard();
        if (!clip.empty()) {
            std::string flat;
            for (char c : clip) {
                if (c == '\r' || c == '\n') break;
                flat += c;
            }
            if (!flat.empty()) {
                cmd_buffer.insert(cmd_cursor_pos, flat);
                cmd_cursor_pos += static_cast<int>(flat.size());
            }
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