#include <notcurses/notcurses.h>
#include <clocale>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <map>
#include <set>

enum class Mode {
    NORMAL,
    INSERT
};

enum class SplitType {
    NONE,
    HORIZONTAL,
    VERTICAL
};

struct Cursor {
    int y{0};
    int x{0};

    bool operator<(const Cursor& o) const {
        if (y != o.y) return y < o.y;
        return x < o.x;
    }
    bool operator==(const Cursor& o) const {
        return y == o.y && x == o.x;
    }
};

struct BufferSnapshot {
    std::vector<std::string> lines;
    std::vector<Cursor> cursors;
};

class TextBuffer {
public:
    std::string name;
    std::vector<std::string> lines;
    std::vector<BufferSnapshot> undo_stack;
    std::vector<BufferSnapshot> redo_stack;

    TextBuffer(std::string name, std::vector<std::string> initial_lines)
        : name(std::move(name)), lines(std::move(initial_lines)) {}

    void push_undo(const std::vector<Cursor>& cursors) {
        undo_stack.push_back({lines, cursors});
        redo_stack.clear();
        if (undo_stack.size() > 100) {
            undo_stack.erase(undo_stack.begin());
        }
    }

    bool undo(std::vector<Cursor>& cursors) {
        if (undo_stack.empty()) return false;
        redo_stack.push_back({lines, cursors});
        auto state = undo_stack.back();
        undo_stack.pop_back();
        lines = state.lines;
        cursors = state.cursors;
        return true;
    }

    bool redo(std::vector<Cursor>& cursors) {
        if (redo_stack.empty()) return false;
        undo_stack.push_back({lines, cursors});
        auto state = redo_stack.back();
        redo_stack.pop_back();
        lines = state.lines;
        cursors = state.cursors;
        return true;
    }
};

struct Window {
    int id{0};
    size_t buffer_idx{0};
    int y{0};
    int x{0};
    int h{0};
    int w{0};
    int scroll_y{0};
    int scroll_x{0};
    std::vector<Cursor> cursors{{0, 0}};

    void deduplicate_cursors() {
        std::sort(cursors.begin(), cursors.end());
        cursors.erase(std::unique(cursors.begin(), cursors.end()), cursors.end());
    }

    int get_max_x(const TextBuffer& buf, int line_idx, Mode mode) const {
        if (line_idx < 0 || line_idx >= static_cast<int>(buf.lines.size())) return 0;
        int len = static_cast<int>(buf.lines[line_idx].size());
        if (mode == Mode::NORMAL) {
            return std::max(0, len - 1);
        }
        return len;
    }

    void clamp_all_cursors(const TextBuffer& buf, Mode mode) {
        int max_y = std::max(0, static_cast<int>(buf.lines.size()) - 1);
        for (auto& c : cursors) {
            c.y = std::clamp(c.y, 0, max_y);
            c.x = std::clamp(c.x, 0, get_max_x(buf, c.y, mode));
        }
        deduplicate_cursors();
    }
};

class VimEngine {
public:
    VimEngine() {
        setlocale(LC_ALL, "");

        notcurses_options opts = {};
        opts.flags = NCOPTION_SUPPRESS_BANNERS;
        nc = notcurses_init(&opts, nullptr);
        if (!nc) {
            throw std::runtime_error("Failed to initialize Notcurses");
        }
        stdplane = notcurses_stdplane(nc);

        // Seed initial buffers
        buffers.push_back(std::make_shared<TextBuffer>("buffer-1.cpp", std::vector<std::string>{
            "// --- Buffer 1: Multi-Window & Multi-Buffer Engine ---",
            "#include <iostream>",
            "",
            "void demo() {",
            "    std::cout << \"Multi-cursor & Multi-window ready!\" << std::endl;",
            "}",
            "",
            "// Navigation: h/j/k/l, Arrows, PgUp, PgDown, Home, End",
            "// Multi-Cursor: [J] Add below, [K] Add above, [Esc] Reset to single",
            "// Buffers: [b] Next buffer, [B] Prev buffer",
            "// Splits: [Alt-s] Horiz split, [Alt-v] Vert split, [Tab] Cycle win, [Alt-x] Close win",
            "// Undo/Redo: [u] Undo, [U] Redo",
            "// Quit: [Alt-q]"
        }));

        buffers.push_back(std::make_shared<TextBuffer>("buffer-2.md", std::vector<std::string>{
            "# Documentation & Notes",
            "",
            "- Multi-buffer editing active.",
            "- Independent undo/redo per buffer.",
            "- Synchronized simultaneous edits across all active cursors."
        }));

        // Initial Window
        Window w;
        w.id = 1;
        w.buffer_idx = 0;
        windows.push_back(w);

        set_info_msg("Welcome! [Alt-s] Split H | [Alt-v] Split V | [J/K] Multi-Cursor | [Alt-q] Quit");
    }

    ~VimEngine() {
        if (nc) notcurses_stop(nc);
    }

    void run() {
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

            // Global Quit Command: Alt-q / Alt-Q
            if (ni.alt && (ni.id == 'q' || ni.id == 'Q')) {
                break;
            }
            // Window management shortcuts
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

            if (mode == Mode::NORMAL) {
                handle_normal_mode(ni, key);
            } else if (mode == Mode::INSERT) {
                handle_insert_mode(ni, key);
            }
        }
    }

private:
    struct notcurses* nc{nullptr};
    struct ncplane* stdplane{nullptr};

    Mode mode{Mode::NORMAL};
    bool running{true};
    SplitType split_mode{SplitType::NONE};

    std::vector<std::shared_ptr<TextBuffer>> buffers;
    std::vector<Window> windows;
    size_t active_win_idx{0};

    std::string info_msg;
    int next_win_id{2};

    const int LINE_NUM_W = 4;

    void set_info_msg(std::string msg) {
        info_msg = std::move(msg);
    }

    Window& active_win() {
        return windows[active_win_idx];
    }

    TextBuffer& active_buf() {
        return *buffers[active_win().buffer_idx];
    }

    void split_window(SplitType type) {
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

    void close_active_window() {
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

    void layout_windows() {
        unsigned int screen_h, screen_w;
        ncplane_dim_yx(stdplane, &screen_h, &screen_w);

        int edit_h = std::max(1, static_cast<int>(screen_h) - 2); // 2 rows for Status + Info msg
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

    bool handle_navigation(const ncinput& ni, uint32_t key) {
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

    void handle_normal_mode(const ncinput& ni, uint32_t key) {
        auto& win = active_win();
        auto& buf = active_buf();

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

            // Multi-Cursor Adders
            case 'J': { // Add cursor on line below
                Cursor primary = win.cursors.back();
                if (primary.y + 1 < static_cast<int>(buf.lines.size())) {
                    win.cursors.push_back({primary.y + 1, primary.x});
                    win.clamp_all_cursors(buf, mode);
                    set_info_msg("Multi-Cursor: Added cursor below. Total: " + std::to_string(win.cursors.size()));
                }
                break;
            }
            case 'K': { // Add cursor on line above
                Cursor primary = win.cursors.front();
                if (primary.y - 1 >= 0) {
                    win.cursors.insert(win.cursors.begin(), {primary.y - 1, primary.x});
                    win.clamp_all_cursors(buf, mode);
                    set_info_msg("Multi-Cursor: Added cursor above. Total: " + std::to_string(win.cursors.size()));
                }
                break;
            }
            case NCKEY_ESC: // Collapse back to single cursor
                if (win.cursors.size() > 1) {
                    win.cursors = {win.cursors.front()};
                    set_info_msg("Multi-Cursor: Reset to single primary cursor.");
                }
                break;

            // Multi-Buffer Switching
            case 'b': // Next buffer
                win.buffer_idx = (win.buffer_idx + 1) % buffers.size();
                win.clamp_all_cursors(active_buf(), mode);
                set_info_msg("Switched to Buffer [" + active_buf().name + "]");
                break;
            case 'B': // Prev buffer
                win.buffer_idx = (win.buffer_idx + buffers.size() - 1) % buffers.size();
                win.clamp_all_cursors(active_buf(), mode);
                set_info_msg("Switched to Buffer [" + active_buf().name + "]");
                break;

            // Undo / Redo
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

            // Enter Insert Mode
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

    void handle_insert_mode(const ncinput& ni, uint32_t key) {
        auto& win = active_win();
        auto& buf = active_buf();

        if (handle_navigation(ni, key)) return;

        if (key == NCKEY_ESC) {
            mode = Mode::NORMAL;
            win.clamp_all_cursors(buf, mode);
            return;
        }

        if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
            // Apply enter across all cursors from bottom to top
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
                // Shift subsequent cursors downward
                for (size_t j = i + 1; j < win.cursors.size(); ++j) {
                    win.cursors[j].y++;
                }
            }
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
            win.deduplicate_cursors();
            return;
        }

        // Multi-cursor character typing
        if (key >= 32 && key != NCKEY_ESC) {
            std::string ins = (ni.utf8[0] != '\0') ? reinterpret_cast<const char*>(ni.utf8) : std::string(1, static_cast<char>(key));
            std::sort(win.cursors.begin(), win.cursors.end());

            // Group cursors by line to adjust column offsets cleanly
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
            win.deduplicate_cursors();
        }
    }

    void update_window_scroll(Window& win, const TextBuffer& buf) {
        if (win.cursors.empty()) return;
        Cursor primary = win.cursors.front();

        if (primary.y < win.scroll_y) {
            win.scroll_y = primary.y;
        }
        if (primary.y >= win.scroll_y + win.h) {
            win.scroll_y = primary.y - win.h + 1;
        }
    }

    void render() {
        ncplane_erase(stdplane);

        unsigned int screen_h, screen_w;
        ncplane_dim_yx(stdplane, &screen_h, &screen_w);

        // 1. Render all split windows
        for (size_t wi = 0; wi < windows.size(); ++wi) {
            render_window(windows[wi], wi == active_win_idx);
        }

        // 2. Render Single Global Status Bar (Row: screen_h - 2)
        render_status_bar(screen_h - 2, screen_w);

        // 3. Render Extra Info/Message Bar (Row: screen_h - 1)
        render_info_bar(screen_h - 1, screen_w);

        // 4. Position terminal hardware cursor at primary cursor of active window
        auto& aw = active_win();
        if (!aw.cursors.empty()) {
            Cursor primary = aw.cursors.front();
            int screen_cy = aw.y + (primary.y - aw.scroll_y);
            int screen_cx = aw.x + LINE_NUM_W + primary.x;
            if (screen_cy >= aw.y && screen_cy < aw.y + aw.h &&
                screen_cx >= aw.x && screen_cx < aw.x + aw.w) {
                ncplane_cursor_move_yx(stdplane, screen_cy, screen_cx);
            }
        }

        notcurses_render(nc);
    }

    void render_window(Window& win, bool is_active) {
        auto& buf = *buffers[win.buffer_idx];
        update_window_scroll(win, buf);

        std::set<std::pair<int, int>> cursor_set;
        for (const auto& c : win.cursors) {
            cursor_set.insert({c.y, c.x});
        }

        for (int r = 0; r < win.h; ++r) {
            int line_idx = win.scroll_y + r;
            int draw_y = win.y + r;

            // Line numbers / Border
            if (is_active) {
                ncplane_set_fg_rgb8(stdplane, 80, 160, 200);
            } else {
                ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
            }
            ncplane_set_bg_rgb8(stdplane, 22, 22, 26);

            if (line_idx < static_cast<int>(buf.lines.size())) {
                ncplane_printf_yx(stdplane, draw_y, win.x, "%3d ", line_idx + 1);
            } else {
                ncplane_putstr_yx(stdplane, draw_y, win.x, "  ~ ");
            }

            // Line content
            int text_avail_w = win.w - LINE_NUM_W;
            if (text_avail_w <= 0) continue;

            if (line_idx < static_cast<int>(buf.lines.size())) {
                const std::string& line = buf.lines[line_idx];
                for (int c = 0; c < text_avail_w; ++c) {
                    int char_idx = win.scroll_x + c;
                    int draw_x = win.x + LINE_NUM_W + c;

                    bool has_cursor = cursor_set.count({line_idx, char_idx});

                    if (has_cursor) {
                        // Multi-cursor highlight (Bright Amber/Orange)
                        ncplane_set_fg_rgb8(stdplane, 0, 0, 0);
                        ncplane_set_bg_rgb8(stdplane, 255, 180, 50);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
                        ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                    }

                    if (char_idx < static_cast<int>(line.size())) {
                        char ch[2] = {line[char_idx], '\0'};
                        ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                    } else if (has_cursor && char_idx == static_cast<int>(line.size())) {
                        ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                    } else {
                        ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                        ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                    }
                }
            } else {
                ncplane_set_bg_rgb8(stdplane, 16, 16, 18);
                std::string empty(text_avail_w, ' ');
                ncplane_putstr_yx(stdplane, draw_y, win.x + LINE_NUM_W, empty.c_str());
            }
        }
    }

    void render_status_bar(int y, unsigned int screen_w) {
        auto& win = active_win();
        auto& buf = active_buf();
        Cursor primary = win.cursors.empty() ? Cursor{0, 0} : win.cursors.front();

        // 1. Mode Badge
        if (mode == Mode::NORMAL) {
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 80, 210, 120);
            ncplane_putstr_yx(stdplane, y, 0, " NORMAL ");
        } else {
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 80, 170, 255);
            ncplane_putstr_yx(stdplane, y, 0, " INSERT ");
        }

        // 2. Bar Info
        ncplane_set_fg_rgb8(stdplane, 230, 230, 230);
        ncplane_set_bg_rgb8(stdplane, 40, 44, 52);

        char left_info[256];
        snprintf(left_info, sizeof(left_info), " [Win %d/%zu] Buf (%zu/%zu): %s %s",
                 win.id, windows.size(), win.buffer_idx + 1, buffers.size(),
                 buf.name.c_str(), (!buf.undo_stack.empty() ? "[+]" : ""));

        char right_info[256];
        snprintf(right_info, sizeof(right_info), "Cursors: %zu | Ln %d, Col %d | %zu lines ",
                 win.cursors.size(), primary.y + 1, primary.x + 1, buf.lines.size());

        int badge_w = 8;
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

    void render_info_bar(int y, unsigned int screen_w) {
        ncplane_set_fg_rgb8(stdplane, 240, 200, 100);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);

        std::string bar = " " + info_msg;
        if (static_cast<int>(bar.size()) < static_cast<int>(screen_w)) {
            bar.append(screen_w - bar.size(), ' ');
        } else {
            bar = bar.substr(0, screen_w);
        }
        ncplane_putstr_yx(stdplane, y, 0, bar.c_str());
    }
};

int main() {
    try {
        VimEngine engine;
        engine.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
// sudo apt install libnotcurses-dev libnotcurses-core-dev
// g++ -std=c++17 -O2 editor.cpp -o editor -lnotcurses-core -lnotcurses
