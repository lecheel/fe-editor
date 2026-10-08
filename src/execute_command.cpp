#include "engine.hpp"
#include "command.hpp"
#include "util/path_line_col.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

using ExCommandFn = std::function<void(VimEngine&, std::istringstream&)>;

void VimEngine::execute_command(const std::string& cmd_str) {
    close_cmd_completion();
    std::istringstream iss(cmd_str);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty()) return;

    if (execute_substitute(cmd_str)) {
        return;
    }

            if (CommandRegistry::instance().execute(*this, cmd_str)) {
                return;
            }

            // Goto line: if command is purely numeric, jump to that line number
            bool is_goto_line = !cmd.empty() && std::all_of(cmd.begin(), cmd.end(),
                [](unsigned char c) { return std::isdigit(c); });
            if (is_goto_line) {
                try {
                    int line_num = std::stoi(cmd);
                    if (line_num > 0) {
                        auto& win = active_win();
                        auto& buf = active_buf();
                        int target_y = std::clamp(line_num - 1, 0, std::max(0, static_cast<int>(buf.lines.size()) - 1));
                        for (auto& c : win.cursors) {
                            c.y = target_y;
                            c.x = 0;
                        }
                        win.clamp_all_cursors(buf, mode);
                        update_window_scroll(win, buf);
                    }
                } catch (...) {}
                return;
            }

            // Built-in ex commands: one handler per command, looked up by name/alias.
            // Lambdas are defined inside a member function, so they keep access to
            // VimEngine's private members through `self`.
            static const std::unordered_map<std::string, ExCommandFn> ex_table = [] {
        std::unordered_map<std::string, ExCommandFn> t;
        auto reg = [&t](std::initializer_list<const char*> names, ExCommandFn fn) {
            for (const char* n : names) t[n] = fn;
        };

        reg({"q"}, [](VimEngine& self, std::istringstream&) {
            if (self.active_buf().modified) {
                self.set_info_msg("E37: No write since last change (add ! to override)");
                return;
            }
            self.save_all_positions();
            self.config.save();
            self.running = false;
        });

        reg({"q!"}, [](VimEngine& self, std::istringstream&) {
            self.save_all_positions();
            self.config.save();
            self.running = false;
        });

        reg({"w"}, [](VimEngine& self, std::istringstream& args) {
            std::string path;
            args >> path;
            if (!self.active_buf().save_to_file(path)) {
                self.set_info_msg("E212: Can't open file for writing");
                return;
            }
            self.save_window_position(self.active_win(), self.active_buf());
            self.save_positions();
            self.config.save();
            self.set_info_msg("\"" + self.active_buf().name + "\" written");
        });

        reg({"wq", "x"}, [](VimEngine& self, std::istringstream& args) {
            std::string path;
            args >> path;
            if (!self.active_buf().save_to_file(path)) {
                self.set_info_msg("E212: Can't open file for writing");
                return;
            }
            self.save_all_positions();
            self.config.save();
            self.running = false;
        });

        reg({"e", "edit"}, [](VimEngine& self, std::istringstream& args) {
            std::string raw_arg;
            args >> raw_arg;
            if (raw_arg.empty()) {
                self.set_info_msg("E471: Argument required");
                return;
            }

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
                args >> path;
            }

            if (path.empty()) {
                self.set_info_msg("E471: Argument required");
                return;
            }

            split_path_line_col(path, target_line, target_col);

            auto& win = self.active_win();
            self.save_window_position(win, self.active_buf());

            size_t found_idx = self.buffers.size();
            for (size_t i = 0; i < self.buffers.size(); ++i) {
                if (self.buffers[i]->file_path == path || self.buffers[i]->name == path) {
                    found_idx = i;
                    break;
                }
            }
            if (found_idx == self.buffers.size()) {
                self.buffers.push_back(TextBuffer::from_file(path));
                found_idx = self.buffers.size() - 1;
            }
            win.buffer_idx = found_idx;
            self.restore_window_position(win, self.active_buf());

            if (target_line > 0) {
                auto& buf = self.active_buf();
                int ty = std::clamp(target_line - 1, 0, std::max(0, static_cast<int>(buf.lines.size()) - 1));
                int tx = 0;
                if (target_col > 0 && ty < static_cast<int>(buf.lines.size())) {
                    tx = std::clamp(target_col - 1, 0, static_cast<int>(buf.lines[ty].size()));
                }
                win.cursors = {{ty, tx}};
                win.clamp_all_cursors(buf, self.mode);
                self.update_window_scroll(win, buf);
            }

            self.set_info_msg("\"" + self.active_buf().name + "\" [" +
                              std::to_string(self.active_buf().lines.size()) + " lines]");
        });

        reg({"git", "gitstatus", "gitview", "gs"},
            [](VimEngine& self, std::istringstream&) { self.open_git_status(); });

        reg({"sp", "split"},
            [](VimEngine& self, std::istringstream&) { self.split_window(SplitType::HORIZONTAL); });

        reg({"vsp", "vsplit"},
            [](VimEngine& self, std::istringstream&) { self.split_window(SplitType::VERTICAL); });

        reg({"bn", "bnext"},
            [](VimEngine& self, std::istringstream&) { self.next_buffer(); });

        reg({"bp", "bprev"},
            [](VimEngine& self, std::istringstream&) { self.prev_buffer(); });

        reg({"b"}, [](VimEngine& self, std::istringstream& args) {
            size_t idx;
            if (args >> idx && idx >= 1 && idx <= self.buffers.size()) {
                self.switch_to_buffer(idx - 1);
            } else {
                self.open_buffer_list();
            }
        });

        reg({"ls", "buffers"},
            [](VimEngine& self, std::istringstream&) { self.open_buffer_list(); });

        reg({"vg", "vimgrep", "rg"}, [](VimEngine& self, std::istringstream& args) {
            std::string pattern;
            std::string word;
            while (args >> word) {
                if (!pattern.empty()) pattern += " ";
                pattern += word;
            }
            if (!pattern.empty()) {
                self.run_ripgrep(pattern);
                return;
            }
            // :vg without pattern reuses previous search from rg_search.json
            if (!self.rg_groups.empty()) {
                self.show_rg_popup = true;
                self.set_info_msg("Ripgrep: \"" + self.rg_query + "\" (" +
                                  std::to_string(self.rg_flattened_matches.size()) + " matches)");
                return;
            }
            std::string c_word = self.get_word_under_cursor();
            if (!c_word.empty()) {
                self.run_ripgrep(c_word);
            } else {
                self.show_rg_popup = true;
                self.rg_query_active = true;
                self.rg_query_input.clear();
                self.set_info_msg("Ripgrep: Enter search pattern...");
            }
        });

        reg({"pwd", "proj", "project"}, [](VimEngine& self, std::istringstream&) {
            self.set_info_msg("Project [" + self.project_name + "]: " + self.project_dir);
        });

        reg({"cd"}, [](VimEngine& self, std::istringstream& args) {
            std::string dir;
            args >> dir;
            if (dir.empty()) dir = self.project_dir;
            std::error_code ec;
            fs::current_path(dir, ec);
            if (ec) {
                self.set_info_msg("E344: Can't find directory " + dir);
                return;
            }
            self.project_dir = detect_project_dir(dir);
            try { self.project_name = fs::path(self.project_dir).filename().string(); } catch (...) {}
            if (self.project_name.empty()) self.project_name = "fe";
            self.set_info_msg("Directory changed to: " + dir + " (Project: " + self.project_name + ")");
        });

        return t;
    }();

    auto it = ex_table.find(cmd);
    if (it == ex_table.end()) {
        set_info_msg("E492: Not an editor command: " + cmd);
        return;
    }
    it->second(*this, iss);
}