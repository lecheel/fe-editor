#include "engine.hpp"
#include "autocomplete.hpp"
#include "keymap.hpp"
#include "util/indent.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Keymap;

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

    // --- Delete key (Del / 0x1b[3~) ---
    // Must be checked BEFORE Backspace: on several terminals/notcurses setups
    // the Delete key arrives as the raw DEL byte 0x7f, which would otherwise
    // be swallowed by the Backspace branch below and just delete the previous
    // character instead of joining the next line at end-of-line.
    if (key == NCKEY_DEL || ni.id == NCKEY_DEL) {
        // Top-to-bottom: joining line y with y+1 removes index y+1, which is
        // what later cursors would have been sitting on.
        std::sort(win.cursors.begin(), win.cursors.end(),
                  [](const Cursor& a, const Cursor& b) {
                      if (a.y != b.y) return a.y < b.y;
                      return a.x < b.x;
                  });

        for (size_t i = 0; i < win.cursors.size(); ++i) {
            Cursor& c = win.cursors[i];
            if (c.y < 0 || c.y >= static_cast<int>(buf.lines.size())) continue;
            std::string& line = buf.lines[c.y];
            if (c.x < static_cast<int>(line.size())) {
                int next_p = Keymap::utf8_next_char(line, c.x);
                if (next_p <= c.x) next_p = c.x + 1;
                if (next_p > static_cast<int>(line.size())) next_p = static_cast<int>(line.size());
                line.erase(c.x, next_p - c.x);
            } else if (c.y + 1 < static_cast<int>(buf.lines.size())) {
                // Cursor sits past the last character of the line (or the line
                // is empty): join it with the line below.
                line += buf.lines[c.y + 1];
                buf.lines.erase(buf.lines.begin() + c.y + 1);
                for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                    if (win.cursors[j].y > c.y) win.cursors[j].y -= 1;
                }
            }
        }
        std::sort(win.cursors.begin(), win.cursors.end());
        buf.modified = true;
        buf.version++;
        buf.invalidate_hunks();
        if (buf.syntax) buf.syntax->update_text(buf.lines);
        win.deduplicate_cursors();
        update_autocomplete_after_edit();
        return;
    }

    // --- Backspace key (BS / 0x08 / raw DEL 0x7f) ---
    // Note: 0x7f is ambiguous. We keep it here so a terminal that sends 0x7f
    // for Backspace still behaves as Backspace. If your Delete key also emits
    // 0x7f, remap it in the terminal to emit \x1b[3~ (which notcurses decodes
    // to NCKEY_DEL above).
    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b' || ni.id == NCKEY_BACKSPACE) {
        std::string lang = buf.syntax ? buf.syntax->get_language() : "";
        if (lang.empty()) lang = detect_lang(!buf.file_path.empty() ? buf.file_path : buf.name);
        IndentInfo info = get_indent_info_for_lang(lang);

        // Bottom-to-top so that joining line y-1 with y (which removes y) does
        // not perturb the y of cursors we have not yet handled.
        std::sort(win.cursors.begin(), win.cursors.end(),
                  [](const Cursor& a, const Cursor& b) {
                      if (a.y != b.y) return a.y > b.y;
                      return a.x > b.x;
                  });

        for (auto& c : win.cursors) {
            if (c.y < 0 || c.y >= static_cast<int>(buf.lines.size())) continue;
            if (c.x > 0) {
                std::string& line = buf.lines[c.y];
                if (c.x > static_cast<int>(line.size())) {
                    c.x = static_cast<int>(line.size());
                }
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
            } else if (c.y > 0) {
                // Backspace at column 0: join with the previous line.
                int prev_len = static_cast<int>(buf.lines[c.y - 1].size());
                buf.lines[c.y - 1] += buf.lines[c.y];
                buf.lines.erase(buf.lines.begin() + c.y);
                c.y -= 1;
                c.x = prev_len;
            }
        }
        std::sort(win.cursors.begin(), win.cursors.end());
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