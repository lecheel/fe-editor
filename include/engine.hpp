#pragma once
#include "types.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "window.hpp"
#include "git.hpp"
#include "command.hpp"
#include <notcurses/notcurses.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

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

    bool show_git_hunk_popup{false};
    int active_hunk_idx{0};

    bool leader_pending{false};
    bool show_whichkey_popup{false};
    std::chrono::steady_clock::time_point leader_start_time;

    // Command mode :e file completion state
    bool show_cmd_completion{false};
    std::string cmd_completion_prefix;
    std::string cmd_completion_base_cmd;
    std::vector<std::string> cmd_completion_candidates;
    int cmd_completion_selected_idx{0};
    int cmd_completion_scroll_row{0};

    bool show_mini_help{false};
    bool show_filepicker{false};
    std::string filepicker_query;
    std::vector<std::string> filepicker_all_files;
    std::vector<std::string> filepicker_filtered_files;
    int filepicker_selected_idx{0};
    int filepicker_scroll{0};

    // Ripgrep grouped search state
    struct RgMatch {
        std::string file;
        int line{1};
        int col{1};
        std::string text;
        bool ignored{false};
    };
    struct RgGroup {
        std::string file;
        std::vector<RgMatch> matches;
    };
    struct RgDisplayLine {
        bool is_file_header{false};
        std::string file;
        int match_idx{-1}; // index in flattened matches if !is_file_header
        int line{1};
        int col{1};
        std::string text;
    };

    bool show_rg_popup{false};
    bool rg_replace_active{false};
    std::string rg_replace_query;
    std::string rg_query;
    std::vector<RgGroup> rg_groups;
    std::vector<RgMatch> rg_flattened_matches;
    std::vector<RgDisplayLine> rg_display_lines;
    int rg_selected_match_idx{0};
    int rg_selected_display_idx{0};
    int rg_scroll{0};

    std::string project_dir;
    std::string project_name;

    ConfigManager config;

    static std::string detect_project_dir(const std::string& start_path = "");

    int get_line_num_w(const TextBuffer& buf) const;
public:
    void set_info_msg(std::string msg);
    Window& active_win();
    TextBuffer& active_buf();

    void jump_to_prev_hunk();
    void jump_to_next_hunk();
    void open_git_hunk_popup();
    void revert_active_hunk();
    void handle_git_hunk_popup(const ncinput& ni, uint32_t key);
    void render_git_hunk_popup(unsigned int screen_h, unsigned int screen_w);

    void open_filepicker();
    void scan_project_files();
    void filter_filepicker_files();
    void handle_filepicker_input(const ncinput& ni, uint32_t key);
    void render_filepicker(unsigned int screen_h, unsigned int screen_w);

    std::string get_word_under_cursor();
    std::string get_rg_cache_path() const;
    void run_ripgrep(const std::string& pattern);
    void save_rg_cache();
    void load_rg_cache();
    void rebuild_rg_display_lines();
    void open_selected_rg_match();
    void apply_rg_replace();
    void handle_rg_popup_input(const ncinput& ni, uint32_t key);
    void render_rg_popup(unsigned int screen_h, unsigned int screen_w);

    void handle_whichkey_popup(const ncinput& ni, uint32_t key);
    void render_whichkey_popup(unsigned int screen_h, unsigned int screen_w);

    void render_mini_help(unsigned int screen_h, unsigned int screen_w);

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
    void trigger_cmd_completion();
    void update_cmd_completion();
    void update_cmd_completion_preview();
    void close_cmd_completion();
    void handle_cmd_completion_input(const ncinput& ni, uint32_t key);
    void render_cmd_completion(unsigned int screen_h, unsigned int screen_w);

    void handle_command_mode(const ncinput& ni, uint32_t key);
    void handle_insert_mode(const ncinput& ni, uint32_t key);
    void execute_command(const std::string& cmd_str);

    void update_window_scroll(Window& win, const TextBuffer& buf);
    void render();
    void render_window(Window& win, bool is_active);
    void render_status_bar(int y, unsigned int screen_w);
    void render_info_bar(int y, unsigned int screen_w);
};