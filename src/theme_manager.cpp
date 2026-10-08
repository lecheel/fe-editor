#include "engine.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace Keymap;

void VimEngine::scan_themes() {
    theme_list.clear();
    std::set<std::string> found;

    std::vector<std::string> dirs = {
        "themes",
        "./themes",
        (fs::path(config.get_config_dir()) / "themes").string()
    };
    const char* home = std::getenv("HOME");
    if (home) {
        dirs.push_back(std::string(home) + "/.config/fe/themes");
        dirs.push_back(std::string(home) + "/.local/share/fe/runtime/themes");
    }
    dirs.push_back("/usr/lib/fe/runtime/themes");
    dirs.push_back("/usr/share/fe/runtime/themes");

    for (const auto& d : dirs) {
        std::error_code ec;
        if (!fs::exists(d, ec) || !fs::is_directory(d, ec)) continue;
        for (const auto& entry : fs::directory_iterator(d, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".toml") {
                found.insert(entry.path().stem().string());
            }
        }
    }

    found.insert("dark_plus");
    theme_list.assign(found.begin(), found.end());
}

void VimEngine::open_theme_popup() {
    show_filepicker = false;
    show_settings_popup = false;
    show_git_hunk_popup = false;
    show_whichkey_popup = false;
    show_buffer_list = false;
    show_rg_popup = false;
    show_hunk_diff = false;
    show_git_status = false;
    show_workspace_list = false;
    close_cmd_completion();
    leader_pending = false;

    scan_themes();
    theme_selected_idx = 0;
    std::string cur_name = config.settings.theme.empty() ? "dark_plus" : config.settings.theme;
    for (size_t i = 0; i < theme_list.size(); ++i) {
        if (theme_list[i] == cur_name) {
            theme_selected_idx = static_cast<int>(i);
            break;
        }
    }
    theme_scroll = 0;
    theme_original = cur_name;
    show_theme_popup = true;
    set_info_msg("Themes (F6 / :theme): [▲/▼] Live preview  [Enter] Keep  [Esc/F6] Revert");
}

void VimEngine::preview_theme(const std::string& name) {
    if (HelixTheme::instance().load_theme(name, config.get_config_dir())) {
        for (auto& b : buffers) {
            if (b->syntax) {
                b->syntax->update_text(b->lines);
            }
        }
        if (hunk_diff_head_syntax) {
            hunk_diff_head_syntax->update_text(hunk_diff_head_lines);
        }
    } else {
        set_info_msg("Could not load theme: " + name);
    }
}

void VimEngine::close_theme_popup() {
    // Cancel path: if theme_original is still set, the selection was not
    // confirmed, so restore the theme that was active before previewing.
    if (!theme_original.empty()) {
        std::string orig = theme_original;
        theme_original.clear();
        if (HelixTheme::instance().get_name() != orig) {
            preview_theme(orig);
        }
    }
    show_theme_popup = false;
    set_info_msg("");
}

void VimEngine::handle_theme_popup_input(const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key) || is_fkey(ni, key, 6) || key == 'q' || key == 'Q') {
        close_theme_popup();
        return;
    }

    if (theme_list.empty()) {
        close_theme_popup();
        return;
    }

    int total = static_cast<int>(theme_list.size());

    // Move selection and apply the theme immediately (live preview).
    auto select_idx = [&](int idx) {
        theme_selected_idx = idx;
        preview_theme(theme_list[theme_selected_idx]);
    };

    if (key == NCKEY_UP || key == 'k' || key == 'K' || is_ctrl(ni, key, 'p')) {
        select_idx((theme_selected_idx + total - 1) % total);
        return;
    }

    if (key == NCKEY_DOWN || key == 'j' || key == 'J' || is_ctrl(ni, key, 'n')) {
        select_idx((theme_selected_idx + 1) % total);
        return;
    }

    if (key == NCKEY_HOME || key == 'g') {
        select_idx(0);
        return;
    }

    if (key == NCKEY_END || key == 'G') {
        select_idx(total - 1);
        return;
    }

    if (key == NCKEY_PGUP) {
        select_idx(std::max(0, theme_selected_idx - 5));
        return;
    }

    if (key == NCKEY_PGDOWN) {
        select_idx(std::min(total - 1, theme_selected_idx + 5));
        return;
    }

    if (is_enter(ni, key) || key == ' ') {
        if (theme_selected_idx >= 0 && theme_selected_idx < total) {
            std::string chosen = theme_list[theme_selected_idx];
            if (HelixTheme::instance().get_name() != chosen) {
                preview_theme(chosen);
            }
            if (HelixTheme::instance().get_name() == chosen) {
                config.settings.theme = chosen;
                config.save();
                theme_original.clear(); // confirmed: do not revert on close
                close_theme_popup();
                set_info_msg("Theme set to: " + chosen);
            }
        }
        return;
    }
}