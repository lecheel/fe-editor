#include "engine.hpp"
#include "log.hpp"
#include <clocale>
#include <cmath>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

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

    project_dir = detect_project_dir(!files.empty() ? files[0] : "");
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

    if (!files.empty()) {
        for (const auto& path : files) {
            buffers.push_back(TextBuffer::from_file(path));
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
            "//   [Space] s / v   Split horizontal / vertical",
            "//   [Space] b / B   Cycle next / previous buffer",
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
            "//   h / j / k / l   Move left, down, up, right (also Arrows, Home, End, PgUp, PgDown)",
            "//   0 / $           Jump to line start / end",
            "//   i / a / o       Insert before cursor, after cursor, or open new line below",
            "//   u / U           Undo / Redo (persisted across buffers)",
            "//   [Alt-u]         Undo while inside Insert Mode",
            "//   [Alt-d]         Delete line while inside Insert Mode",
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
            "//   [Alt-s] / [:sp] Horizontal split window",
            "//   [Alt-v] / [:vsp]Vertical split window",
            "//   [Tab] / [Alt-w] Cycle window focus",
            "//   [Alt-x]         Close active window",
            "",
            "// --- Commands & Project --------------------------------------------------",
            "//   [:]             Enter command mode",
            "//   :w [file]       Save file (persists cursor position to config.json)",
            "//   :e <file>       Open or create file",
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
    windows.push_back(w);

    load_rg_cache();

    set_info_msg("[F12] Help | [F2/F3] Hunks | [F4] Diff | [F9] Settings | [F11] Ripgrep | [Space] Leader");
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

VimEngine::~VimEngine() {
    save_all_positions();
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
        int h_per_win = edit_h / n;
        for (int i = 0; i < n; ++i) {
            windows[i].x = 0;
            windows[i].w = edit_w;
            windows[i].y = i * h_per_win;
            windows[i].h = (i == n - 1) ? (edit_h - windows[i].y) : h_per_win;
        }
    }
}


void VimEngine::run() {
    while (running) {
        layout_windows();
        render();

        ncinput ni;
        uint32_t key = 0;

        if (leader_pending && !show_whichkey_popup) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - leader_start_time).count();
            int delay = config.settings.whichkey_delay_ms > 0 ? config.settings.whichkey_delay_ms : 300;

            if (elapsed >= delay) {
                show_whichkey_popup = true;
                set_info_msg("WhichKey: [Space] Leader Menu");
                continue;
            } else {
                int remain_ms = delay - static_cast<int>(elapsed);
                struct timespec ts;
                ts.tv_sec = remain_ms / 1000;
                ts.tv_nsec = (remain_ms % 1000) * 1000000L;
                key = notcurses_get(nc, &ts, &ni);
                if (key == 0) {
                    show_whichkey_popup = true;
                    set_info_msg("WhichKey: [Space] Leader Menu");
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

        if (key == NCKEY_F12 || ni.id == NCKEY_F12) {
            show_mini_help = !show_mini_help;
            continue;
        }

        if (show_mini_help && (key == NCKEY_ESC || key == 'q' || key == 'Q')) {
            show_mini_help = false;
            continue;
        }

        if (key == NCKEY_F02 || ni.id == NCKEY_F02) {
            jump_to_prev_hunk();
            continue;
        }

        if (key == NCKEY_F03 || ni.id == NCKEY_F03) {
            jump_to_next_hunk();
            continue;
        }

        if (key == NCKEY_F04 || ni.id == NCKEY_F04) {
            if (show_git_hunk_popup) {
                show_git_hunk_popup = false;
            } else {
                show_whichkey_popup = false;
                open_git_hunk_popup();
            }
            continue;
        }

        if (key == NCKEY_F09 || ni.id == NCKEY_F09) {
            show_settings_popup = !show_settings_popup;
            if (show_settings_popup) {
                show_git_hunk_popup = false;
                show_whichkey_popup = false;
            }
            continue;
        }

        if (key == NCKEY_F11 || ni.id == NCKEY_F11) {
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
            continue;
        }

        if (show_rg_popup) {
            handle_rg_popup_input(ni, key);
            continue;
        }

        if (show_filepicker) {
            handle_filepicker_input(ni, key);
            continue;
        }

        if (leader_pending) {
            handle_whichkey_popup(ni, key);
            continue;
        }

        if (ni.alt && (ni.id == 'e' || ni.id == 'E')) {
            open_filepicker();
            continue;
        }

        if (key == NCKEY_F09 || ni.id == NCKEY_F09) {
            show_settings_popup = !show_settings_popup;
            if (show_settings_popup) {
                show_git_hunk_popup = false;
                show_whichkey_popup = false;
            }
            continue;
        }

        if (show_settings_popup) {
            handle_settings_popup(ni, key);
            continue;
        }

        if (key == NCKEY_F02 || ni.id == NCKEY_F02) {
            jump_to_prev_hunk();
            continue;
        }

        if (key == NCKEY_F03 || ni.id == NCKEY_F03) {
            jump_to_next_hunk();
            continue;
        }

        if (key == NCKEY_F04 || ni.id == NCKEY_F04) {
            if (show_git_hunk_popup) {
                show_git_hunk_popup = false;
            } else {
                show_whichkey_popup = false;
                open_git_hunk_popup();
            }
            continue;
        }

        if (show_git_hunk_popup) {
            handle_git_hunk_popup(ni, key);
            continue;
        }

        if (ni.alt && (ni.id == 'q' || ni.id == 'Q')) {
            break;
        }

        if (mode != Mode::COMMAND) {
            if (ni.alt && (ni.id == 's' || ni.id == 'S')) {
                split_window(SplitType::HORIZONTAL);
                continue;
            }
            if (ni.alt && (ni.id == 'v' || ni.id == 'V')) {
                split_window(SplitType::VERTICAL);
                continue;
            }
            if (ni.alt && (ni.id == 'x' || ni.id == 'X')) {
                close_active_window();
                continue;
            }
            if (key == '\t' || (ni.alt && (ni.id == 'w' || ni.id == 'W'))) {
                active_win_idx = (active_win_idx + 1) % windows.size();
                set_info_msg("Focused Window #" + std::to_string(windows[active_win_idx].id));
                continue;
            }
        }

        if (mode == Mode::NORMAL) {
            handle_normal_mode(ni, key);
        } else if (mode == Mode::INSERT) {
            handle_insert_mode(ni, key);
        } else if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) {
            handle_visual_mode(ni, key);
        } else if (mode == Mode::COMMAND) {
            handle_command_mode(ni, key);
        }
    }
}
