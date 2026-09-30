#pragma once
#include "types.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "window.hpp"
#include "git.hpp"
#include "workspace.hpp"
#include "command.hpp"
#include "keymap.hpp"
#include <notcurses/notcurses.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

class VimEngine {
public:
    struct YankRegister {
        bool is_linewise{true};
        std::vector<std::string> lines;
        std::string text;
    };
    YankRegister yank_reg;
    KeymapConfig keymap;

    int workspace_slot{-1};      // Session-local active workspace slot (0..4, or -1)
    bool workspace_active{false}; // True if session has adopted an active workspace

    Mode get_mode() const { return mode; }

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
    int cmd_cursor_pos{0};
    std::vector<std::string> cmd_history;
    int cmd_history_idx{-1};
    std::string cmd_history_draft;
    void load_cmd_history();
    void save_cmd_history();
    int next_win_id{2};
    bool show_settings_popup{false};

    // F6 Theme selection popup state
    bool show_theme_popup{false};
    std::vector<std::string> theme_list;
    int theme_selected_idx{0};
    int theme_scroll{0};
    std::string theme_original; // theme active when popup opened; restored on cancel
    void preview_theme(const std::string& name);

    // Workspace Slots (0..4) state
    bool show_workspace_list{false};
    int workspace_cursor{0};
    bool workspace_editing{false};
    std::string workspace_edit_draft;
    std::string workspace_status_msg;
    int settings_selected_idx{0};

    bool show_git_hunk_popup{false};
    int active_hunk_idx{0};

    // F1 Git Status View (gitview.md) state
    bool show_git_status{false};
    GitViewData git_view_data;
    struct GitStatusRow {
        enum Kind {
            HEADER,
            PLACEHOLDER,
            STAGE_FILE,
            UNSTAGE_FILE,
            UNTRACKED_FILE,
            COMMIT1_FILE,
            COMMIT2_FILE,
            STASH,
            BRANCH
        } kind{HEADER};
        int section_idx{0};
        std::string path;
        char glyph{' '};
        int stash_idx{0};
        std::string stash_ref;
        std::string stash_subject;
        std::string branch_name;
        bool is_current_branch{false};
        std::string reltime;
        std::string commit_hash;
    };
    std::vector<GitStatusRow> git_status_rows;
    int git_status_cursor{0};
    int git_status_scroll_y{0};
    std::string git_status_msg;

    struct GitUnifiedLine {
        enum Type { META, HUNK_HDR, CONTEXT, REMOVED, ADDED, ELLIPSIS } type{META};
        std::string text;
    };
    std::vector<GitUnifiedLine> git_status_right_lines;
    int git_status_right_scroll_y{0};

    bool git_stash_action_active{false};
    std::string git_stash_action_ref;
    int git_stash_action_idx{0};
    std::string git_stash_status_msg;

    // F14 full-screen side-by-side hunk diff / delta diff state
    bool show_hunk_diff{false};
    bool hunk_diff_is_delta{false};
    std::string hunk_diff_left_name;
    std::string hunk_diff_right_name;
    std::shared_ptr<TextBuffer> hunk_diff_left_buf{nullptr};
    std::shared_ptr<TextBuffer> hunk_diff_right_buf{nullptr};
    std::string hunk_diff_focus{"left"}; // "left" (Working/file1) or "right" (HEAD/file2)
    int hunk_diff_cursor_row{0};
    int hunk_diff_scroll_y{0};
    std::string hunk_diff_status_msg;
    std::vector<std::string> hunk_diff_head_lines;
    AlignedDiff hunk_diff_diff;
    std::shared_ptr<SyntaxHighlighter> hunk_diff_head_syntax;

    // Manual block markers for left (working) and right (HEAD)
    bool manual_left_active{false};
    int manual_left_start{-1};
    int manual_left_end{-1};

    bool manual_right_active{false};
    int manual_right_start{-1};
    int manual_right_end{-1};

    enum class WhichKeyMode {
        LEADER,
        WINDOW
    };
    WhichKeyMode whichkey_mode{WhichKeyMode::LEADER};
    bool leader_pending{false};
    bool leader_p_pending{false};
    bool ctrl_w_pending{false};
    bool show_whichkey_popup{false};
    std::chrono::steady_clock::time_point leader_start_time;
    std::chrono::steady_clock::time_point ctrl_w_start_time;

    bool show_buffer_list{false};
    std::string buffer_list_query;
    std::vector<size_t> buffer_list_filtered_indices;
    int buffer_list_selected_idx{0};
    int buffer_list_scroll{0};

    bool show_cmd_completion{false};
    std::string cmd_completion_prefix;
    std::string cmd_completion_base_cmd;
    std::vector<std::string> cmd_completion_candidates;
    int cmd_completion_selected_idx{0};
    int cmd_completion_scroll_row{0};

    bool show_mini_help{false};
    bool show_filepicker{false};
    bool filepicker_tree_mode{false};
    std::string filepicker_cur_dir;
    struct TreeEntry {
        std::string name;
        std::string full_path;
        bool is_dir{false};
        bool is_parent{false};
    };
    std::vector<TreeEntry> filepicker_tree_entries;
    std::vector<TreeEntry> filepicker_filtered_tree;
    std::string filepicker_query;
    std::vector<std::string> filepicker_all_files;
    std::vector<std::string> filepicker_filtered_files;
    std::unordered_map<std::string, char> filepicker_git_status;
    int filepicker_selected_idx{0};
    int filepicker_scroll{0};

    // Buffer search state ('/' incremental search, n/N)
    bool search_active{false};
    std::string search_query;
    std::string search_input;
    int search_input_cursor{0};
    bool search_highlight_on{false};
    Cursor search_start_cursor{0, 0};
    int search_start_scroll{0};
    int search_current_match_idx{-1};
    int search_total_matches{0};

    // Visual command range ('<, '>)
    int visual_range_start_y{-1};
    int visual_range_end_y{-1};
    Mode visual_save_mode{Mode::NORMAL};

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
    int compute_display_col(const std::string& line, int byte_x) const;
    size_t compute_byte_offset(const std::string& line, int target_col) const;
public:
    ConfigManager& get_config() { return config; }
    std::vector<std::shared_ptr<TextBuffer>>& get_buffers() { return buffers; }
    const std::vector<std::shared_ptr<TextBuffer>>& get_buffers() const { return buffers; }
    void set_info_msg(std::string msg);
    Window& active_win();
    TextBuffer& active_buf();

    void jump_to_prev_hunk();
    void jump_to_next_hunk();
    void open_git_hunk_popup();
    void revert_active_hunk();
    void handle_git_hunk_popup(const ncinput& ni, uint32_t key);
    void render_git_hunk_popup(unsigned int screen_h, unsigned int screen_w);

    void open_git_status();
    void close_git_status();
    void refresh_git_status();
    void refresh_git_status_right();
    void rebuild_git_status_rows();
    void handle_git_status_input(const ncinput& ni, uint32_t key);
    void render_git_status(unsigned int screen_h, unsigned int screen_w);

    void open_hunk_diff();
    void open_delta_diff(std::shared_ptr<TextBuffer> left_buf, std::shared_ptr<TextBuffer> right_buf);
    void open_file_diff(const std::string& path1, const std::string& path2);
    void close_hunk_diff();
    void handle_hunk_diff_input(const ncinput& ni, uint32_t key);
    void render_hunk_diff(unsigned int screen_h, unsigned int screen_w);

    void open_filepicker();
    void scan_project_files();
    void scan_filepicker_tree();
    void filter_filepicker_files();
    void handle_filepicker_input(const ncinput& ni, uint32_t key);
    void render_filepicker(unsigned int screen_h, unsigned int screen_w);

    void open_search();
    void handle_search_input(const ncinput& ni, uint32_t key);
    void update_search_incremental();
    void search_jump_next();
    void search_jump_prev();
    void clear_search_highlights();

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

    void next_buffer();
    void prev_buffer();
    void switch_to_buffer(size_t idx);
    void open_buffer_list();
    void filter_buffer_list();
    void handle_buffer_list_input(const ncinput& ni, uint32_t key);
    void render_buffer_list(unsigned int screen_h, unsigned int screen_w);

    void handle_whichkey_popup(const ncinput& ni, uint32_t key);
    void render_whichkey_popup(unsigned int screen_h, unsigned int screen_w);

    void render_mini_help(unsigned int screen_h, unsigned int screen_w);

    void handle_settings_popup(const ncinput& ni, uint32_t key);
    void render_settings_popup(unsigned int screen_h, unsigned int screen_w);

    void open_theme_popup();
    void close_theme_popup();
    void scan_themes();
    void handle_theme_popup_input(const ncinput& ni, uint32_t key);
    void render_theme_popup(unsigned int screen_h, unsigned int screen_w);

    void open_workspace_list();
    void close_workspace_list();
    bool load_workspace(int slot, bool show_msg = true);
    bool save_workspace(int slot, bool show_msg = true);
    bool delete_workspace(int slot);
    bool clear_active_workspace();
    bool rename_workspace(int slot, const std::string& desc);
    std::string get_workspace_description(int slot);
    void handle_workspace_list_input(const ncinput& ni, uint32_t key);
    void render_workspace_list(unsigned int screen_h, unsigned int screen_w);

    std::unordered_map<std::string, FilePosition> file_positions;
    std::string get_position_path() const;
    void load_positions();
    void save_positions();
    std::string normalize_position_key(const std::string& key) const;
    void set_file_position(const std::string& key, int y, int x, int scroll_y);
    bool get_file_position(const std::string& key, FilePosition& pos) const;
    void save_window_position(const Window& win, const TextBuffer& buf);
    void restore_window_position(Window& win, const TextBuffer& buf);
    void save_all_positions();

    void split_window(SplitType type);
    void close_active_window();
    void layout_windows();

    void handle_key_input(const ncinput& ni, uint32_t key);
    bool handle_global_shortcuts(const ncinput& ni, uint32_t key);
    bool handle_navigation(const ncinput& ni, uint32_t key);
    void copy_to_system_clipboard(const std::string& text);
    std::string get_system_clipboard();
    void copy_selection_to_clipboard();
    void paste_text_raw(const std::string& text);
    void paste_from_clipboard(bool bracket_paste = true);
    void paste_full_replace();
    bool handle_bracketed_paste_fast();
    bool handle_paste_burst(const ncinput& ni, uint32_t key);

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
    bool execute_substitute(const std::string& cmd_str);

    void update_window_scroll(Window& win, const TextBuffer& buf);
    void render();
    void render_window(Window& win, bool is_active);
    void render_window_separator(int y, unsigned int screen_w, bool is_active);
    void render_status_bar(int y, unsigned int screen_w);
    void render_info_bar(int y, unsigned int screen_w);
};
