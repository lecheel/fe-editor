#pragma once
#include "types.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "window.hpp"
#include <notcurses/notcurses.h>
#include <memory>
#include <vector>
#include <string>

class VimEngine {
public:
    explicit VimEngine(bool verbose = false, const std::vector<std::string>& files = {});
    ~VimEngine();

    void run();

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
    std::string cmd_buffer;
    int next_win_id{2};
    bool show_settings_popup{false};
    int settings_selected_idx{0};

    ConfigManager config;

    int get_line_num_w(const TextBuffer& buf) const;
    void set_info_msg(std::string msg);
    Window& active_win();
    TextBuffer& active_buf();

    void handle_settings_popup(const ncinput& ni, uint32_t key);
    void render_settings_popup(unsigned int screen_h, unsigned int screen_w);

    void save_window_position(const Window& win, const TextBuffer& buf);
    void restore_window_position(Window& win, const TextBuffer& buf);
    void save_all_positions();

    void split_window(SplitType type);
    void close_active_window();
    void layout_windows();

    bool handle_navigation(const ncinput& ni, uint32_t key);
    void handle_normal_mode(const ncinput& ni, uint32_t key);
    void handle_visual_mode(const ncinput& ni, uint32_t key);
    void handle_command_mode(const ncinput& ni, uint32_t key);
    void handle_insert_mode(const ncinput& ni, uint32_t key);
    void execute_command(const std::string& cmd_str);

    void update_window_scroll(Window& win, const TextBuffer& buf);
    void render();
    void render_window(Window& win, bool is_active);
    void render_status_bar(int y, unsigned int screen_w);
    void render_info_bar(int y, unsigned int screen_w);
};