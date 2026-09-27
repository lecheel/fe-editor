#include "engine.hpp"
#include "log.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

void VimEngine::render_window_separator(int y, unsigned int screen_w, bool is_active) {
    if (is_active) {
        ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
    } else {
        ncplane_set_fg_rgb8(stdplane, 65, 75, 95);
    }
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    std::string sep(screen_w, '-');
    ncplane_putstr_yx(stdplane, y, 0, sep.c_str());
}

void VimEngine::update_window_scroll(Window& win, const TextBuffer& buf) {
    if (win.cursors.empty()) return;
    Cursor primary = win.cursors.front();

    int scrolloff = std::max(0, config.settings.scroll_offset);
    int effective_scrolloff = std::min(scrolloff, win.h > 1 ? (win.h - 1) / 2 : 0);

    if (primary.y < win.scroll_y + effective_scrolloff) {
        win.scroll_y = primary.y - effective_scrolloff;
    }
    if (primary.y >= win.scroll_y + win.h - effective_scrolloff) {
        win.scroll_y = primary.y - win.h + effective_scrolloff + 1;
    }
    win.scroll_y = std::max(0, win.scroll_y);
}

void VimEngine::render() {
    ncplane_erase(stdplane);

    unsigned int screen_h, screen_w;
    ncplane_dim_yx(stdplane, &screen_h, &screen_w);

    for (size_t wi = 0; wi < windows.size(); ++wi) {
        render_window(windows[wi], wi == active_win_idx);
    }

    if (split_mode != SplitType::VERTICAL && windows.size() > 1) {
        for (size_t wi = 0; wi + 1 < windows.size(); ++wi) {
            int sep_y = windows[wi].y + windows[wi].h;
            render_window_separator(sep_y, screen_w, wi == active_win_idx);
        }
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

    if (show_git_status) {
        render_git_status(screen_h, screen_w);
        if (!git_stash_action_active) {
            notcurses_cursor_disable(nc);
        }
    } else if (show_mini_help) {
        render_mini_help(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_buffer_list) {
        render_buffer_list(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_settings_popup) {
        render_settings_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_hunk_diff) {
        render_hunk_diff(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_rg_popup) {
        render_rg_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_filepicker) {
        render_filepicker(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_git_hunk_popup) {
        render_git_hunk_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    } else if (show_whichkey_popup) {
        render_whichkey_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
    }

    if (mode == Mode::COMMAND && show_cmd_completion) {
        render_cmd_completion(screen_h, screen_w);
    }

    notcurses_render(nc);
}