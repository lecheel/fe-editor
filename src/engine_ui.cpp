#include "engine.hpp"
#include "autocomplete.hpp"
#include "log.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <set>
#include <wchar.h>

extern int g_hunk_marker_style;

namespace fs = std::filesystem;

void VimEngine::render_workspace_list(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(56, static_cast<int>(screen_w * 0.70));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 4);
    int popup_h = 10;
    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    ncplane_set_fg_rgb8(stdplane, 170, 115, 250);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }
    ncplane_putstr_yx(stdplane, popup_y + 2, popup_x, "├");
    ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + popup_w - 1, "┤");

    std::string title = " Workspaces (Alt-0 / :ws) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    std::string active_header = (workspace_active && workspace_slot >= 0) ?
        (" [Active: WS" + std::to_string(workspace_slot) + "] ") : " [No Active WS] ";
    int ah_x = popup_x + popup_w - static_cast<int>(active_header.size()) - 2;
    if (ah_x > popup_x + static_cast<int>(title.size()) + 4) {
        if (workspace_active && workspace_slot >= 0) ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
        else ncplane_set_fg_rgb8(stdplane, 140, 150, 160);
        ncplane_putstr_yx(stdplane, popup_y, ah_x, active_header.c_str());
    }

    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "Slot");
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 9, "Session Contents");
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 34, "Description");

    WorkspaceStorage storage(config.get_config_dir());
    for (int s = 0; s < 5; ++s) {
        int draw_y = popup_y + 3 + s;
        bool is_sel = (s == workspace_cursor);
        bool is_act = (s == workspace_slot && workspace_active);

        WorkspaceSnapshot snap;
        bool populated = storage.load_snapshot(s, snap) && !snap.buffers.empty();

        if (is_sel) {
            ncplane_set_bg_rgb8(stdplane, 45, 65, 115);
            ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        } else {
            ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
            ncplane_set_fg_rgb8(stdplane, 210, 215, 225);
        }

        for (int c = 1; c < popup_w - 1; ++c) {
            ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
        }

        if (is_sel) {
            ncplane_set_fg_rgb8(stdplane, 255, 205, 60);
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 1, "▶");
        }

        std::string slot_tag = "[" + std::to_string(s) + (is_act ? "*" : " ") + "]";
        if (is_act) {
            ncplane_set_fg_rgb8(stdplane, 80, 220, 100);
        } else if (is_sel) {
            ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        } else {
            ncplane_set_fg_rgb8(stdplane, 160, 170, 185);
        }
        ncplane_putstr_yx(stdplane, draw_y, popup_x + 3, slot_tag.c_str());

        std::string info_col;
        if (populated) {
            int nf = static_cast<int>(snap.buffers.size());
            int nw = static_cast<int>(snap.windows.size());
            info_col = "(" + std::to_string(nf) + (nf == 1 ? " file, " : " files, ") +
                       std::to_string(nw) + (nw == 1 ? " window)" : " windows)");
        } else {
            info_col = "(empty)";
        }

        bool hide_info_for_edit = (is_sel && workspace_editing && !populated);

        if (!hide_info_for_edit) {
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, populated ? 180 : 130, populated ? 220 : 130, populated ? 255 : 140);
            } else {
                ncplane_set_fg_rgb8(stdplane, populated ? 130 : 100, populated ? 160 : 105, populated ? 190 : 115);
            }
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 9, info_col.c_str());
        }

        int desc_x = hide_info_for_edit ? (popup_x + 9) : (popup_x + 34);
        int max_desc_w = (popup_x + popup_w - 2) - desc_x;

        if (is_sel && workspace_editing) {
            std::string disp_draft = workspace_edit_draft + "▏";
            ncplane_set_fg_rgb8(stdplane, 255, 240, 120);
            if (static_cast<int>(disp_draft.size()) > max_desc_w && max_desc_w > 0) {
                disp_draft = disp_draft.substr(0, max_desc_w);
            }
            ncplane_putstr_yx(stdplane, draw_y, desc_x, disp_draft.c_str());
        } else {
            std::string desc = snap.description;
            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            else ncplane_set_fg_rgb8(stdplane, 200, 205, 215);

            if (static_cast<int>(desc.size()) > max_desc_w && max_desc_w > 0) {
                desc = desc.substr(0, max_desc_w);
            }
            ncplane_putstr_yx(stdplane, draw_y, desc_x, desc.c_str());
        }
    }

    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    if (workspace_editing) {
        std::string footer = " [Enter] Commit name  [Esc] Cancel ";
        ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
    } else if (!workspace_status_msg.empty()) {
        std::string footer = " " + workspace_status_msg + " ";
        ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
    } else {
        std::string footer = " [Enter] Load  [s] Save  [c] Clear  [d] Delete  [e] Name  [Esc] Close ";
        ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
    }
}

void VimEngine::render_cmd_completion(unsigned int screen_h, unsigned int screen_w) {
    if (cmd_completion_candidates.empty()) return;

    const int NUM_COLS = 5;
    int total_items = static_cast<int>(cmd_completion_candidates.size());
    int total_rows = (total_items + NUM_COLS - 1) / NUM_COLS;

    // Up to 5 rows of items (capped at 6 if expanded), popup height max 8 lines
    int visible_rows = std::min(total_rows, 5);
    if (total_rows >= 6) {
        visible_rows = 6;
    }
    if (visible_rows < 1) visible_rows = 1;

    int popup_h = std::min(8, visible_rows + 2);
    int popup_w = static_cast<int>(screen_w);
    int popup_x = 0;
    int popup_y = (static_cast<int>(screen_h) - 1) - popup_h;
    popup_y = std::max(0, popup_y);

    int cur_row = cmd_completion_selected_idx / NUM_COLS;
    if (cur_row < cmd_completion_scroll_row) {
        cmd_completion_scroll_row = cur_row;
    }
    if (cur_row >= cmd_completion_scroll_row + visible_rows) {
        cmd_completion_scroll_row = cur_row - visible_rows + 1;
    }
    cmd_completion_scroll_row = std::max(0, cmd_completion_scroll_row);

    // Background fill
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox)
    ncplane_set_fg_rgb8(stdplane, 75, 185, 235);
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

    // Title & count
    std::string title = " Files Completion [2D Grid: 5 cols] ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    char stats[64];
    snprintf(stats, sizeof(stats), " [%d/%d] ", cmd_completion_selected_idx + 1, total_items);
    int stats_x = popup_x + popup_w - static_cast<int>(std::string(stats).size()) - 2;
    if (stats_x > popup_x + static_cast<int>(title.size()) + 2) {
        ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
        ncplane_putstr_yx(stdplane, popup_y, stats_x, stats);
    }

    // Render 5 columns x visible_rows cells
    int avail_w = popup_w - 2;
    int col_w = std::max(10, avail_w / NUM_COLS);

    for (int r = 0; r < visible_rows; ++r) {
        int grid_r = cmd_completion_scroll_row + r;
        int draw_y = popup_y + 1 + r;

        for (int c = 0; c < NUM_COLS; ++c) {
            int item_idx = grid_r * NUM_COLS + c;
            int cell_x = popup_x + 1 + c * col_w;
            int cell_w = (c == NUM_COLS - 1) ? (avail_w - c * col_w) : col_w;

            if (cell_x >= popup_x + popup_w - 1) break;

            if (item_idx < total_items) {
                bool is_sel = (item_idx == cmd_completion_selected_idx);
                const std::string& item = cmd_completion_candidates[item_idx];
                bool is_dir = (!item.empty() && item.back() == '/');

                if (is_sel) {
                    ncplane_set_bg_rgb8(stdplane, 45, 70, 130);
                } else {
                    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
                }

                for (int x = 0; x < cell_w; ++x) {
                    ncplane_putchar_yx(stdplane, draw_y, cell_x + x, ' ');
                }

                if (is_sel) {
                    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
                    ncplane_putstr_yx(stdplane, draw_y, cell_x + 1, "▶ ");
                } else if (is_dir) {
                    ncplane_set_fg_rgb8(stdplane, 80, 200, 240);
                    ncplane_putstr_yx(stdplane, draw_y, cell_x + 1, "📁");
                } else {
                    ncplane_putstr_yx(stdplane, draw_y, cell_x + 1, "  ");
                }

                if (is_sel) {
                    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
                } else if (is_dir) {
                    ncplane_set_fg_rgb8(stdplane, 130, 215, 255);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 215, 220, 230);
                }

                std::string disp = item;
                int max_text_w = cell_w - 4;
                if (max_text_w > 0 && static_cast<int>(disp.size()) > max_text_w) {
                    disp = ".." + disp.substr(disp.size() - (max_text_w - 2));
                }
                if (max_text_w > 0) {
                    ncplane_putstr_yx(stdplane, draw_y, cell_x + 3, disp.c_str());
                }
            }
        }
    }

    // Footer actions
    std::string footer = " [Tab/▲/▼/◀/▶] Navigate  [Enter] Select  [Esc] Cancel ";
    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}

void VimEngine::render_theme_popup(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(44, static_cast<int>(screen_w * 0.45));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 4);
    int popup_h = std::min(14, static_cast<int>(screen_h) - 4);
    popup_h = std::max(6, popup_h);

    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    auto& theme = HelixTheme::instance();
    auto popup_style = theme.get_ui_style("ui.popup", {{212, 212, 212}, {37, 37, 38}, true, true});
    auto border_col  = theme.get_color("blue", {0, 122, 204});
    auto title_col   = theme.get_color("gold", {215, 186, 125});
    auto sel_style   = theme.get_ui_style("ui.menu.selected", {{255, 255, 255}, {9, 71, 113}, true, true});
    auto text_col    = theme.get_color("text", {212, 212, 212});
    auto active_col  = theme.get_color("dark_green2", {72, 126, 2});

    ncplane_set_bg_rgb8(stdplane, popup_style.bg.r, popup_style.bg.g, popup_style.bg.b);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    ncplane_set_fg_rgb8(stdplane, border_col.r, border_col.g, border_col.b);
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

    std::string title = " Themes (F6) ";
    ncplane_set_fg_rgb8(stdplane, title_col.r, title_col.g, title_col.b);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    std::string count_str = " (" + std::to_string(theme_list.size()) + ") ";
    int cs_x = popup_x + popup_w - static_cast<int>(count_str.size()) - 2;
    if (cs_x > popup_x + static_cast<int>(title.size()) + 4) {
        ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
        ncplane_putstr_yx(stdplane, popup_y, cs_x, count_str.c_str());
    }

    int visible_rows = popup_h - 2;
    if (theme_selected_idx < theme_scroll) {
        theme_scroll = theme_selected_idx;
    }
    if (theme_selected_idx >= theme_scroll + visible_rows) {
        theme_scroll = theme_selected_idx - visible_rows + 1;
    }
    theme_scroll = std::max(0, theme_scroll);

    std::string cur_active = config.settings.theme.empty() ? "dark_plus" : config.settings.theme;

    for (int r = 0; r < visible_rows; ++r) {
        int idx = theme_scroll + r;
        int draw_y = popup_y + 1 + r;

        if (idx < static_cast<int>(theme_list.size())) {
            bool is_sel = (idx == theme_selected_idx);
            bool is_cur = (theme_list[idx] == cur_active);

            if (is_sel) {
                ncplane_set_bg_rgb8(stdplane, sel_style.bg.r, sel_style.bg.g, sel_style.bg.b);
            } else {
                ncplane_set_bg_rgb8(stdplane, popup_style.bg.r, popup_style.bg.g, popup_style.bg.b);
            }

            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            int cur_x = popup_x + 2;
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, title_col.r, title_col.g, title_col.b);
                ncplane_putstr_yx(stdplane, draw_y, cur_x, "▶ ");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, cur_x, "  ");
            }
            cur_x += 2;

            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            } else if (is_cur) {
                ncplane_set_fg_rgb8(stdplane, 130, 220, 255);
            } else {
                ncplane_set_fg_rgb8(stdplane, text_col.r, text_col.g, text_col.b);
            }
            std::string tname = theme_list[idx];
            int max_w = popup_w - 16;
            if (static_cast<int>(tname.size()) > max_w && max_w > 0) {
                tname = tname.substr(0, max_w);
            }
            ncplane_putstr_yx(stdplane, draw_y, cur_x, tname.c_str());

            if (is_cur) {
                std::string badge = "[active]";
                int bx = popup_x + popup_w - static_cast<int>(badge.size()) - 3;
                if (bx > cur_x + static_cast<int>(tname.size()) + 1) {
                    ncplane_set_fg_rgb8(stdplane, active_col.r, active_col.g, active_col.b);
                    ncplane_putstr_yx(stdplane, draw_y, bx, badge.c_str());
                }
            }
        }
    }

    std::string footer = " [▲/▼] Navigate  [Enter] Select  [Esc/F6] Close ";
    ncplane_set_fg_rgb8(stdplane, 140, 150, 160);
    ncplane_set_bg_rgb8(stdplane, popup_style.bg.r, popup_style.bg.g, popup_style.bg.b);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}

void VimEngine::render_mini_help(unsigned int screen_h, unsigned int screen_w) {
    struct Slot {
        std::string key;
        std::string label;
    };

    std::vector<Slot> row1 = {
        {"F1", "Git View"},
        {"F2", "Prev Hunk"},
        {"F3", "Next Hunk"},
        {"F4", "Hunk Popup"},
        {"F5", "Hunk Diff"},
        {"F6", "Theme"}
    };

    std::vector<Slot> row2 = {
        {"F7", "--"},
        {"F8", "--"},
        {"F9", "Settings"},
        {"F10", "--"},
        {"F11", "vg view"},
        {"F12", "Mini Help"}
    };

    int popup_h = 4;
    int popup_w = std::min(static_cast<int>(screen_w) - 2, 98);
    int popup_x = std::max(0, (static_cast<int>(screen_w) - popup_w) / 2);
    int popup_y = std::max(0, static_cast<int>(screen_h) - 2 - popup_h);

    // Background
    ncplane_set_bg_rgb8(stdplane, 24, 26, 30);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border: cyan/teal rounded box
    ncplane_set_fg_rgb8(stdplane, 100, 185, 195);
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
    std::string title = " MiniHelp ";
    ncplane_set_fg_rgb8(stdplane, 160, 220, 230);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    auto draw_slots_row = [&](int draw_y, const std::vector<Slot>& slots) {
        int avail_w = popup_w - 2;
        int col_w = avail_w / static_cast<int>(slots.size());

        for (size_t i = 0; i < slots.size(); ++i) {
            int sx = popup_x + 1 + static_cast<int>(i) * col_w + 1;
            if (sx + static_cast<int>(slots[i].key.size()) + 5 >= popup_x + popup_w) break;

            // '[' in gold
            ncplane_set_fg_rgb8(stdplane, 230, 190, 70);
            ncplane_putstr_yx(stdplane, draw_y, sx, "[");

            // 'F...' in green
            ncplane_set_fg_rgb8(stdplane, 110, 205, 120);
            ncplane_putstr_yx(stdplane, draw_y, sx + 1, slots[i].key.c_str());

            // ']' in gold
            int b_end_x = sx + 1 + static_cast<int>(slots[i].key.size());
            ncplane_set_fg_rgb8(stdplane, 230, 190, 70);
            ncplane_putstr_yx(stdplane, draw_y, b_end_x, "] ");

            // label in white/cream
            if (slots[i].label == "--") {
                ncplane_set_fg_rgb8(stdplane, 120, 125, 135);
            } else {
                ncplane_set_fg_rgb8(stdplane, 220, 220, 220);
            }
            int max_lbl_w = col_w - static_cast<int>(slots[i].key.size()) - 4;
            std::string lbl = slots[i].label;
            if (static_cast<int>(lbl.size()) > max_lbl_w && max_lbl_w > 0) {
                lbl = lbl.substr(0, max_lbl_w);
            }
            ncplane_putstr_yx(stdplane, draw_y, b_end_x + 2, lbl.c_str());
        }
    };

    draw_slots_row(popup_y + 1, row1);
    draw_slots_row(popup_y + 2, row2);
}

void VimEngine::next_buffer() {
    if (buffers.empty()) return;
    auto& win = active_win();
    save_window_position(win, active_buf());
    win.buffer_idx = (win.buffer_idx + 1) % buffers.size();
    restore_window_position(win, active_buf());
    set_info_msg("Switched to Buffer [" + std::to_string(win.buffer_idx + 1) + "/" +
                 std::to_string(buffers.size()) + "]: " + active_buf().name);
}

void VimEngine::prev_buffer() {
    if (buffers.empty()) return;
    auto& win = active_win();
    save_window_position(win, active_buf());
    win.buffer_idx = (win.buffer_idx + buffers.size() - 1) % buffers.size();
    restore_window_position(win, active_buf());
    set_info_msg("Switched to Buffer [" + std::to_string(win.buffer_idx + 1) + "/" +
                 std::to_string(buffers.size()) + "]: " + active_buf().name);
}

void VimEngine::switch_to_buffer(size_t idx) {
    if (idx >= buffers.size()) return;
    auto& win = active_win();
    save_window_position(win, active_buf());
    win.buffer_idx = idx;
    restore_window_position(win, active_buf());
    set_info_msg("Switched to Buffer [" + std::to_string(win.buffer_idx + 1) + "/" +
                 std::to_string(buffers.size()) + "]: " + active_buf().name);
}

void VimEngine::open_buffer_list() {
    show_whichkey_popup = false;
    show_git_hunk_popup = false;
    show_settings_popup = false;
    show_filepicker = false;
    show_rg_popup = false;
    show_git_status = false;
    close_cmd_completion();
    leader_pending = false;

    buffer_list_query.clear();
    filter_buffer_list();

    size_t cur_buf_idx = active_win().buffer_idx;
    buffer_list_selected_idx = 0;
    for (size_t i = 0; i < buffer_list_filtered_indices.size(); ++i) {
        if (buffer_list_filtered_indices[i] == cur_buf_idx) {
            buffer_list_selected_idx = static_cast<int>(i);
            break;
        }
    }
    buffer_list_scroll = 0;
    show_buffer_list = true;
    set_info_msg("Buffer List: Type to filter, [▲/▼] Navigate, [Enter] Switch, [Ctrl-d] Close, [Esc] Cancel");
}

void VimEngine::filter_buffer_list() {
    buffer_list_filtered_indices.clear();
    if (buffer_list_query.empty()) {
        for (size_t i = 0; i < buffers.size(); ++i) {
            buffer_list_filtered_indices.push_back(i);
        }
    } else {
        std::string q = buffer_list_query;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });

        for (size_t i = 0; i < buffers.size(); ++i) {
            std::string name_lower = buffers[i]->name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), [](unsigned char c) { return std::tolower(c); });
            std::string path_lower = buffers[i]->file_path;
            std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), [](unsigned char c) { return std::tolower(c); });

            if (name_lower.find(q) != std::string::npos || path_lower.find(q) != std::string::npos) {
                buffer_list_filtered_indices.push_back(i);
            }
        }
    }

    if (buffer_list_selected_idx >= static_cast<int>(buffer_list_filtered_indices.size())) {
        buffer_list_selected_idx = std::max(0, static_cast<int>(buffer_list_filtered_indices.size()) - 1);
    }
}

void VimEngine::render_buffer_list(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(48, static_cast<int>(screen_w * 0.65));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 4);
    int popup_h = 15;
    popup_h = std::min(popup_h, static_cast<int>(screen_h) - 4);

    int popup_x = (static_cast<int>(screen_w) - popup_w) / 2;
    int popup_y = std::max(1, (static_cast<int>(screen_h) - popup_h) / 2);

    // Draw background
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Draw rounded box
    ncplane_set_fg_rgb8(stdplane, 70, 195, 210);
    ncplane_putstr_yx(stdplane, popup_y, popup_x, "╭");
    ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - 1, "╮");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x, "╰");
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - 1, "╯");

    for (int c = 1; c < popup_w - 1; ++c) {
        ncplane_putstr_yx(stdplane, popup_y, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + c, "─");
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + c, "─");
    }
    for (int r = 1; r < popup_h - 1; ++r) {
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x, "│");
        ncplane_putstr_yx(stdplane, popup_y + r, popup_x + popup_w - 1, "│");
    }
    ncplane_putstr_yx(stdplane, popup_y + 2, popup_x, "├");
    ncplane_putstr_yx(stdplane, popup_y + 2, popup_x + popup_w - 1, "┤");

    // Title & count
    std::string title = " Buffer List (Alt-b / :ls) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    std::string count_str = " (" + std::to_string(buffer_list_filtered_indices.size()) + "/" +
                            std::to_string(buffers.size()) + ") ";
    if (popup_w - static_cast<int>(count_str.size()) - 3 > popup_x + static_cast<int>(title.size()) + 2) {
        ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - static_cast<int>(count_str.size()) - 2, count_str.c_str());
    }

    // Search query prompt
    ncplane_set_fg_rgb8(stdplane, 80, 220, 120);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "> ");
    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
    std::string q_display = buffer_list_query + "_";
    int max_q_w = popup_w - 6;
    if (static_cast<int>(q_display.size()) > max_q_w) {
        q_display = q_display.substr(q_display.size() - max_q_w);
    }
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 4, q_display.c_str());

    // Buffer list rows
    int visible_rows = popup_h - 4;
    if (buffer_list_selected_idx < buffer_list_scroll) {
        buffer_list_scroll = buffer_list_selected_idx;
    }
    if (buffer_list_selected_idx >= buffer_list_scroll + visible_rows) {
        buffer_list_scroll = buffer_list_selected_idx - visible_rows + 1;
    }
    buffer_list_scroll = std::max(0, buffer_list_scroll);

    for (int r = 0; r < visible_rows; ++r) {
        int list_idx = buffer_list_scroll + r;
        int draw_y = popup_y + 3 + r;

        if (list_idx < static_cast<int>(buffer_list_filtered_indices.size())) {
            size_t buf_idx = buffer_list_filtered_indices[list_idx];
            bool is_sel = (list_idx == buffer_list_selected_idx);
            bool is_active_win = (buf_idx == active_win().buffer_idx);
            const auto& b = *buffers[buf_idx];

            if (is_sel) {
                ncplane_set_bg_rgb8(stdplane, 45, 65, 115);
            } else {
                ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
            }

            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            // Pointer
            int cur_x = popup_x + 2;
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 205, 60);
                ncplane_putstr_yx(stdplane, draw_y, cur_x, "▶ ");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, cur_x, "  ");
            }
            cur_x += 2;

            // Buffer index
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "[%zu]%c ", buf_idx + 1, is_active_win ? '*' : ' ');
            if (is_active_win) {
                ncplane_set_fg_rgb8(stdplane, 80, 220, 120);
            } else {
                ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
            }
            ncplane_putstr_yx(stdplane, draw_y, cur_x, num_buf);
            cur_x += static_cast<int>(std::string(num_buf).size());

            // Name
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            } else {
                ncplane_set_fg_rgb8(stdplane, 215, 220, 230);
            }
            std::string disp_name = b.name;
            if (b.modified) {
                disp_name += " [+]";
            }
            int max_name_w = popup_w - 28;
            if (static_cast<int>(disp_name.size()) > max_name_w && max_name_w > 0) {
                disp_name = ".." + disp_name.substr(disp_name.size() - (max_name_w - 2));
            }
            ncplane_putstr_yx(stdplane, draw_y, cur_x, disp_name.c_str());

            // Right side info (lines & lang)
            std::string lang = (b.syntax && !b.syntax->get_language().empty()) ? b.syntax->get_language() : "text";
            std::string right_info = std::to_string(b.lines.size()) + "L | " + lang;
            int rx = popup_x + popup_w - static_cast<int>(right_info.size()) - 3;
            if (rx > cur_x + static_cast<int>(disp_name.size()) + 2) {
                ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
                ncplane_putstr_yx(stdplane, draw_y, rx, right_info.c_str());
            }
        }
    }

    // Footer actions
    std::string footer = " [▲/▼] Navigate  [Enter] Switch  [Ctrl-d] Close  [Esc] Cancel ";
    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}

void VimEngine::render_whichkey_popup(unsigned int screen_h, unsigned int screen_w) {
    struct WkItem {
        std::string key;
        std::string desc;
    };

    std::vector<WkItem> col1;
    std::vector<WkItem> col2;
    std::string title;
    uint8_t border_r = 170, border_g = 115, border_b = 250;

    if (leader_p_pending) {
        title = " Paste [Space-p] ";
        border_r = 80; border_g = 220; border_b = 120;
        col1 = {
            {"p", "Full Replace Buffer"},
            {"v", "Paste from Clipboard"}
        };
        col2 = {
            {"Esc", "Cancel"}
        };
    } else if (whichkey_mode == WhichKeyMode::WINDOW) {
        title = " Window Ops [Ctrl-w] ";
        border_r = 75; border_g = 175; border_b = 245;
        col1 = {
            {"q", "Close Window"},
            {"v", "Split Vert"},
            {"s", "Split Horiz"},
            {"w", "Next Window"},
            {"c", "Close Window"}
        };
        col2 = {
            {"o", "Close Others"},
            {"h", "Focus Prev"},
            {"l", "Focus Next"},
            {"W", "Prev Window"},
            {"Esc", "Cancel"}
        };
    } else {
        title = " Leader [Space] ";
        border_r = 170; border_g = 115; border_b = 250;
        col1 = {
            {"f", "File Picker"},
            {"g", "Grep Cursor"},
            {"y", "Copy to OS (OSC 52)"},
            {"p", "Paste Menu (pp/pv)"},
            {"w", "Save Buffer"},
            {"q", "Quit"}
        };
        col2 = {
            {"d", "Hunk Diff (F5)"},
            {"h", "Hunk Popup (F4)"},
            {"j", "Next Hunk"},
            {"k", "Prev Hunk"},
            {"l", "Gutter Settings"},
            {"u", "Undo"}
        };
    }

    int popup_w = 44;
    int popup_h = 9;

    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);
    popup_h = std::min(popup_h, static_cast<int>(screen_h) - 3);

    int popup_x = static_cast<int>(screen_w) - popup_w - 1;
    int popup_y = static_cast<int>(screen_h) - 2 - popup_h;

    popup_x = std::max(0, popup_x);
    popup_y = std::max(0, popup_y);

    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    ncplane_set_fg_rgb8(stdplane, border_r, border_g, border_b);
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

    // Header title
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    int c1_x = popup_x + 2;
    int c2_x = popup_x + (popup_w / 2) + 1;

    for (size_t i = 0; i < 6 && (i + 1) < static_cast<size_t>(popup_h - 1); ++i) {
        int draw_y = popup_y + 1 + static_cast<int>(i);

        // Column 1
        if (i < col1.size()) {
            ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c1_x, "[");
            ncplane_set_fg_rgb8(stdplane, 255, 200, 70);
            ncplane_putstr_yx(stdplane, draw_y, c1_x + 1, col1[i].key.c_str());
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c1_x + 2, "] ");
            ncplane_set_fg_rgb8(stdplane, 220, 225, 235);
            ncplane_putstr_yx(stdplane, draw_y, c1_x + 4, col1[i].desc.c_str());
        }

        // Column 2
        if (i < col2.size() && c2_x < popup_x + popup_w - 5) {
            ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c2_x, "[");
            ncplane_set_fg_rgb8(stdplane, 255, 200, 70);
            ncplane_putstr_yx(stdplane, draw_y, c2_x + 1, col2[i].key.c_str());
            ncplane_set_fg_rgb8(stdplane, 100, 115, 135);
            ncplane_putstr_yx(stdplane, draw_y, c2_x + 2, "] ");
            ncplane_set_fg_rgb8(stdplane, 220, 225, 235);
            ncplane_putstr_yx(stdplane, draw_y, c2_x + 4, col2[i].desc.c_str());
        }
    }

    // Footer hint on bottom border
    std::string footer = " [Esc] Close ";
    ncplane_set_fg_rgb8(stdplane, 140, 145, 160);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + popup_w - static_cast<int>(footer.size()) - 2, footer.c_str());
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
    std::string footer = " [r] Revert Hunk   [Esc/q] Close ";
    if (static_cast<int>(footer.size()) < popup_w - 2) {
        ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 2, footer.c_str());
    }
}

void VimEngine::render_window(Window& win, bool is_active) {
    auto& buf = *buffers[win.buffer_idx];
    update_window_scroll(win, buf);

    std::set<std::pair<int, int>> cursor_set;
    for (const auto& c : win.cursors) {
        cursor_set.insert({c.y, c.x});
    }

    Cursor primary = win.cursors.empty() ? Cursor{0, 0} : win.cursors.front();
    Mode effective_visual_mode = mode;
    if (mode == Mode::COMMAND && (visual_save_mode == Mode::VISUAL || visual_save_mode == Mode::VISUAL_BLOCK)) {
        effective_visual_mode = visual_save_mode;
    }

    int v_min_y = std::min(win.visual_anchor.y, primary.y);
    int v_max_y = std::max(win.visual_anchor.y, primary.y);
    int v_min_x = std::min(win.visual_anchor.x, primary.x);
    int v_max_x = std::max(win.visual_anchor.x, primary.x);

    int gutter_w = get_line_num_w(buf);

    for (int r = 0; r < win.h; ++r) {
        int line_idx = win.scroll_y + r;
        int draw_y = win.y + r;

        auto& theme = HelixTheme::instance();
        auto ui_bg = theme.get_ui_style("ui.background", {{212, 212, 212}, {30, 30, 30}, true, true});
        auto ui_cursorline = theme.get_ui_style("ui.cursorline.primary", {{}, {40, 40, 40}, false, true});
        auto ui_linenr = theme.get_ui_style("ui.linenr", {{133, 133, 133}, {30, 30, 30}, true, true});
        auto ui_linenr_sel = theme.get_ui_style("ui.linenr.selected", {{198, 198, 198}, {40, 40, 40}, true, true});

        if (gutter_w > 0) {
            bool is_cursor_line = (line_idx == primary.y);
            if (is_cursor_line && config.settings.highlight_current_line && is_active) {
                ncplane_set_bg_rgb8(stdplane, ui_cursorline.bg.r, ui_cursorline.bg.g, ui_cursorline.bg.b);
            } else {
                ncplane_set_bg_rgb8(stdplane, ui_bg.bg.r, ui_bg.bg.g, ui_bg.bg.b);
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

            auto plus_col = theme.get_color("dark_green2", {72, 126, 2});
            auto min_col  = theme.get_color("orange_red", {241, 76, 76});
            auto mod_col  = theme.get_color("blue2", {86, 156, 214});

            if (config.settings.show_line_numbers) {
                if (line_idx < static_cast<int>(buf.lines.size())) {
                    if (is_cursor_line && config.settings.highlight_current_line && is_active) {
                        ncplane_set_fg_rgb8(stdplane, ui_linenr_sel.fg.r, ui_linenr_sel.fg.g, ui_linenr_sel.fg.b);
                    } else if (is_active) {
                        ncplane_set_fg_rgb8(stdplane, ui_linenr.fg.r, ui_linenr.fg.g, ui_linenr.fg.b);
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
                        ncplane_set_fg_rgb8(stdplane, plus_col.r, plus_col.g, plus_col.b);
                    } else if (git_sign == '~') {
                        ncplane_set_fg_rgb8(stdplane, mod_col.r, mod_col.g, mod_col.b);
                    } else if (git_sign == '-') {
                        ncplane_set_fg_rgb8(stdplane, min_col.r, min_col.g, min_col.b);
                    } else {
                        ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                    }
                    char disp_sign = (g_hunk_marker_style == 1 && git_sign != ' ') ? '|' : git_sign;
                    char sign_buf[3] = {disp_sign, ' ', '\0'};
                    ncplane_putstr_yx(stdplane, draw_y, win.x + gutter_w - 2, sign_buf);
        } else {
            ncplane_set_bg_rgb8(stdplane, ui_bg.bg.r, ui_bg.bg.g, ui_bg.bg.b);
            ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
            std::string tilde;
            for (int sp = 0; sp < std::max(0, gutter_w - 2); ++sp) tilde += " ";
            tilde += "~ ";
            ncplane_putstr_yx(stdplane, draw_y, win.x, tilde.c_str());
        }
            } else {
                ncplane_set_bg_rgb8(stdplane, ui_bg.bg.r, ui_bg.bg.g, ui_bg.bg.b);
                if (git_sign == '+') {
                    ncplane_set_fg_rgb8(stdplane, plus_col.r, plus_col.g, plus_col.b);
                } else if (git_sign == '~') {
                    ncplane_set_fg_rgb8(stdplane, mod_col.r, mod_col.g, mod_col.b);
                } else if (git_sign == '-') {
                    ncplane_set_fg_rgb8(stdplane, min_col.r, min_col.g, min_col.b);
                } else {
                    ncplane_set_fg_rgb8(stdplane, 70, 70, 70);
                }
                char disp_sign = (g_hunk_marker_style == 1 && git_sign != ' ') ? '|' : git_sign;
                char sign_buf[3] = {disp_sign, ' ', '\0'};
                ncplane_putstr_yx(stdplane, draw_y, win.x, sign_buf);
            }
        }

        int text_avail_w = win.w - gutter_w;
        if (text_avail_w <= 0) continue;

        if (line_idx < static_cast<int>(buf.lines.size())) {
            const std::string& full_line = buf.lines[line_idx];
            // Horizontal scroll: compute byte offset for scroll_x display columns
            size_t h_byte_start = compute_byte_offset(full_line, win.scroll_x);
            int h_col_skipped = compute_display_col(full_line, static_cast<int>(h_byte_start));
            int col_adjust = win.scroll_x - h_col_skipped; // padding for wide char straddling boundary
            std::string line = (h_byte_start < full_line.size()) ? full_line.substr(h_byte_start) : "";
            std::vector<SyntaxStyle> syn_styles;
            if (buf.syntax) {
                auto full_styles = buf.syntax->get_line_styles(line_idx, full_line);
                if (h_byte_start < full_styles.size()) {
                    syn_styles.assign(full_styles.begin() + h_byte_start, full_styles.end());
                }
            }
            // Expose scroll offset for rendering code that follows
            int scroll_x_off = static_cast<int>(h_byte_start);

            std::string ghost_str;
            if (is_active && mode == Mode::INSERT && line_idx == primary.y && win.cursors.size() == 1) {
                ghost_str = AutocompleteState::instance().get_ghost_suffix();
            }
            int ghost_len = static_cast<int>(ghost_str.size());

            auto ui_cursor = theme.get_ui_style("ui.cursor", {{0, 0, 0}, {166, 166, 166}, true, true});
            auto ui_sel = theme.get_ui_style("ui.selection.primary", {{255, 255, 255}, {38, 79, 120}, true, true});

            std::string active_search = search_active ? search_input : (search_highlight_on ? search_query : "");
            int sub_start_line = -1;
            int sub_end_line = -1;

            if (mode == Mode::COMMAND && !cmd_buffer.empty()) {
                std::string cstr = cmd_buffer;
                size_t cs = 0;
                while (cs < cstr.size() && std::isspace(static_cast<unsigned char>(cstr[cs]))) cs++;
                if (cs < cstr.size() && cstr[cs] == ':') cs++;
                cstr = cstr.substr(cs);

                size_t after_cmd = std::string::npos;
                if (cstr.rfind("'<,'>s", 0) == 0) {
                    sub_start_line = (visual_range_start_y >= 0) ? visual_range_start_y : primary.y;
                    sub_end_line = (visual_range_end_y >= 0) ? visual_range_end_y : primary.y;
                    after_cmd = 6;
                } else if (cstr.rfind("%s", 0) == 0) {
                    sub_start_line = 0;
                    sub_end_line = static_cast<int>(buf.lines.size()) - 1;
                    after_cmd = 2;
                } else if (cstr.rfind("s", 0) == 0 && (cstr.size() == 1 || !std::isalpha(static_cast<unsigned char>(cstr[1])))) {
                    sub_start_line = primary.y;
                    sub_end_line = primary.y;
                    after_cmd = 1;
                }

                if (after_cmd != std::string::npos) {
                    while (after_cmd < cstr.size() && std::isspace(static_cast<unsigned char>(cstr[after_cmd]))) after_cmd++;
                    if (after_cmd < cstr.size()) {
                        char delim = cstr[after_cmd++];
                        if (!std::isalnum(static_cast<unsigned char>(delim)) && !std::isspace(static_cast<unsigned char>(delim))) {
                            std::string pat;
                            bool esc = false;
                            while (after_cmd < cstr.size()) {
                                char ch = cstr[after_cmd++];
                                if (esc) {
                                    if (ch != delim) pat += '\\';
                                    pat += ch;
                                    esc = false;
                                } else if (ch == '\\') {
                                    esc = true;
                                } else if (ch == delim) {
                                    break;
                                } else {
                                    pat += ch;
                                }
                            }
                            if (!pat.empty()) {
                                active_search = pat;
                            }
                        }
                    }
                }
            }

            std::vector<std::pair<int, int>> search_ranges;
            bool has_search = !active_search.empty();
            if (has_search && sub_start_line >= 0) {
                // Dismiss the solid blue selection block once substitute pattern is active (like Neovim)
                effective_visual_mode = Mode::NORMAL;
                int s_min = std::min(sub_start_line, sub_end_line);
                int s_max = std::max(sub_start_line, sub_end_line);
                if (line_idx < s_min || line_idx > s_max) {
                    has_search = false;
                }
            }
            if (has_search) {
                bool has_upper = false;
                for (char sc : active_search) {
                    if (std::isupper(static_cast<unsigned char>(sc))) { has_upper = true; break; }
                }
                size_t spos = 0;
                while (spos < line.size()) {
                    size_t found = std::string::npos;
                    if (has_upper) {
                        found = line.find(active_search, spos);
                    } else {
                        auto it = std::search(line.begin() + spos, line.end(),
                                              active_search.begin(), active_search.end(),
                                              [](char a, char b) {
                                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                                         std::tolower(static_cast<unsigned char>(b));
                                              });
                        if (it != line.end()) {
                            found = std::distance(line.begin(), it);
                        }
                    }
                    if (found == std::string::npos) break;
                    search_ranges.push_back({static_cast<int>(found), static_cast<int>(found + active_search.size())});
                    spos = found + std::max<size_t>(1, active_search.size());
                }
            }

            ncplane_set_bg_rgb8(stdplane, ui_bg.bg.r, ui_bg.bg.g, ui_bg.bg.b);
            for (int c = 0; c < text_avail_w; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, win.x + gutter_w + c, ' ');
            }

            size_t byte_idx = 0;
            int col_x = 0;

            while (byte_idx < line.size()) {
                unsigned char lead = static_cast<unsigned char>(line[byte_idx]);
                int char_len = Keymap::utf8_char_len(lead);
                if (byte_idx + char_len > line.size()) {
                    char_len = static_cast<int>(line.size() - byte_idx);
                }
                std::string glyph = line.substr(byte_idx, char_len);

                int char_width = 1;
                wchar_t wc = 0;
                mbstate_t mbs = {};
                if (mbrtowc(&wc, glyph.data(), glyph.size(), &mbs) > 0) {
                    int cw = wcwidth(wc);
                    if (cw > 0) char_width = cw;
                }

                int orig_byte = static_cast<int>(byte_idx) + scroll_x_off;
                bool has_cursor = cursor_set.count({line_idx, orig_byte});
                bool in_visual = false;
                if (is_active) {
                    if (effective_visual_mode == Mode::VISUAL_BLOCK) {
                        if (line_idx >= v_min_y && line_idx <= v_max_y &&
                            orig_byte >= v_min_x && orig_byte <= v_max_x) {
                            in_visual = true;
                        }
                    } else if (effective_visual_mode == Mode::VISUAL) {
                        Cursor cur_pt{line_idx, orig_byte};
                        Cursor v_start = std::min(win.visual_anchor, primary);
                        Cursor v_end = std::max(win.visual_anchor, primary);
                        if (!(cur_pt < v_start) && !(v_end < cur_pt)) {
                            in_visual = true;
                        }
                    }
                }

                bool in_search = false;
                bool is_current_search = false;
                if (has_search) {
                    for (const auto& sr : search_ranges) {
                        if (static_cast<int>(byte_idx) >= sr.first && static_cast<int>(byte_idx) < sr.second) {
                            in_search = true;
                            if (line_idx == primary.y && sr.first + scroll_x_off == primary.x) {
                                is_current_search = true;
                            }
                            break;
                        }
                    }
                }

                int screen_col = col_x + col_adjust;
                if (screen_col >= 0 && screen_col < text_avail_w) {
                    int draw_x = win.x + gutter_w + screen_col;

                    if (has_cursor) {
                        ncplane_set_fg_rgb8(stdplane, ui_cursor.fg.r, ui_cursor.fg.g, ui_cursor.fg.b);
                        ncplane_set_bg_rgb8(stdplane, ui_cursor.bg.r, ui_cursor.bg.g, ui_cursor.bg.b);
                    } else if (is_current_search) {
                        ncplane_set_fg_rgb8(stdplane, 0, 0, 0);
                        ncplane_set_bg_rgb8(stdplane, 255, 215, 60);
                    } else if (in_search) {
                        ncplane_set_fg_rgb8(stdplane, 0, 0, 0);
                        ncplane_set_bg_rgb8(stdplane, 255, 180, 50);
                    } else if (in_visual) {
                        ncplane_set_fg_rgb8(stdplane, ui_sel.fg.r, ui_sel.fg.g, ui_sel.fg.b);
                        ncplane_set_bg_rgb8(stdplane, ui_sel.bg.r, ui_sel.bg.g, ui_sel.bg.b);
                    } else {
                        if (byte_idx < syn_styles.size()) {
                            ncplane_set_fg_rgb8(stdplane, syn_styles[byte_idx].r, syn_styles[byte_idx].g, syn_styles[byte_idx].b);
                        } else {
                            ncplane_set_fg_rgb8(stdplane, ui_bg.fg.r, ui_bg.fg.g, ui_bg.fg.b);
                        }
                        ncplane_set_bg_rgb8(stdplane, ui_bg.bg.r, ui_bg.bg.g, ui_bg.bg.b);
                    }

                    ncplane_putstr_yx(stdplane, draw_y, draw_x, glyph.c_str());
                }

                byte_idx += char_len;
                col_x += char_width;
            }

            int end_screen_col = col_x + col_adjust;
            if (end_screen_col >= 0 && end_screen_col < text_avail_w) {
                int draw_x = win.x + gutter_w + end_screen_col;
                bool has_cursor = cursor_set.count({line_idx, static_cast<int>(full_line.size())});
                bool in_visual = false;
                if (is_active) {
                    if (effective_visual_mode == Mode::VISUAL_BLOCK) {
                        if (line_idx >= v_min_y && line_idx <= v_max_y &&
                            static_cast<int>(full_line.size()) >= v_min_x && static_cast<int>(full_line.size()) <= v_max_x) {
                            in_visual = true;
                        }
                    } else if (effective_visual_mode == Mode::VISUAL) {
                        Cursor cur_pt{line_idx, static_cast<int>(full_line.size())};
                        Cursor v_start = std::min(win.visual_anchor, primary);
                        Cursor v_end = std::max(win.visual_anchor, primary);
                        if (!(cur_pt < v_start) && !(v_end < cur_pt)) {
                            in_visual = true;
                        }
                    }
                }

                if (has_cursor) {
                    ncplane_set_fg_rgb8(stdplane, ui_cursor.fg.r, ui_cursor.fg.g, ui_cursor.fg.b);
                    ncplane_set_bg_rgb8(stdplane, ui_cursor.bg.r, ui_cursor.bg.g, ui_cursor.bg.b);
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                } else if (in_visual) {
                    ncplane_set_fg_rgb8(stdplane, ui_sel.fg.r, ui_sel.fg.g, ui_sel.fg.b);
                    ncplane_set_bg_rgb8(stdplane, ui_sel.bg.r, ui_sel.bg.g, ui_sel.bg.b);
                    ncplane_putstr_yx(stdplane, draw_y, draw_x, " ");
                }
            }

            if (ghost_len > 0) {
                int ghost_col = end_screen_col;
                for (int gi = 0; gi < ghost_len; ++gi) {
                    int sc = ghost_col + gi;
                    if (sc >= 0 && sc < text_avail_w) {
                        int draw_x = win.x + gutter_w + sc;
                        ncplane_set_fg_rgb8(stdplane, 130, 140, 155);
                        ncplane_set_bg_rgb8(stdplane, 24, 26, 32);
                        ncplane_on_styles(stdplane, NCSTYLE_ITALIC);
                        char ch[2] = {ghost_str[gi], '\0'};
                        ncplane_putstr_yx(stdplane, draw_y, draw_x, ch);
                        ncplane_off_styles(stdplane, NCSTYLE_ITALIC);
                    }
                }
            }
        } else {
            ncplane_set_bg_rgb8(stdplane, ui_bg.bg.r, ui_bg.bg.g, ui_bg.bg.b);
            std::string empty(text_avail_w, ' ');
            ncplane_putstr_yx(stdplane, draw_y, win.x + gutter_w, empty.c_str());
        }
    }
}

void VimEngine::render_settings_popup(unsigned int screen_h, unsigned int screen_w) {
    int popup_w = std::max(38, static_cast<int>(screen_w * 0.50));
    popup_w = std::min(popup_w, static_cast<int>(screen_w) - 2);
    int popup_h = 14;
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
    std::string title = " Editor & Gutter Settings (F9) ";
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
    std::string hunk_style_str = (g_hunk_marker_style == 1) ? "< | >" : "< ~-= >";
    std::string scrolloff_str = (config.settings.scroll_offset == 0) ? "< 0 (Off) >" :
        ("< " + std::to_string(config.settings.scroll_offset) + (config.settings.scroll_offset == 1 ? " line >" : " lines >"));

    std::vector<Item> items = {
        {"Show Line Numbers", config.settings.show_line_numbers ? "[ ON ]" : "[ OFF ]"},
        {"Line Number Style", "< " + mode_str + " >"},
        {"Hunk/Gutter Width", "< " + w_str + " >"},
        {"Highlight Active", config.settings.highlight_current_line ? "[ ON ]" : "[ OFF ]"},
        {"Hunk Gutter Style", hunk_style_str},
        {"Scroll Clamp Offset", scrolloff_str},
        {"F5 Right Syntax", config.settings.hunk_diff_right_syntax ? "[ ON ]" : "[ OFF ]"},
        {"Search Wrap", config.settings.search_wrap ? "[ ON ]" : "[ OFF ]"}
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

    auto& theme = HelixTheme::instance();
    auto status_fill = theme.get_ui_style("ui.statusline.powerline.fill", {{}, {0, 122, 204}, false, true});
    auto status_ws   = theme.get_ui_style("ui.statusline.powerline.ws", {{212, 212, 212}, {38, 79, 120}, true, true});
    auto status_br   = theme.get_ui_style("ui.statusline.powerline.branch", {{255, 255, 255}, {9, 71, 113}, true, true});
    auto status_file = theme.get_ui_style("ui.statusline.powerline.file", {{238, 238, 238}, {38, 79, 120}, true, true});
    auto status_pos  = theme.get_ui_style("ui.statusline.powerline.pos", {{255, 255, 255}, {0, 43, 80}, true, true});
    auto status_lang = theme.get_ui_style("ui.statusline.powerline.lang", {{255, 255, 255}, {9, 71, 113}, true, true});

    // Clear entire status line background
    ncplane_set_bg_rgb8(stdplane, status_fill.bg.r, status_fill.bg.g, status_fill.bg.b);
    for (unsigned int c = 0; c < screen_w; ++c) {
        ncplane_putchar_yx(stdplane, y, c, ' ');
    }

    int cur_x = 0;

    // --- Left Segment 0: [Workspace Slot or FE] ---
    std::string ws_label;
    if (workspace_active && workspace_slot >= 0 && workspace_slot < 5) {
        ws_label = " WS" + std::to_string(workspace_slot) + " ";
    } else {
        ws_label = " FE ";
    }
    ncplane_set_fg_rgb8(stdplane, status_ws.fg.r, status_ws.fg.g, status_ws.fg.b);
    ncplane_set_bg_rgb8(stdplane, status_ws.bg.r, status_ws.bg.g, status_ws.bg.b);
    ncplane_putstr_yx(stdplane, y, cur_x, ws_label.c_str());
    cur_x += static_cast<int>(ws_label.size());

    // --- Left Segment 1: [mode] ---
    std::string mode_str = " NORMAL ";
    UIStyle m_style = theme.get_ui_style("ui.statusline.powerline.normal", {{255, 255, 255}, {0, 43, 80}, true, true});
    if (search_active) {
        mode_str = " SEARCH ";
        m_style = UIStyle{{15, 15, 15}, {255, 215, 60}, true, true};
    } else {
        switch (mode) {
            case Mode::NORMAL:
                mode_str = " NORMAL ";
                m_style = theme.get_ui_style("ui.statusline.powerline.normal", {{255, 255, 255}, {0, 43, 80}, true, true});
                break;
            case Mode::INSERT:
                mode_str = " INSERT ";
                m_style = theme.get_ui_style("ui.statusline.powerline.insert", {{255, 255, 255}, {72, 126, 2}, true, true});
                break;
            case Mode::VISUAL:
                mode_str = " VISUAL ";
                m_style = theme.get_ui_style("ui.statusline.powerline.visual", {{255, 255, 255}, {197, 134, 192}, true, true});
                break;
            case Mode::VISUAL_BLOCK:
                mode_str = " V-BLOCK ";
                m_style = theme.get_ui_style("ui.statusline.powerline.visual", {{255, 255, 255}, {197, 134, 192}, true, true});
                break;
            case Mode::COMMAND:
                mode_str = " COMMAND ";
                m_style = UIStyle{{15, 15, 15}, {240, 200, 80}, true, true};
                break;
        }
    }
    ncplane_set_fg_rgb8(stdplane, m_style.fg.r, m_style.fg.g, m_style.fg.b);
    ncplane_set_bg_rgb8(stdplane, m_style.bg.r, m_style.bg.g, m_style.bg.b);
    ncplane_putstr_yx(stdplane, y, cur_x, mode_str.c_str());
    cur_x += static_cast<int>(mode_str.size());

    // --- Left Segment 2: [branch name +0 ~0 -0] ---
    if (buf.is_git_repo && cur_x < static_cast<int>(screen_w)) {
        int add_cnt = 0, mod_cnt = 0, del_cnt = 0;
        for (const auto& h : buf.get_hunks()) {
            if (h.type == HunkType::ADDED) add_cnt += h.cur_count;
            else if (h.type == HunkType::MODIFIED) mod_cnt += h.cur_count;
            else if (h.type == HunkType::DELETED) del_cnt += h.orig_count;
        }

        std::string branch_name = !buf.git_branch.empty() ? buf.git_branch : "git";
        std::string git_seg = "  " + branch_name + " +" + std::to_string(add_cnt) +
                              " ~" + std::to_string(mod_cnt) + " -" + std::to_string(del_cnt) + " ";

        ncplane_set_fg_rgb8(stdplane, status_br.fg.r, status_br.fg.g, status_br.fg.b);
        ncplane_set_bg_rgb8(stdplane, status_br.bg.r, status_br.bg.g, status_br.bg.b);
        ncplane_putstr_yx(stdplane, y, cur_x, git_seg.c_str());
        cur_x += static_cast<int>(git_seg.size()) - 2;

    }

    // --- Left Segment 3: [filename] ---
    if (cur_x < static_cast<int>(screen_w)) {
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

        std::string file_seg = " " + display_name + (buf.modified ? " [+] " : " ");
        ncplane_set_fg_rgb8(stdplane, status_file.fg.r, status_file.fg.g, status_file.fg.b);
        ncplane_set_bg_rgb8(stdplane, status_file.bg.r, status_file.bg.g, status_file.bg.b);
        ncplane_putstr_yx(stdplane, y, cur_x, file_seg.c_str());
        cur_x += static_cast<int>(file_seg.size());
    }

    // --- Right Segments: [filetype][row:col][buffer 1/2] ---
    std::string ft = (buf.syntax && !buf.syntax->get_language().empty()) ? buf.syntax->get_language() : "text";
    std::string ft_seg = " " + ft + " ";
    std::string pos_seg = " " + std::to_string(primary.y + 1) + ":" + std::to_string(primary.x + 1) + " ";
    std::string buf_seg = " " + std::to_string(win.buffer_idx + 1) + "/" + std::to_string(buffers.size()) + " ";

    int right_total_w = static_cast<int>(ft_seg.size() + pos_seg.size() + buf_seg.size());
    int rx = std::max(cur_x, static_cast<int>(screen_w) - right_total_w);

    if (rx < static_cast<int>(screen_w)) {
        // [filetype]
        ncplane_set_fg_rgb8(stdplane, status_lang.fg.r, status_lang.fg.g, status_lang.fg.b);
        ncplane_set_bg_rgb8(stdplane, status_lang.bg.r, status_lang.bg.g, status_lang.bg.b);
        ncplane_putstr_yx(stdplane, y, rx, ft_seg.c_str());
        rx += static_cast<int>(ft_seg.size());

        // [row:col]
        ncplane_set_fg_rgb8(stdplane, status_pos.fg.r, status_pos.fg.g, status_pos.fg.b);
        ncplane_set_bg_rgb8(stdplane, status_pos.bg.r, status_pos.bg.g, status_pos.bg.b);
        ncplane_putstr_yx(stdplane, y, rx, pos_seg.c_str());
        rx += static_cast<int>(pos_seg.size());

        // [buffer 1/2]
        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        ncplane_set_bg_rgb8(stdplane, status_fill.bg.r, status_fill.bg.g, status_fill.bg.b);
        ncplane_putstr_yx(stdplane, y, rx, buf_seg.c_str());
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

        int avail_w = std::max(1, static_cast<int>(screen_w) - 1);
        int view_start = 0;
        if (cmd_cursor_pos >= avail_w) {
            view_start = cmd_cursor_pos - avail_w + 1;
        }

        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        if (view_start < static_cast<int>(cmd_buffer.size())) {
            std::string disp = cmd_buffer.substr(view_start, avail_w);
            ncplane_putstr_yx(stdplane, y, 1, disp.c_str());
        }
    } else if (search_active) {
        ncplane_set_fg_rgb8(stdplane, 255, 230, 80);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);
        ncplane_putstr_yx(stdplane, y, 0, "/");

        int avail_w = std::max(1, static_cast<int>(screen_w) - 1);
        int view_start = 0;
        if (search_input_cursor >= avail_w) {
            view_start = search_input_cursor - avail_w + 1;
        }

        ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
        if (view_start < static_cast<int>(search_input.size())) {
            std::string disp = search_input.substr(view_start, avail_w);
            ncplane_putstr_yx(stdplane, y, 1, disp.c_str());
        }

        if (search_total_matches > 0) {
            std::string cnt_str = " [" + std::to_string(search_current_match_idx + 1) + "/" +
                                  std::to_string(search_total_matches) + "]";
            int rx = static_cast<int>(screen_w) - static_cast<int>(cnt_str.size()) - 1;
            if (rx > 1 + static_cast<int>(search_input.size())) {
                ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
                ncplane_putstr_yx(stdplane, y, rx, cnt_str.c_str());
            }
        } else if (!search_input.empty()) {
            std::string not_found = " [Pattern not found]";
            int rx = static_cast<int>(screen_w) - static_cast<int>(not_found.size()) - 1;
            if (rx > 1 + static_cast<int>(search_input.size())) {
                ncplane_set_fg_rgb8(stdplane, 240, 80, 80);
                ncplane_putstr_yx(stdplane, y, rx, not_found.c_str());
            }
        }
    } else {
        ncplane_set_fg_rgb8(stdplane, 240, 200, 100);
        ncplane_set_bg_rgb8(stdplane, 20, 20, 24);

        if (!info_msg.empty()) {
            bool is_pending_op = (info_msg == "d" || info_msg == "dg" || info_msg == "y" ||
                                  info_msg == "g" || info_msg == "=" || info_msg == ">" ||
                                  info_msg == "<");
            if (is_pending_op) {
                int text_len = static_cast<int>(info_msg.size());
                int rx = static_cast<int>(screen_w) - text_len - 1;
                if (rx >= 0) {
                    ncplane_putstr_yx(stdplane, y, rx, info_msg.c_str());
                }
            } else {
                std::string bar = " " + info_msg;
                if (static_cast<int>(bar.size()) >= static_cast<int>(screen_w)) {
                    bar = bar.substr(0, screen_w - 1);
                }
                ncplane_putstr_yx(stdplane, y, 0, bar.c_str());
            }
        }
    }
}
