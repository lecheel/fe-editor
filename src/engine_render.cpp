#include "engine.hpp"
#include "log.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <wchar.h>

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

int VimEngine::compute_display_col(const std::string& line, int byte_x) const {
    int col = 0;
    size_t bi = 0;
    while (bi < line.size() && static_cast<int>(bi) < byte_x) {
        unsigned char lead = static_cast<unsigned char>(line[bi]);
        int clen = Keymap::utf8_char_len(lead);
        if (bi + clen > line.size()) clen = static_cast<int>(line.size() - bi);
        int cw = 1;
        wchar_t wc = 0;
        mbstate_t mbs = {};
        if (mbrtowc(&wc, line.data() + bi, clen, &mbs) > 0) {
            int w = wcwidth(wc);
            if (w > 0) cw = w;
        }
        col += cw;
        bi += clen;
    }
    return col;
}

size_t VimEngine::compute_byte_offset(const std::string& line, int target_col) const {
    size_t bi = 0;
    int col = 0;
    while (bi < line.size() && col < target_col) {
        unsigned char lead = static_cast<unsigned char>(line[bi]);
        int clen = Keymap::utf8_char_len(lead);
        if (bi + clen > line.size()) clen = static_cast<int>(line.size() - bi);
        int cw = 1;
        wchar_t wc = 0;
        mbstate_t mbs = {};
        if (mbrtowc(&wc, line.data() + bi, clen, &mbs) > 0) {
            int w = wcwidth(wc);
            if (w > 0) cw = w;
        }
        if (col + cw > target_col) break;
        col += cw;
        bi += clen;
    }
    return bi;
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

    // Horizontal scrolling for long lines
    int gutter_w = get_line_num_w(buf);
    int text_avail_w = win.w - gutter_w;
    if (text_avail_w <= 0) return;

    int cursor_col = primary.x;
    if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
        cursor_col = compute_display_col(buf.lines[primary.y], primary.x);
    }

    // If the cursor's line fits entirely within the viewport, reset horizontal scroll
    if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
        int line_disp_w = compute_display_col(buf.lines[primary.y], static_cast<int>(buf.lines[primary.y].size()));
        if (line_disp_w <= text_avail_w) {
            win.scroll_x = 0;
        }
    }

    int h_scrolloff = std::min(2, text_avail_w > 4 ? text_avail_w / 4 : 0);

    if (cursor_col < win.scroll_x + h_scrolloff) {
        win.scroll_x = std::max(0, cursor_col - h_scrolloff);
    }
    if (cursor_col >= win.scroll_x + text_avail_w - h_scrolloff) {
        win.scroll_x = cursor_col - text_avail_w + h_scrolloff + 1;
    }
    win.scroll_x = std::max(0, win.scroll_x);
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
    if (mode == Mode::COMMAND || search_active) {
        int avail_w = std::max(1, static_cast<int>(screen_w) - 1);
        int pos = search_active ? search_input_cursor : cmd_cursor_pos;
        int view_start = 0;
        if (pos >= avail_w) {
            view_start = pos - avail_w + 1;
        }
        int cursor_x = std::clamp(1 + (pos - view_start), 1, static_cast<int>(screen_w) - 1);
        notcurses_cursor_enable(nc, screen_h - 1, cursor_x);
    } else {
        auto& aw = active_win();
        if (!aw.cursors.empty()) {
            Cursor primary = aw.cursors.front();
            int gutter_w = get_line_num_w(active_buf());
            int screen_cy = aw.y + (primary.y - aw.scroll_y);
            int cursor_col = 0;
            if (primary.y >= 0 && primary.y < static_cast<int>(active_buf().lines.size())) {
                const std::string& pline = active_buf().lines[primary.y];
                size_t bi = 0;
                while (bi < pline.size() && static_cast<int>(bi) < primary.x) {
                    unsigned char lead = static_cast<unsigned char>(pline[bi]);
                    int clen = Keymap::utf8_char_len(lead);
                    if (bi + clen > pline.size()) clen = static_cast<int>(pline.size() - bi);
                    std::string g = pline.substr(bi, clen);
                    int cw = 1;
                    wchar_t wc = 0;
                    mbstate_t mbs = {};
                    if (mbrtowc(&wc, g.data(), g.size(), &mbs) > 0) {
                        int w = wcwidth(wc);
                        if (w > 0) cw = w;
                    }
                    cursor_col += cw;
                    bi += clen;
                }
            } else {
                cursor_col = primary.x;
            }
            int screen_cx = aw.x + gutter_w + cursor_col - aw.scroll_x;
            if (screen_cy >= aw.y && screen_cy < aw.y + aw.h &&
                screen_cx >= aw.x + gutter_w && screen_cx < aw.x + aw.w) {
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
    } else if (show_theme_popup) {
        render_theme_popup(screen_h, screen_w);
        notcurses_cursor_disable(nc);
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
    } else if (show_workspace_list) {
        render_workspace_list(screen_h, screen_w);
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