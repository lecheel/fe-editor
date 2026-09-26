#include <notcurses/notcurses.h>
#include <clocale>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

enum class Mode {
    NORMAL,
    INSERT
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

        // Initial sample buffer
        lines = {
            "// Welcome to your Notcurses Vim-Modal Engine!",
            "// Press 'i' to enter INSERT mode, 'Esc' for NORMAL mode.",
            "// Press 'Alt-q' to quit at any time.",
            "",
            "int main() {",
            "    std::cout << \"Hello, Modern TUI!\" << std::endl;",
            "    return 0;",
            "}",
            "",
            "Line 10", "Line 11", "Line 12", "Line 13", "Line 14", "Line 15"
        };
    }

    ~VimEngine() {
        if (nc) {
            notcurses_stop(nc);
        }
    }

    void run() {
        bool running = true;
        while (running) {
            render();

            ncinput ni;
            uint32_t key = notcurses_get(nc, nullptr, &ni);

            // Global Quit Command: Alt-q / Alt-Q
            if (ni.alt && (ni.id == 'q' || ni.id == 'Q')) {
                break;
            }

            if (mode == Mode::NORMAL) {
                running = handle_normal_mode(ni, key);
            } else if (mode == Mode::INSERT) {
                running = handle_insert_mode(ni, key);
            }
        }
    }

private:
    struct notcurses* nc = nullptr;
    struct ncplane* stdplane = nullptr;

    Mode mode = Mode::NORMAL;
    std::vector<std::string> lines;
    
    // Cursor position in buffer space
    int cursor_y = 0;
    int cursor_x = 0;

    // Viewport offset (scrolling)
    int scroll_y = 0;

    const int LINE_NUM_WIDTH = 4; // Margin for line numbers

    // Common navigation keys across both modes
    bool handle_navigation(const ncinput& ni, uint32_t key, unsigned int screen_h) {
        switch (key) {
            case NCKEY_UP:
                cursor_y = std::max(0, cursor_y - 1);
                clamp_cursor_x();
                return true;
            case NCKEY_DOWN:
                cursor_y = std::min(static_cast<int>(lines.size()) - 1, cursor_y + 1);
                clamp_cursor_x();
                return true;
            case NCKEY_LEFT:
                cursor_x = std::max(0, cursor_x - 1);
                return true;
            case NCKEY_RIGHT:
                cursor_x = std::min(get_line_max_x(), cursor_x + 1);
                return true;
            case NCKEY_HOME:
                cursor_x = 0;
                return true;
            case NCKEY_END:
                cursor_x = get_line_max_x();
                return true;
            case NCKEY_PGUP: {
                int page = static_cast<int>(screen_h) - 2;
                cursor_y = std::max(0, cursor_y - page);
                clamp_cursor_x();
                return true;
            }
            case NCKEY_PGDOWN: {
                int page = static_cast<int>(screen_h) - 2;
                cursor_y = std::min(static_cast<int>(lines.size()) - 1, cursor_y + page);
                clamp_cursor_x();
                return true;
            }
            default:
                return false;
        }
    }

    bool handle_normal_mode(const ncinput& ni, uint32_t key) {
        unsigned int h, w;
        ncplane_dim_yx(stdplane, &h, &w);

        // Arrow and Page keys
        if (handle_navigation(ni, key, h)) {
            return true;
        }

        switch (key) {
            // Vim Direction Keys
            case 'h':
                cursor_x = std::max(0, cursor_x - 1);
                break;
            case 'l':
                cursor_x = std::min(get_line_max_x(), cursor_x + 1);
                break;
            case 'k':
                cursor_y = std::max(0, cursor_y - 1);
                clamp_cursor_x();
                break;
            case 'j':
                cursor_y = std::min(static_cast<int>(lines.size()) - 1, cursor_y + 1);
                clamp_cursor_x();
                break;

            // Vim Jump Keys
            case '0':
            case '^':
                cursor_x = 0;
                break;
            case '$':
                cursor_x = get_line_max_x();
                break;
            case 'G':
                cursor_y = static_cast<int>(lines.size()) - 1;
                clamp_cursor_x();
                break;

            // Enter Insert Mode
            case 'i':
                mode = Mode::INSERT;
                break;
            case 'a':
                mode = Mode::INSERT;
                cursor_x = std::min(static_cast<int>(lines[cursor_y].size()), cursor_x + 1);
                break;
            case 'o':
                // Open new line below
                lines.insert(lines.begin() + cursor_y + 1, "");
                cursor_y++;
                cursor_x = 0;
                mode = Mode::INSERT;
                break;
        }
        return true;
    }

    bool handle_insert_mode(const ncinput& ni, uint32_t key) {
        unsigned int h, w;
        ncplane_dim_yx(stdplane, &h, &w);

        // Arrow and Page keys still work in Insert mode
        if (handle_navigation(ni, key, h)) {
            return true;
        }

        if (key == NCKEY_ESC) {
            mode = Mode::NORMAL;
            clamp_cursor_x();
            return true;
        }

        if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
            std::string& current = lines[cursor_y];
            std::string rest = current.substr(cursor_x);
            current = current.substr(0, cursor_x);
            lines.insert(lines.begin() + cursor_y + 1, rest);
            cursor_y++;
            cursor_x = 0;
            return true;
        }

        if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
            if (cursor_x > 0) {
                std::string& current = lines[cursor_y];
                current.erase(cursor_x - 1, 1);
                cursor_x--;
            } else if (cursor_y > 0) {
                // Merge with previous line
                int prev_len = lines[cursor_y - 1].size();
                lines[cursor_y - 1] += lines[cursor_y];
                lines.erase(lines.begin() + cursor_y);
                cursor_y--;
                cursor_x = prev_len;
            }
            return true;
        }

        // Standard character insertion (UTF-8 / ASCII)
        if (key >= 32 && key != NCKEY_ESC) {
            std::string& current = lines[cursor_y];
            // If UTF-8 payload exists, use it; otherwise cast single char
            if (ni.utf8[0] != '\0') {
                current.insert(cursor_x, reinterpret_cast<const char*>(ni.utf8));
                cursor_x += std::string(reinterpret_cast<const char*>(ni.utf8)).size();
            } else {
                current.insert(cursor_x, 1, static_cast<char>(key));
                cursor_x++;
            }
        }

        return true;
    }

    int get_line_max_x() const {
        if (lines.empty()) return 0;
        int len = static_cast<int>(lines[cursor_y].size());
        if (mode == Mode::NORMAL) {
            return std::max(0, len - 1);
        }
        return len; // In Insert mode, cursor can sit one past the end
    }

    void clamp_cursor_x() {
        cursor_x = std::min(cursor_x, get_line_max_x());
    }

    void update_scrolling(unsigned int text_view_h) {
        if (cursor_y < scroll_y) {
            scroll_y = cursor_y;
        }
        if (cursor_y >= scroll_y + static_cast<int>(text_view_h)) {
            scroll_y = cursor_y - text_view_h + 1;
        }
    }

    void render() {
        ncplane_erase(stdplane);

        unsigned int screen_h, screen_w;
        ncplane_dim_yx(stdplane, &screen_h, &screen_w);
        
        unsigned int text_view_h = screen_h - 1; // Reserve bottom line for status bar
        update_scrolling(text_view_h);

        // 1. Render Buffer Lines
        for (unsigned int i = 0; i < text_view_h; ++i) {
            int line_idx = scroll_y + i;
            if (line_idx < static_cast<int>(lines.size())) {
                // Draw Line Number (Dim Cyan)
                ncplane_set_fg_rgb8(stdplane, 80, 120, 140);
                ncplane_set_bg_rgb8(stdplane, 20, 20, 20);
                ncplane_printf_yx(stdplane, i, 0, "%3d ", line_idx + 1);

                // Draw Text Line Content
                ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
                ncplane_set_bg_rgb8(stdplane, 15, 15, 15);

                std::string text = lines[line_idx];
                int available_w = static_cast<int>(screen_w) - LINE_NUM_WIDTH;
                if (available_w > 0) {
                    if (static_cast<int>(text.size()) > available_w) {
                        text = text.substr(0, available_w);
                    }
                    ncplane_putstr_yx(stdplane, i, LINE_NUM_WIDTH, text.c_str());
                }
            } else {
                // Vim ~ Tilde lines
                ncplane_set_fg_rgb8(stdplane, 60, 60, 80);
                ncplane_set_bg_rgb8(stdplane, 15, 15, 15);
                ncplane_putstr_yx(stdplane, i, 0, "~");
            }
        }

        // 2. Render Status Bar (Bottom line)
        render_status_bar(screen_h - 1, screen_w);

        // 3. Move Hardware Cursor to current edit position
        int screen_cursor_y = cursor_y - scroll_y;
        int screen_cursor_x = LINE_NUM_WIDTH + cursor_x;
        ncplane_cursor_move_yx(stdplane, screen_cursor_y, screen_cursor_x);

        notcurses_render(nc);
    }

    void render_status_bar(int y, unsigned int screen_w) {
        // Mode badge color styling
        if (mode == Mode::NORMAL) {
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 100, 200, 100); // Green
            ncplane_putstr_yx(stdplane, y, 0, " NORMAL ");
        } else {
            ncplane_set_fg_rgb8(stdplane, 15, 15, 15);
            ncplane_set_bg_rgb8(stdplane, 100, 180, 255); // Blue
            ncplane_putstr_yx(stdplane, y, 0, " INSERT ");
        }

        // Status Details (Dark Gray background)
        ncplane_set_fg_rgb8(stdplane, 200, 200, 200);
        ncplane_set_bg_rgb8(stdplane, 40, 40, 40);

        char info[128];
        snprintf(info, sizeof(info), " Ln %d, Col %d | %zu lines | [Alt-q] Quit ", 
                 cursor_y + 1, cursor_x + 1, lines.size());
        
        int badge_len = 8;
        int remaining_w = static_cast<int>(screen_w) - badge_len;
        if (remaining_w > 0) {
            // Fill background
            std::string filler(remaining_w, ' ');
            ncplane_putstr_yx(stdplane, y, badge_len, filler.c_str());

            // Right-aligned status text
            int info_len = static_cast<int>(std::string(info).size());
            int info_x = std::max(badge_len, static_cast<int>(screen_w) - info_len);
            ncplane_putstr_yx(stdplane, y, info_x, info);
        }
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
