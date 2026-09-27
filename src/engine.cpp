#include "engine.hpp"
#include "log.hpp"
#include <clocale>
#include <cmath>
#include <cctype>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

struct FileLocationTarget {
    std::string path;
    int line = -1; // 1-based, -1 if unspecified
    int col = -1;  // 1-based, -1 if unspecified
};

FileLocationTarget parse_file_spec(const std::string& arg) {
    FileLocationTarget target;
    target.path = arg;
    target.line = -1;
    target.col = -1;

    if (arg.empty()) return target;

    auto is_number = [](const std::string& s) {
        if (s.empty()) return false;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    // Check for colon separation: path:line:col or path:line
    size_t last_colon = arg.rfind(':');
    if (last_colon != std::string::npos && last_colon > 0) {
        std::string part1 = arg.substr(last_colon + 1);

        if (is_number(part1)) {
            size_t prev_colon = arg.rfind(':', last_colon - 1);
            if (prev_colon != std::string::npos && prev_colon > 0) {
                std::string part2 = arg.substr(prev_colon + 1, last_colon - prev_colon - 1);
                if (is_number(part2)) {
                    std::string potential_path = arg.substr(0, prev_colon);
                    std::error_code ec;
                    if (!fs::exists(arg, ec) || fs::exists(potential_path, ec)) {
                        target.path = potential_path;
                        try { target.line = std::stoi(part2); } catch (...) {}
                        try { target.col = std::stoi(part1); } catch (...) {}
                        return target;
                    }
                }
            }

            std::string potential_path = arg.substr(0, last_colon);
            std::error_code ec;
            if (!fs::exists(arg, ec) || fs::exists(potential_path, ec)) {
                target.path = potential_path;
                try { target.line = std::stoi(part1); } catch (...) {}
                target.col = -1;
                return target;
            }
        }
    }

    return target;
}

std::vector<FileLocationTarget> parse_file_location_args(const std::vector<std::string>& raw_args) {
    std::vector<FileLocationTarget> targets;
    int pending_line = -1;
    int pending_col = -1;

    for (const auto& arg : raw_args) {
        if (arg.empty()) continue;

        // Check for +<line> or +<line>:<col> or +<line>,<col>
        if (arg[0] == '+' && arg.size() > 1 && std::isdigit(static_cast<unsigned char>(arg[1]))) {
            int pline = -1, pcol = -1;
            size_t sep = arg.find_first_of(":,", 1);
            if (sep != std::string::npos) {
                std::string lstr = arg.substr(1, sep - 1);
                std::string cstr = arg.substr(sep + 1);
                try { pline = std::stoi(lstr); } catch (...) {}
                try { pcol = std::stoi(cstr); } catch (...) {}
            } else {
                try { pline = std::stoi(arg.substr(1)); } catch (...) {}
            }

            pending_line = pline;
            pending_col = pcol;
            continue;
        }

        FileLocationTarget t = parse_file_spec(arg);
        if (pending_line > 0) {
            if (t.line <= 0) {
                t.line = pending_line;
                if (pending_col > 0 && t.col <= 0) {
                    t.col = pending_col;
                }
            }
            pending_line = -1;
            pending_col = -1;
        }
        targets.push_back(t);
    }

    // Trailing +<line> applied to last file (e.g. file1 +50)
    if (pending_line > 0 && !targets.empty()) {
        if (targets.back().line <= 0) {
            targets.back().line = pending_line;
            if (pending_col > 0 && targets.back().col <= 0) {
                targets.back().col = pending_col;
            }
        }
    }

    return targets;
}

} // namespace

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
#ifndef NCKEY_F06
#define NCKEY_F06 (NCKEY_F01 + 5)
#endif
#ifndef NCKEY_F07
#define NCKEY_F07 (NCKEY_F01 + 6)
#endif
#ifndef NCKEY_F08
#define NCKEY_F08 (NCKEY_F01 + 7)
#endif
#ifndef NCKEY_F10
#define NCKEY_F10 (NCKEY_F01 + 9)
#endif
#ifndef NCKEY_F11
#define NCKEY_F11 (NCKEY_F01 + 10)
#endif
#ifndef NCKEY_F12
#define NCKEY_F12 (NCKEY_F01 + 11)
#endif

std::string VimEngine::detect_project_dir(const std::string& start_path) {
    std::string toplevel = detect_git_repo_root(start_path);
    if (!toplevel.empty()) {
        return toplevel;
    }
    std::error_code ec;
    return fs::current_path(ec).string();
}

VimEngine::VimEngine(bool verbose, const std::vector<std::string>& files) {
    Log::init(verbose, "fe_debug.log");
    LOGD("VimEngine constructing, verbose=%d", verbose);
    setlocale(LC_ALL, "");

    config.load();
    keymap.load(config.get_config_dir());

    bool delta_mode = false;
    std::vector<std::string> filtered_files;
    for (const auto& f : files) {
        if (f == "--delta" || f == "-d" || f == "--diff") {
            delta_mode = true;
        } else if (f.rfind("--delta=", 0) == 0) {
            delta_mode = true;
            std::string r = f.substr(8);
            if (!r.empty()) filtered_files.push_back(r);
        } else if (f.rfind("--diff=", 0) == 0) {
            delta_mode = true;
            std::string r = f.substr(7);
            if (!r.empty()) filtered_files.push_back(r);
        } else {
            filtered_files.push_back(f);
        }
    }

    auto file_targets = parse_file_location_args(filtered_files);

    project_dir = detect_project_dir(!file_targets.empty() ? file_targets[0].path : "");
    try {
        project_name = fs::path(project_dir).filename().string();
    } catch (...) {
        project_name = "fe";
    }
    if (project_name.empty()) project_name = "fe";

    notcurses_options opts = {};
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    nc = notcurses_init(&opts, nullptr);
    if (!nc) {
        throw std::runtime_error("Failed to initialize Notcurses");
    }
    stdplane = notcurses_stdplane(nc);

    if (!file_targets.empty()) {
        for (const auto& target : file_targets) {
            buffers.push_back(TextBuffer::from_file(target.path));
        }
    } else {
        buffers.push_back(std::make_shared<TextBuffer>("buffer-1.cpp", std::vector<std::string>{
            "// ==========================================================================",
            "//  fe editor - Quick Reference & Feature Hints",
            "// ==========================================================================",
            "#include <iostream>",
            "",
            "void demo() {",
            "    int a = 100;",
            "    int b = 200;",
            "    int c = 300;",
            "    std::cout << \"Modular architecture initialized!\" << std::endl;",
            "}",
            "",
            "// --- Leader & WhichKey Menu ----------------------------------------------",
            "//   [Space]         Leader key (opens WhichKey popup in bottom-right after 300ms)",
            "//   [Space] f       File finder / picker (instant without popup if typed fast)",
            "//   [Space] g       Ripgrep word on cursor (or :vg <pattern>)",
            "//   [Space] w       Save buffer",
            "//   [Space] q       Quit (or :q / :q!)",
            "//   [Alt-b]         Buffer list popup (or :ls / :buffers)",
            "//   [Alt--] / [:bp] Switch to previous buffer",
            "//   [Alt-=] / [:bn] Switch to next buffer",
            "//   b / B           Cycle next / previous buffer",
            "//   [Space] d       Git hunk diff view (F5)",
            "//   [Space] h       Git hunk diff popup (F4)",
            "//   [Space] j / k   Jump next / prev git hunk (F3 / F2)",
            "//   [Space] l       Line number & gutter settings popup (F9)",
            "//   [Space] u       Undo",
            "",
            "// --- File Picker & Search ------------------------------------------------",
            "//   [Alt-e]         Fuzzy file finder across project repository",
            "//   [F11] / [:vg]   Ripgrep search results popup (reopen / toggle)",
            "//   [:vg <pattern>] Ripgrep grouped search in full-screen popup",
            "//   [:vg]           Reopen last ripgrep search (persisted in rg_search.json)",
            "//   In Search Popup: [j/k/Up/Down] navigate matches, [{/}] file groups, [Enter] open",
            "",
            "// --- Git Hunks & Realtime Memory Gutter ----------------------------------",
            "//   fe -d f1 f2     Compare two files side-by-side (--delta)",
            "//   Gutter markers: [+] added, [~] modified, [-] deleted (realtime in-memory diff)",
            "//   [F2] / [F3]     Jump to previous / next git hunk",
            "//   [F4]            Open git hunk diff popup",
            "//   In Hunk Popup:  [r] Revert current hunk (instant, undoable with 'u')",
            "",
            "// --- Settings & Gutter (F9) -----------------------------------------------",
            "//   [F9]            Toggle rounded settings popup (50% screen width)",
            "//   Options:        Toggle line numbers, Style (Absolute/Relative/Hybrid), Width, Active highlight, Hunk style (~-= or |)",
            "",
            "// --- Navigation & Editing ------------------------------------------------",
            "//   h / j / k       Move left, down, up (also Arrows, Home, End, PgUp, PgDown)",
            "//   l / L           Jump to next / previous git hunk (like F3 / F2)",
            "//   0 / $           Jump to line start / end",
            "//   gg / G          Jump to top / end of file",
            "//   yy / p / P      Yank line, paste after / paste before cursor",
            "//   dd / dw / d^ / d$ Delete line, word, to line start, to line end",
            "//   d0 / dG         Delete to top of file / end of file",
            "//   .               Repeat last change (dd, dw, d^, d0, d$, dG)",
            "//   i / a / o       Insert before cursor, after cursor, or open new line below",
            "//   u / U           Undo / Redo (persisted across buffers)",
            "//   [Alt-u]         Undo while inside Insert Mode",
            "//   [Alt-d]         Delete line while inside Insert Mode",
            "//   [Alt-/]         Autocomplete (prefix >= 3, auto >= 5, Up/Down cycle, Right accept)",
            "",
            "// --- Visual & Multi-Cursor -----------------------------------------------",
            "//   [v]             Character visual mode",
            "//   [Ctrl-v]        Visual block mode",
            "//   In Block Mode:  [I] / [A] multi-cursor insert at start/end of block",
            "//   In Visual Mode: [d] / [x] delete selection/block, [y] yank",
            "//   [C]             Add multi-cursor on line below",
            "//   [Alt-k]         Add multi-cursor on line above",
            "//   [Esc]           Clear multiple cursors / return to Normal Mode",
            "",
            "// --- Windows & Splits ----------------------------------------------------",
            "//   [Ctrl-w]        Window ops menu with WhichKey popup:",
            "//                   [q] close, [v] split vert, [s] split horiz, [w] next window",
            "//   [Alt-s] / [:sp] Horizontal split window",
            "//   [Alt-v] / [:vsp]Vertical split window",
            "//   [Tab] / [Alt-w] Cycle window focus",
            "//   [Alt-x]         Close active window",
            "",
            "// --- Commands & Project --------------------------------------------------",
            "//   [:]             Enter command mode",
            "//   :w [file]       Save file (persists cursor position to config.json)",
            "//   :e <file>       Open or create file",
            "//   :delta [f1] f2  Compare two files side-by-side (or :diff f1 f2)",
            "//   :b <n> / :bn    Switch buffer by index / next buffer",
            "//   :pwd / :proj    Print project repository root (auto-detected git toplevel)",
            "//   :cd <dir>       Change working directory / project root"
        }));

        buffers.push_back(std::make_shared<TextBuffer>("buffer-2.md", std::vector<std::string>{
            "# Documentation & Notes",
            "",
            "- Modular design: Buffer, Window, Engine, Types separated.",
            "- Visual Mode: 'v' character visual, 'Ctrl-v' block visual.",
            "- In Block Mode: press 'I' to insert across all lines.",
            "- Command Mode: ':w [file]', ':q', ':wq', ':q!'."
        }));
    }

    Window w;
    w.id = 1;
    w.buffer_idx = 0;
    restore_window_position(w, *buffers[0]);

    for (size_t i = 0; i < file_targets.size() && i < buffers.size(); ++i) {
        const auto& target = file_targets[i];
        if (target.line > 0) {
            int ty = std::clamp(target.line - 1, 0, std::max(0, static_cast<int>(buffers[i]->lines.size()) - 1));
            int tx = 0;
            if (target.col > 0 && ty < static_cast<int>(buffers[i]->lines.size())) {
                tx = std::clamp(target.col - 1, 0, static_cast<int>(buffers[i]->lines[ty].size()));
            }
            std::string key = !buffers[i]->file_path.empty() ? buffers[i]->file_path : buffers[i]->name;
            config.set_position(key, ty, tx, 0);

            if (i == 0) {
                w.cursors = {{ty, tx}};
                w.clamp_all_cursors(*buffers[0], mode);
                w.scroll_y = std::max(0, ty - 10);
            }
        }
    }

    windows.push_back(w);

    load_rg_cache();
    load_cmd_history();

    if (delta_mode) {
        if (buffers.size() >= 2) {
            Window w2;
            w2.id = next_win_id++;
            w2.buffer_idx = 1;
            restore_window_position(w2, *buffers[1]);
            windows.push_back(w2);
            split_mode = SplitType::VERTICAL;
            open_delta_diff(buffers[0], buffers[1]);
        } else if (!buffers.empty()) {
            open_hunk_diff();
        }
    } else if (!file_targets.empty() && file_targets[0].line > 0) {
        set_info_msg("\"" + buffers[0]->name + "\" [" +
                     std::to_string(w.cursors.front().y + 1) + ":" +
                     std::to_string(w.cursors.front().x + 1) + "]");
    } else {
        set_info_msg("[F12] Help | [F1] Git View | [F5] Hunk Diff | [F4] Popup | [F2/F3] Hunks | [F9] Settings | [Space] Leader");
    }
}

int VimEngine::get_line_num_w(const TextBuffer& buf) const {
    if (!config.settings.show_line_numbers) {
        return buf.is_git_repo ? 2 : 0;
    }
    if (config.settings.line_number_width > 0) {
        return std::max(4, config.settings.line_number_width);
    }
    int digits = static_cast<int>(std::to_string(std::max(1UL, buf.lines.size())).size());
    return std::max(4, digits + 2);
}

void VimEngine::load_cmd_history() {
    std::string path = (fs::path(config.get_config_dir()) / "cmd_history").string();
    std::ifstream in(path);
    if (!in.is_open()) return;
    cmd_history.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            cmd_history.push_back(line);
        }
    }
    if (cmd_history.size() > 500) {
        cmd_history.erase(cmd_history.begin(), cmd_history.begin() + (cmd_history.size() - 500));
    }
}

void VimEngine::save_cmd_history() {
    std::string path = (fs::path(config.get_config_dir()) / "cmd_history").string();
    std::error_code ec;
    fs::create_directories(config.get_config_dir(), ec);
    std::ofstream out(path);
    if (!out.is_open()) return;
    size_t start = (cmd_history.size() > 500) ? (cmd_history.size() - 500) : 0;
    for (size_t i = start; i < cmd_history.size(); ++i) {
        out << cmd_history[i] << "\n";
    }
}

VimEngine::~VimEngine() {
    save_all_positions();
    save_cmd_history();
    config.save();
    LOGD("VimEngine shutting down");
    if (nc) notcurses_stop(nc);
    Log::shutdown();
}

void VimEngine::save_window_position(const Window& win, const TextBuffer& buf) {
    if (win.cursors.empty()) return;
    Cursor primary = win.cursors.front();
    std::string key = !buf.file_path.empty() ? buf.file_path : buf.name;
    config.set_position(key, primary.y, primary.x, win.scroll_y);
}

void VimEngine::restore_window_position(Window& win, const TextBuffer& buf) {
    std::string key = !buf.file_path.empty() ? buf.file_path : buf.name;
    FilePosition pos;
    if (config.get_position(key, pos)) {
        win.cursors = {{pos.y, pos.x}};
        win.scroll_y = pos.scroll_y;
    } else {
        win.cursors = {{0, 0}};
        win.scroll_y = 0;
    }
    win.clamp_all_cursors(buf, mode);
}

void VimEngine::save_all_positions() {
    for (const auto& win : windows) {
        if (win.buffer_idx < buffers.size()) {
            save_window_position(win, *buffers[win.buffer_idx]);
        }
    }
}

void VimEngine::set_info_msg(std::string msg) {
    info_msg = std::move(msg);
}

Window& VimEngine::active_win() {
    return windows[active_win_idx];
}

TextBuffer& VimEngine::active_buf() {
    return *buffers[active_win().buffer_idx];
}

void VimEngine::split_window(SplitType type) {
    if (windows.size() >= 4) {
        set_info_msg("Max 4 split windows reached.");
        return;
    }
    Window new_w = active_win();
    new_w.id = next_win_id++;
    windows.push_back(new_w);
    split_mode = type;
    active_win_idx = windows.size() - 1;
    set_info_msg("Split window created (#" + std::to_string(new_w.id) + ")");
}

void VimEngine::close_active_window() {
    if (windows.size() <= 1) {
        set_info_msg("Cannot close the last window.");
        return;
    }
    windows.erase(windows.begin() + active_win_idx);
    if (active_win_idx >= windows.size()) {
        active_win_idx = windows.size() - 1;
    }
    if (windows.size() == 1) split_mode = SplitType::NONE;
    set_info_msg("Window closed.");
}

void VimEngine::layout_windows() {
    unsigned int screen_h, screen_w;
    ncplane_dim_yx(stdplane, &screen_h, &screen_w);

    int edit_h = std::max(1, static_cast<int>(screen_h) - 2);
    int edit_w = static_cast<int>(screen_w);
    int n = static_cast<int>(windows.size());

    if (n == 1) {
        windows[0].y = 0;
        windows[0].x = 0;
        windows[0].h = edit_h;
        windows[0].w = edit_w;
        return;
    }

    if (split_mode == SplitType::VERTICAL) {
        int w_per_win = edit_w / n;
        for (int i = 0; i < n; ++i) {
            windows[i].y = 0;
            windows[i].h = edit_h;
            windows[i].x = i * w_per_win;
            windows[i].w = (i == n - 1) ? (edit_w - windows[i].x) : w_per_win;
        }
    } else {
        int num_seps = n - 1;
        int avail_h = std::max(n, edit_h - num_seps);
        int h_per_win = avail_h / n;
        int rem = avail_h % n;
        int cur_y = 0;
        for (int i = 0; i < n; ++i) {
            int win_h = h_per_win + (i < rem ? 1 : 0);
            windows[i].x = 0;
            windows[i].w = edit_w;
            windows[i].y = cur_y;
            windows[i].h = win_h;
            cur_y += win_h;
            if (i < n - 1) {
                cur_y += 1;
            }
        }
    }
}


void VimEngine::run() {
    while (running) {
        layout_windows();
        render();

        ncinput ni;
        uint32_t key = 0;

        if ((leader_pending || ctrl_w_pending) && !show_whichkey_popup) {
            auto now = std::chrono::steady_clock::now();
            auto start_t = ctrl_w_pending ? ctrl_w_start_time : leader_start_time;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_t).count();
            int delay = config.settings.whichkey_delay_ms > 0 ? config.settings.whichkey_delay_ms : 300;

            if (elapsed >= delay || ctrl_w_pending) {
                show_whichkey_popup = true;
                if (ctrl_w_pending) {
                    whichkey_mode = WhichKeyMode::WINDOW;
                    set_info_msg("Window [Ctrl-w]: [q] Close  [v] V-Split  [s] H-Split  [w] Next");
                } else {
                    whichkey_mode = WhichKeyMode::LEADER;
                    set_info_msg("WhichKey: [Space] Leader Menu");
                }
                continue;
            } else {
                int remain_ms = delay - static_cast<int>(elapsed);
                struct timespec ts;
                ts.tv_sec = remain_ms / 1000;
                ts.tv_nsec = (remain_ms % 1000) * 1000000L;
                key = notcurses_get(nc, &ts, &ni);
                if (key == 0) {
                    show_whichkey_popup = true;
                    if (ctrl_w_pending) {
                        whichkey_mode = WhichKeyMode::WINDOW;
                        set_info_msg("Window [Ctrl-w]: [q] Close  [v] V-Split  [s] H-Split  [w] Next");
                    } else {
                        whichkey_mode = WhichKeyMode::LEADER;
                        set_info_msg("WhichKey: [Space] Leader Menu");
                    }
                    continue;
                }
            }
        } else {
            key = notcurses_get(nc, nullptr, &ni);
        }

        if (key == (uint32_t)-1 || key == 0) {
            continue;
        }
        if (ni.evtype == NCTYPE_RELEASE) {
            continue;
        }

        LOGD("run() key=%u id=%u utf8=%02x %02x ctrl=%d alt=%d shift=%d mode=%d",
             key, ni.id, (unsigned)ni.utf8[0], (unsigned)ni.utf8[1], ni.ctrl, ni.alt, ni.shift, (int)mode);

        handle_key_input(ni, key);
    }
}
