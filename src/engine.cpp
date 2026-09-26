#include "engine.hpp"
#include "log.hpp"
#include <clocale>
#include <cmath>
#include <iostream>
#include <sstream>
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

std::string VimEngine::detect_project_dir(const std::string& start_path) {
    std::string dir = start_path;
    std::error_code ec;
    if (dir.empty()) {
        dir = fs::current_path(ec).string();
    } else {
        fs::path p = fs::absolute(start_path, ec);
        if (!ec) {
            dir = fs::is_directory(p, ec) ? p.string() : p.parent_path().string();
        }
    }
    if (dir.empty()) dir = ".";

    std::string cmd = "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        char buf[1024];
        std::string toplevel;
        if (fgets(buf, sizeof(buf), fp)) {
            toplevel = buf;
            while (!toplevel.empty() && (toplevel.back() == '\n' || toplevel.back() == '\r')) {
                toplevel.pop_back();
            }
        }
        int status = pclose(fp);
        if (status == 0 && !toplevel.empty()) {
            return toplevel;
        }
    }

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
            "// --- Buffer 1: Modular Multi-Window, Visual Block & Command Engine ---",
            "#include <iostream>",
            "",
            "void demo() {",
            "    int a = 100;",
            "    int b = 200;",
            "    int c = 300;",
            "    std::cout << \"Modular architecture initialized!\" << std::endl;",
            "}",
            "",
            "// Visual Block: [Ctrl-v] start block, [I]/[A] multi-cursor insert, [d]/[x] delete block",
            "// Commands:     [:] enter command mode (:w, :q, :wq, :q!, :sp, :vsp, :b <n>)",
            "// Navigation:   h/j/k/l, Arrows, PgUp, PgDown, Home, End",
            "// Multi-Cursor: [C] Add below, [Alt-k] Add above, [Esc] Normal/Reset",
            "// Splits:       [Alt-s] Horiz split, [Alt-v] Vert split, [Tab] Cycle win, [Alt-x] Close"
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

    set_info_msg("[F2/F3] Prev/Next Hunk | [F4] Hunk Diff | [F9] Settings | [:] Cmd | [v] Visual");
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
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
            return true;
        case NCKEY_RIGHT:
            for (auto& c : win.cursors) c.x = std::min(win.get_max_x(buf, c.y, mode), c.x + 1);
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
    auto& win = active_win();
    auto& buf = active_buf();

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
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
            break;
        case 'l':
            for (auto& c : win.cursors) c.x = std::min(win.get_max_x(buf, c.y, mode), c.x + 1);
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
            save_window_position(win, active_buf());
            win.buffer_idx = (win.buffer_idx + 1) % buffers.size();
            restore_window_position(win, active_buf());
            set_info_msg("Switched to Buffer [" + active_buf().name + "]");
            break;
        case 'B':
            save_window_position(win, active_buf());
            win.buffer_idx = (win.buffer_idx + buffers.size() - 1) % buffers.size();
            restore_window_position(win, active_buf());
            set_info_msg("Switched to Buffer [" + active_buf().name + "]");
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
        case 'o':
            buf.push_undo(win.cursors);
            for (auto& c : win.cursors) {
                buf.lines.insert(buf.lines.begin() + c.y + 1, "");
                c.y++;
                c.x = 0;
            }
            mode = Mode::INSERT;
            break;
    }
}

void VimEngine::handle_visual_mode(const ncinput& ni, uint32_t key) {
    auto& win = active_win();
    auto& buf = active_buf();

    if (key == NCKEY_ESC) {
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
        set_info_msg("");
        return;
    }

    if (key == ':' || ni.id == ':' || (ni.utf8[0] == ':' && ni.utf8[1] == '\0') ||
        (ni.shift && (key == ';' || ni.id == ';'))) {
        mode = Mode::COMMAND;
        cmd_buffer.clear();
        return;
    }

    if (handle_navigation(ni, key)) return;

    switch (key) {
        case 'h':
            for (auto& c : win.cursors) c.x = std::max(0, c.x - 1);
            break;
        case 'l':
            for (auto& c : win.cursors) c.x = std::min(win.get_max_x(buf, c.y, mode), c.x + 1);
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
        case 'x': {
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
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            set_info_msg("Block deleted.");
            break;
        }
        case 'y':
            mode = Mode::NORMAL;
            set_info_msg("Selection yanked.");
            break;
    }
}

void VimEngine::handle_command_mode(const ncinput& ni, uint32_t key) {
    LOGD("handle_command_mode key=%u id=%u utf8=%02x %02x cmd_buffer='%s'",
         key, ni.id, (unsigned)ni.utf8[0], (unsigned)ni.utf8[1], cmd_buffer.c_str());
    if (key == NCKEY_ESC) {
        mode = Mode::NORMAL;
        cmd_buffer.clear();
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        execute_command(cmd_buffer);
        cmd_buffer.clear();
        mode = Mode::NORMAL;
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        if (!cmd_buffer.empty()) {
            cmd_buffer.pop_back();
        } else {
            mode = Mode::NORMAL;
            set_info_msg("");
        }
        return;
    }

    // Ignore synthesized non-printable keys (like arrows/F-keys)
    if (!nckey_synthesized_p(key)) {
        if (ni.utf8[0] != '\0') {
            cmd_buffer += reinterpret_cast<const char*>(ni.utf8);
        } else if (key >= 32 && key < 127) {
            cmd_buffer += static_cast<char>(key);
        }
    }
}

void VimEngine::execute_command(const std::string& cmd_str) {
    std::istringstream iss(cmd_str);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty()) return;

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
        std::string path;
        iss >> path;
        if (!path.empty()) {
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
            set_info_msg("\"" + active_buf().name + "\" [" + std::to_string(active_buf().lines.size()) + " lines]");
        } else {
            set_info_msg("E471: Argument required");
        }
    } else if (cmd == "sp" || cmd == "split") {
        split_window(SplitType::HORIZONTAL);
    } else if (cmd == "vsp" || cmd == "vsplit") {
        split_window(SplitType::VERTICAL);
    } else if (cmd == "bn" || cmd == "bnext") {
        save_window_position(active_win(), active_buf());
        active_win().buffer_idx = (active_win().buffer_idx + 1) % buffers.size();
        restore_window_position(active_win(), active_buf());
        set_info_msg("Switched to Buffer [" + active_buf().name + "]");
    } else if (cmd == "bp" || cmd == "bprev") {
        save_window_position(active_win(), active_buf());
        active_win().buffer_idx = (active_win().buffer_idx + buffers.size() - 1) % buffers.size();
        restore_window_position(active_win(), active_buf());
        set_info_msg("Switched to Buffer [" + active_buf().name + "]");
    } else if (cmd == "b") {
        size_t idx;
        if (iss >> idx && idx >= 1 && idx <= buffers.size()) {
            save_window_position(active_win(), active_buf());
            active_win().buffer_idx = idx - 1;
            restore_window_position(active_win(), active_buf());
            set_info_msg("Switched to Buffer [" + active_buf().name + "]");
        } else {
            set_info_msg("Invalid buffer index (1-" + std::to_string(buffers.size()) + ")");
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

    if (handle_navigation(ni, key)) return;

    if (key == NCKEY_ESC) {
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        std::sort(win.cursors.begin(), win.cursors.end());
        for (int i = static_cast<int>(win.cursors.size()) - 1; i >= 0; --i) {
            int cy = win.cursors[i].y;
            int cx = win.cursors[i].x;
            std::string& cur = buf.lines[cy];
            std::string rest = (cx < static_cast<int>(cur.size())) ? cur.substr(cx) : "";
            cur = cur.substr(0, cx);
            buf.lines.insert(buf.lines.begin() + cy + 1, rest);
            win.cursors[i].y++;
            win.cursors[i].x = 0;
            for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                win.cursors[j].y++;
            }
        }
        buf.modified = true;
        win.deduplicate_cursors();
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.x > 0 && c.y < static_cast<int>(buf.lines.size())) {
                buf.lines[c.y].erase(c.x - 1, 1);
                c.x--;
            }
        }
        buf.modified = true;
        win.deduplicate_cursors();
        return;
    }

    if (key >= 32 && key != NCKEY_ESC) {
        std::string ins = (ni.utf8[0] != '\0') ? reinterpret_cast<const char*>(ni.utf8) : std::string(1, static_cast<char>(key));
        std::sort(win.cursors.begin(), win.cursors.end());

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
        buf.invalidate_hunks();
        win.deduplicate_cursors();
    }
}

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

void VimEngine::handle_git_hunk_popup(const ncinput& ni, uint32_t key) {
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();

    if (key == NCKEY_ESC || key == NCKEY_F04 || (ni.id == NCKEY_F04) || key == 'q' || key == 'Q') {
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

    if (key == NCKEY_F02 || (ni.id == NCKEY_F02) || key == NCKEY_UP || key == 'k' || key == 'K') {
        active_hunk_idx = (active_hunk_idx + static_cast<int>(hunks.size()) - 1) % hunks.size();
    } else if (key == NCKEY_F03 || (ni.id == NCKEY_F03) || key == NCKEY_DOWN || key == 'j' || key == 'J') {
        active_hunk_idx = (active_hunk_idx + 1) % hunks.size();
    }
}

void VimEngine::render_git_hunk_popup(unsigned int screen_h, unsigned int screen_w) {
    auto& buf = active_buf();
    const auto& hunks = buf.get_hunks();
    if (active_hunk_idx < 0 || active_hunk_idx >= static_cast<int>(hunks.size())) {
        show_git_hunk_popup = false;
        return;
    }

    const auto& hunk = hunks[active_hunk_idx];

    // Compute diff lines to display
    std::vector<std::pair<char, std::string>> diff_entries;
    for (const auto& l : hunk.orig_lines) {
        diff_entries.push_back({'-', l});
    }
    for (const auto& l : hunk.cur_lines) {
        diff_entries.push_back({'+', l});
    }

    int popup_w = std::max(48, static_cast<int>(screen_w * 0.70));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);

    int diff_lines_avail = std::min(static_cast<int>(diff_entries.size()), 12);
    int popup_h = std::max(7, diff_lines_avail + 5);
    popup_h = std::min(popup_h, static_cast<int>(screen_h) - 2);

    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    // Background
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox)
    ncplane_set_fg_rgb8(stdplane, 200, 100, 255);
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
    std::string type_str = (hunk.type == HunkType::ADDED) ? "ADDED" :
                           ((hunk.type == HunkType::MODIFIED) ? "MODIFIED" : "DELETED");
    std::string title = " Git Hunk (" + std::to_string(active_hunk_idx + 1) + "/" +
                        std::to_string(hunks.size()) + ") [" + type_str + "] ";
    if (static_cast<int>(title.size()) < popup_w - 4) {
        ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());
    }

    // Unified diff header @@ -orig,len +cur,len @@
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "@@ -%d,%d +%d,%d @@",
             hunk.orig_start + 1, std::max(1, hunk.orig_count),
             hunk.cur_start + 1, std::max(1, hunk.cur_count));
    ncplane_set_fg_rgb8(stdplane, 80, 200, 240);
    ncplane_set_bg_rgb8(stdplane, 28, 30, 40);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, hdr);

    // Diff lines
    int max_draw_lines = popup_h - 4;
    for (int i = 0; i < max_draw_lines && i < static_cast<int>(diff_entries.size()); ++i) {
        int draw_y = popup_y + 2 + i;
        char sign = diff_entries[i].first;
        const std::string& text = diff_entries[i].second;

        if (sign == '-') {
            ncplane_set_fg_rgb8(stdplane, 255, 110, 110);
            ncplane_set_bg_rgb8(stdplane, 50, 20, 25);
        } else {
            ncplane_set_fg_rgb8(stdplane, 110, 240, 140);
            ncplane_set_bg_rgb8(stdplane, 20, 48, 28);
        }

        std::string line_disp = std::string(1, sign) + " " + text;
        int max_len = popup_w - 4;
        if (static_cast<int>(line_disp.size()) > max_len) {
            line_disp = line_disp.substr(0, max_len);
        } else {
            line_disp += std::string(max_len - line_disp.size(), ' ');
        }
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, line_disp.c_str());
    }

    // Footer actions
    ncplane_set_fg_rgb8(stdplane, 255, 230, 100);
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    std::string footer = " [r] Revert Hunk   [F2/F3] Prev/Next   [Esc/F4/q] Close ";
    if (static_cast<int>(footer.size()) < popup_w - 2) {
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 2, footer.c_str());
    }
}

void VimEngine::update_window_scroll(Window& win, const TextBuffer& buf) {
    if (win.cursors.empty()) return;
    Cursor primary = win.cursors.front();

    if (primary.y < win.scroll_y) {
        win.scroll_y = primary.y;
    }
    if (primary.y >= win.scroll_y + win.h) {
        win.scroll_y = primary.y - win.h + 1;
    }
}

void VimEngine::render() {
    ncplane_erase(stdplane);

    unsigned int screen_h, screen_w;
    ncplane_dim_yx(stdplane, &screen_h, &screen_w);

    for (size_t wi = 0; wi < windows.size(); ++wi) {
        render_window(windows[wi], wi == active_win_idx);
    }

    render_status_bar(screen_h - 2, screen_w);
    render_info_bar(screen_h - 1, screen_w);

    // Enable and position hardware terminal cursor
    if (mode == Mode::COMMAND) {
        int cursor_x = std::min(static_cast<int>(screen_w) - 1, 1 + static_cast<int>(cmd_buffer.size()));
        notcurses_cursor_enable(nc, screen_h - 1, cursor_x);
    } else {
        auto& aw = active_win();
        if (!aw.cursors.empty()) {
            Cursor primary = aw.cursors.front();
            int gutter_w = get_line_num_w(active_buf());
            int screen_cy = aw.y + (primary.y - aw.scroll_y);
            int screen_cx = aw.x + gutter_w + primary.x;
            if (screen_cy >= aw.y && screen_cy < aw.y + aw.h &&
                screen_cx >= aw.x && screen_cx < aw.x + aw.w) {
                notcurses_cursor_enable(nc, screen_cy, screen_cx);
            } else {
                notcurses_cursor_disable(nc);
            }
        }
    }

    if (show_settings_popup) {
        render_settings_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_git_hunk_popup) {
        render_git_hunk_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    }

    notcurses_render(nc);
}

void VimEngine::render_window(Window& win, bool is_active) {
    auto& buf = *buffers[win.buffer_idx];
    update_window_scroll(win, buf);

    std::set<std::pair<int, int>> cursor_set;
    for (const auto& c : win.cursors) {
        cursor_set.insert({c.y, c.x});
    }

    Cursor primary = win.cursors.empty() ? Cursor{0, 0} : win.cursors.front();
    int v_min_y = std::min(win.visual_anchor.y, primary.y);
    int v_max_y = std::max(win.visual_anchor.y, primary.y);
    int v_min_x = std::min(win.visual_anchor.x, primary.x);
    int v_max_x = std::max(win.visual_anchor.x, primary.x);

    int gutter_w = get_line_num_w(buf);

    for (int r = 0; r < win.h; ++r) {
        int line_idx = win.scroll_y + r;
        int draw_y = win.y + r;

        if (gutter_w > 0) {
            bool is_cursor_line = (line_idx == primary.y);
            if (is_cursor_line && config.settings.highlight_current_line && is_active) {
                ncplane_set_bg_rgb8(stdplane, 35, 38, 48);
            } else {
                ncplane_set_bg_rgb8(stdplane, 22, 22, 26);
            }

            // Determine git hunk gutter sign (~, +, -)
            char git_sign = ' ';
            if (buf.is_git_repo && line_idx < static_cast<int>(buf.lines.size())) {
                const auto& hunks = buf.get_hunks();
                for (const auto& h : hunks) {
                    if (h.type == HunkType::ADDED) {
                        if (line_idx >= h.cur_start && line_idx < h.cur_start + h.cur_count) {
                            git_sign = '+';
                            break;
                        }
                    } else if (h.type == HunkType::MODIFIED) {
                        if (line_idx >= h.cur_start && line_idx < h.cur_start + h.cur_count) {
                            git_sign = '~';
                            break;
                        }
                    } else if (h.type == HunkType::DELETED) {
                        int del_pos = std::min(h.cur_start, static_cast<int>(buf.lines.size()) - 1);
                        if (line_idx == del_pos) {
                            git_sign = '-';
                            break;
                        }
                    }
                }
            }

            if (config.settings.show_line_numbers) {
                if (line_idx < static_cast<int>(buf.lines.size())) {
                    if (is_cursor_line && config.settings.highlight_current_line && is_active) {
                        ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
                    } else if (is_active) {
                        ncplane_set_fg_rgb8(stdplane, 80, 160, 200);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    }

                    int disp_num = line_idx + 1;
                    if (config.settings.line_number_mode == LineNumberMode::RELATIVE) {
                        disp_num = std::abs(line_idx - primary.y);
                    } else if (config.settings.line_number_mode == LineNumberMode::HYBRID) {
                        disp_num = is_cursor_line ? (line_idx + 1) : std::abs(line_idx - primary.y);
                    }
                    std::string num_fmt = "%" + std::to_string(gutter_w - 2) + "d";
                    ncplane_printf_yx(stdplane, draw_y, win.x, num_fmt.c_str(), disp_num);

                    // Print git sign column
                    if (git_sign == '+') {
                        ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                    } else if (git_sign == '~') {
                        ncplane_set_fg_rgb8(stdplane, 80, 180, 240);
                    } else if (git_sign == '-') {
                        ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    }
                    char sign_buf[3] = {git_sign, ' ', '\0'};
                    ncplane_putstr_yx(stdplane, draw_y, win.x + gutter_w - 2, sign_buf);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    std::string tilde;
                    for (int sp = 0; sp < std::max(0, gutter_w - 2); ++sp) tilde += " ";
                    tilde += "~ ";
                    ncplane_putstr_yx(stdplane, draw_y, win.x, tilde.c_str());
                }
            } else {
                if (git_sign == '+') {
                    ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
                } else if (git_sign == '~') {
                    ncplane_set_fg_rgb8(stdplane, 80, 180, 240);
                } else if (git_sign == '-') {
                    ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                }
                char sign_buf[3] = {git_sign, ' ', '\0'};
                ncplane_putstr_yx(stdplane, draw_y, win.x, sign_buf);
            }
        }

        int text_avail_w = win.w - gutter_w;
        if (text_avail_w <= 0) continue;

        if (line_idx < static_cast<int>(buf.lines.size())) {
            const std::string& line = buf.lines[line_idx];
            for (int c = 0; c < text_avail_w; ++c) {
                int char_idx = win.scroll_x + c;
                int draw_x = win.x + gutter_w + c;

                bool has_cursor = cursor_set.count({line_idx, char_idx});
                bool in_visual = false;

                if (is_active) {
                    if (mode == Mode::VISUAL_BLOCK) {
                        if (line_idx >= v_min_y && line_idx <= v_max_y &&
                            char_idx >= v_min_x && char_idx <= v_max_x) {
                            in_visual = true;
                        }
                    } else if (mode == Mode::VISUAL) {
                        Cursor cur_pt{line_idx, char_idx};
                        Cursor v_start = std::min(win.visual_anchor, primary);
                        Cursor v_end = std::max(win.visual_anchor, primary);
                        if (!(cur_pt < v_start) && !(v_end < cur_pt)) {
                            in_visual = true;
                        }
                    }
                }

                if (has_cursor) {
                    ncplane_set_fg_rgb8(stdplane, 0, 0, 0);
                    ncplane_set_bg_rgb8(stdplane, 255, 180, 50);
                } else if (in_visual) {
                    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
                    ncplane_set_bg_rgb8(stdplane, 55, 75, 135);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
                    ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                }

                if (char_idx < static_cast<int>(line.size())) {
                    char ch[2] = {line[char_idx], '\0'};
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                } else if ((has_cursor || in_visual) && char_idx == static_cast<int>(line.size())) {
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                } else {
                    ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                }
            }
        } else {
            ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
            std::string empty(text_avail_w, ' ');
            ncplane_putstr_yx(stdplane, draw_y, win.x + gutter_w, empty.c_str());
        }
    }
}

void VimEngine::handle_settings_popup(const ncinput& ni, uint32_t key) {
    if (key == NCKEY_ESC || key == NCKEY_F09 || (ni.id == NCKEY_F09) || key == 'q' || key == 'Q') {
        show_settings_popup = false;
        config.save();
        set_info_msg("Settings applied.");
        return;
    }

    const int total_items = 4;
    if (key == NCKEY_UP || key == 'k' || key == 'K') {
        settings_selected_idx = (settings_selected_idx + total_items - 1) % total_items;
    } else if (key == NCKEY_DOWN || key == 'j' || key == 'J') {
        settings_selected_idx = (settings_selected_idx + 1) % total_items;
    } else if (key == NCKEY_ENTER || key == '\n' || key == '\r' || key == ' ' ||
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
        }
        config.save();
    }
}

void VimEngine::render_settings_popup(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(36, static_cast<int>(screen_w * 0.50));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);
    int popup_h = 10;
    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    // Draw panel background
    ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Draw roundbox border
    ncplane_set_fg_rgb8(stdplane, 90, 180, 240);
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

    // Header Title
    std::string title = " Line Number Hunk Settings (F9) ";
    if (static_cast<int>(title.size()) < popup_w - 4) {
        ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());
    }

    // Setting items
    struct Item {
        std::string label;
        std::string value;
    };

    std::string mode_str = "Absolute";
    if (config.settings.line_number_mode == LineNumberMode::RELATIVE) mode_str = "Relative";
    else if (config.settings.line_number_mode == LineNumberMode::HYBRID) mode_str = "Hybrid";

    std::string w_str = (config.settings.line_number_width == 0) ? "Auto" : std::to_string(config.settings.line_number_width);

    std::vector<Item> items = {
        {"Show Line Numbers", config.settings.show_line_numbers ? "[ ON ]" : "[ OFF ]"},
        {"Line Number Style", "< " + mode_str + " >"},
        {"Hunk/Gutter Width", "< " + w_str + " >"},
        {"Highlight Active", config.settings.highlight_current_line ? "[ ON ]" : "[ OFF ]"}
    };

    for (size_t i = 0; i < items.size(); ++i) {
        int draw_y = popup_y + 2 + static_cast<int>(i);
        bool is_sel = (static_cast<int>(i) == settings_selected_idx);

        if (is_sel) {
            ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            ncplane_set_bg_rgb8(stdplane, 50, 75, 140);
        } else {
            ncplane_set_fg_rgb8(stdplane, 210, 210, 210);
            ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
        }

        std::string pointer = is_sel ? " ▶ " : "   ";
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, pointer.c_str());
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 5, items[i].label.c_str());

        int val_x = popup_x + popup_w - 2 - static_cast<int>(items[i].value.size());
        if (val_x > popup_x + 24) {
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 220, 80);
            } else {
                ncplane_set_fg_rgb8(stdplane, 130, 200, 255);
            }
            ncplane_putstr_yx(stdplane, draw_y, val_x, items[i].value.c_str());
        }
    }

    // Bottom help tip
    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
    std::string help = "[▲/▼] Navigate  [Enter/Space/◀/▶] Toggle  [Esc/F9] Close";
    if (static_cast<int>(help.size()) < popup_w - 4) {
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 2, popup_x + 2, help.c_str());
    }
}

void VimEngine::render_status_bar(int y, unsigned int screen_w) {
    auto& win = active_win();
    auto& buf = active_buf();
    Cursor primary = win.cursors.empty() ? Cursor{0, 0} : win.cursors.front();

    switch (mode) {
        case Mode::NORMAL:
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 80, 210, 120);
            ncplane_putstr_yx(stdplane, y, 0, " NORMAL ");
            break;
        case Mode::INSERT:
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 80, 170, 255);
            ncplane_putstr_yx(stdplane, y, 0, " INSERT ");
            break;
        case Mode::VISUAL:
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 230, 140, 60);
            ncplane_putstr_yx(stdplane, y, 0, " VISUAL ");
            break;
        case Mode::VISUAL_BLOCK:
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 200, 100, 255);
            ncplane_putstr_yx(stdplane, y, 0, " V-BLOCK ");
            break;
        case Mode::COMMAND:
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 240, 200, 80);
            ncplane_putstr_yx(stdplane, y, 0, " COMMAND ");
            break;
    }

    ncplane_set_fg_rgb8(stdplane, 230, 230, 230);
    ncplane_set_bg_rgb8(stdplane, 40, 44, 52);

    std::string display_name = buf.name;
    if (!buf.file_path.empty() && !project_dir.empty()) {
        try {
            std::error_code ec;
            fs::path p = fs::absolute(buf.file_path, ec);
            fs::path root = fs::absolute(project_dir, ec);
            auto rel = fs::relative(p, root, ec);
            if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0) {
                display_name = rel.string();
            }
        } catch (...) {}
    }

    char left_info[256];
    snprintf(left_info, sizeof(left_info), " [%s] [Win %d/%zu] Buf (%zu/%zu): %s %s",
             project_name.c_str(), win.id, windows.size(), win.buffer_idx + 1, buffers.size(),
             display_name.c_str(), (buf.modified ? "[+]" : ""));

    char right_info[256];
    snprintf(right_info, sizeof(right_info), "Cursors: %zu | Ln %d, Col %d | %zu lines ",
             win.cursors.size(), primary.y + 1, primary.x + 1, buf.lines.size());

    int badge_w = (mode == Mode::VISUAL_BLOCK || mode == Mode::COMMAND) ? 9 : 8;
    int bar_w = static_cast<int>(screen_w) - badge_w;
    if (bar_w > 0) {
        std::string filler(bar_w, ' ');
        ncplane_putstr_yx(stdplane, y, badge_w, filler.c_str());
        ncplane_putstr_yx(stdplane, y, badge_w, left_info);

        int right_len = static_cast<int>(std::string(right_info).size());
        int right_x = std::max(badge_w + static_cast<int>(std::string(left_info).size()),
                               static_cast<int>(screen_w) - right_len);
        ncplane_putstr_yx(stdplane, y, right_x, right_info);
    }
}

void VimEngine::render_info_bar(int y, unsigned int screen_w) {
    // Clear line background cleanly without overflowing the terminal corner
    ncplane_set_bg_rgb8(stdplane, 20, 20, 24);
    for (unsigned int x = 0; x < screen_w; ++x) {
        ncplane_putchar_yx(stdplane, y, x, ' ');
    }

    if (mode == Mode::COMMAND) {
        // Distinct bright yellow ':' prompt
        ncplane_set_fg_rgb8(stdplane, 255, 230, 80);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);
        ncplane_putstr_yx(stdplane, y, 0, ":");

        // Command text typed by user
        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        if (!cmd_buffer.empty()) {
            ncplane_putstr_yx(stdplane, y, 1, cmd_buffer.c_str());
        }
    } else {
        ncplane_set_fg_rgb8(stdplane, 240, 200, 100);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);

        if (!info_msg.empty()) {
            std::string bar = " " + info_msg;
            if (static_cast<int>(bar.size()) >= static_cast<int>(screen_w)) {
                bar = bar.substr(0, screen_w - 1);
            }
            ncplane_putstr_yx(stdplane, y, 0, bar.c_str());
        }
    }
}

void VimEngine::run() {
    while (running) {
        layout_windows();
        render();

        ncinput ni;
        uint32_t key = notcurses_get(nc, nullptr, &ni);
        if (key == (uint32_t)-1 || key == 0) {
            continue;
        }
        if (ni.evtype == NCTYPE_RELEASE) {
            continue;
        }

        LOGD("run() key=%u id=%u utf8=%02x %02x ctrl=%d alt=%d shift=%d mode=%d",
             key, ni.id, (unsigned)ni.utf8[0], (unsigned)ni.utf8[1], ni.ctrl, ni.alt, ni.shift, (int)mode);

        if (key == NCKEY_F09 || ni.id == NCKEY_F09) {
            show_settings_popup = !show_settings_popup;
            if (show_settings_popup) show_git_hunk_popup = false;
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