#include "engine.hpp"
#include "log.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <set>
#include <map>
#include <cstdio>

#ifndef NCKEY_F02
#define NCKEY_F02 (NCKEY_F01 + 1)
#endif
#ifndef NCKEY_F03
#define NCKEY_F03 (NCKEY_F01 + 2)
#endif
#ifndef NCKEY_F04
#define NCKEY_F04 (NCKEY_F01 + 3)
#endif

namespace fs = std::filesystem;

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

void VimEngine::open_filepicker() {
    show_whichkey_popup = false;
    show_git_hunk_popup = false;
    show_settings_popup = false;
    leader_pending = false;

    filepicker_query.clear();
    filepicker_selected_idx = 0;
    filepicker_scroll = 0;
    scan_project_files();
    filter_filepicker_files();
    show_filepicker = true;
    set_info_msg("FilePicker: Type to filter, [Enter] open, [Esc] close");
}

void VimEngine::scan_project_files() {
    filepicker_all_files.clear();
    std::string root = !project_dir.empty() ? project_dir : ".";

    bool used_git = get_git_project_files(root, filepicker_all_files);

    if (!used_git) {
        std::error_code ec;
        auto iter = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
        auto end_iter = fs::recursive_directory_iterator();

        const int MAX_NON_REPO_DEPTH = 3;
        const size_t MAX_FILES = 500;
        auto start_time = std::chrono::steady_clock::now();

        for (; iter != end_iter && !ec; iter.increment(ec)) {
            if (filepicker_all_files.size() >= MAX_FILES) break;

            if ((filepicker_all_files.size() % 100) == 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 100) {
                    break;
                }
            }

            if (iter.depth() >= MAX_NON_REPO_DEPTH) {
                iter.disable_recursion_pending();
            }

            const auto& path = iter->path();
            std::string fn = path.filename().string();

            if (iter->is_symlink(ec)) {
                if (iter->is_directory(ec)) {
                    iter.disable_recursion_pending();
                }
                continue;
            }

            if (iter->is_directory(ec)) {
                if (fn.front() == '.' || fn == "node_modules" || fn == "build" ||
                    fn == "target" || fn == "bin" || fn == "obj" || fn == "dist" ||
                    fn == "vendor" || fn == "venv" || fn == ".venv" || fn == "__pycache__" ||
                    fn == "cache" || fn == ".cache" || fn == "tmp" || fn == "temp" ||
                    fn == "proc" || fn == "sys" || fn == "dev" || fn == "run" ||
                    fn == "var" || fn == "Library" || fn == "AppData") {
                    iter.disable_recursion_pending();
                }
                continue;
            }

            if (iter->is_regular_file(ec)) {
                if (fn.front() == '.') continue;
                std::string rel;
                try {
                    rel = fs::relative(path, root, ec).string();
                } catch (...) {
                    rel = fn;
                }
                if (!ec && !rel.empty()) {
                    filepicker_all_files.push_back(rel);
                }
            }
        }
    }

    std::sort(filepicker_all_files.begin(), filepicker_all_files.end());
}

void VimEngine::filter_filepicker_files() {
    filepicker_filtered_files.clear();
    if (filepicker_query.empty()) {
        filepicker_filtered_files = filepicker_all_files;
    } else {
        std::string q = filepicker_query;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });

        for (const auto& f : filepicker_all_files) {
            std::string lf = f;
            std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lf.find(q) != std::string::npos) {
                filepicker_filtered_files.push_back(f);
            }
        }
    }

    if (filepicker_selected_idx >= static_cast<int>(filepicker_filtered_files.size())) {
        filepicker_selected_idx = std::max(0, static_cast<int>(filepicker_filtered_files.size()) - 1);
    }
}

void VimEngine::handle_filepicker_input(const ncinput& ni, uint32_t key) {
    if (key == NCKEY_ESC) {
        show_filepicker = false;
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        if (!filepicker_filtered_files.empty() &&
            filepicker_selected_idx >= 0 &&
            filepicker_selected_idx < static_cast<int>(filepicker_filtered_files.size())) {

            std::string rel_path = filepicker_filtered_files[filepicker_selected_idx];
            std::string full_path = (fs::path(!project_dir.empty() ? project_dir : ".") / rel_path).lexically_normal().string();

            save_window_position(active_win(), active_buf());

            size_t found_idx = buffers.size();
            for (size_t i = 0; i < buffers.size(); ++i) {
                if (buffers[i]->file_path == full_path || buffers[i]->name == rel_path || buffers[i]->file_path == rel_path) {
                    found_idx = i;
                    break;
                }
            }

            if (found_idx == buffers.size()) {
                buffers.push_back(TextBuffer::from_file(full_path));
                found_idx = buffers.size() - 1;
            }

            active_win().buffer_idx = found_idx;
            restore_window_position(active_win(), active_buf());
            show_filepicker = false;
            set_info_msg("\"" + active_buf().name + "\" [" + std::to_string(active_buf().lines.size()) + " lines]");
        }
        return;
    }

    if (key == NCKEY_UP || (ni.ctrl && (key == 'p' || key == 'P' || key == 'k' || key == 'K'))) {
        if (filepicker_selected_idx > 0) {
            filepicker_selected_idx--;
        } else if (!filepicker_filtered_files.empty()) {
            filepicker_selected_idx = static_cast<int>(filepicker_filtered_files.size()) - 1;
        }
        return;
    }

    if (key == NCKEY_DOWN || (ni.ctrl && (key == 'n' || key == 'N' || key == 'j' || key == 'J'))) {
        if (filepicker_selected_idx + 1 < static_cast<int>(filepicker_filtered_files.size())) {
            filepicker_selected_idx++;
        } else {
            filepicker_selected_idx = 0;
        }
        return;
    }

    if (key == NCKEY_BACKSPACE || key == 127 || key == '\b') {
        if (!filepicker_query.empty()) {
            filepicker_query.pop_back();
            filter_filepicker_files();
        }
        return;
    }

    if (!ni.alt && !ni.ctrl && key >= 32 && key < 127) {
        filepicker_query += static_cast<char>(key);
        filter_filepicker_files();
        return;
    }
}

void VimEngine::render_filepicker(unsigned int screen_h, unsigned int screen_w) {
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
    ncplane_set_fg_rgb8(stdplane, 80, 180, 240);
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
    std::string title = " File Finder (Alt-e / Space-f) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    std::string count_str = " (" + std::to_string(filepicker_filtered_files.size()) + "/" +
                            std::to_string(filepicker_all_files.size()) + ") ";
    if (popup_w - static_cast<int>(count_str.size()) - 3 > popup_x + static_cast<int>(title.size()) + 2) {
        ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
        ncplane_putstr_yx(stdplane, popup_y, popup_x + popup_w - static_cast<int>(count_str.size()) - 2, count_str.c_str());
    }

    // Search query prompt
    ncplane_set_fg_rgb8(stdplane, 80, 220, 120);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "> ");
    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
    std::string q_display = filepicker_query + "_";
    int max_q_w = popup_w - 6;
    if (static_cast<int>(q_display.size()) > max_q_w) {
        q_display = q_display.substr(q_display.size() - max_q_w);
    }
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 4, q_display.c_str());

    // File list
    int visible_rows = popup_h - 4;
    if (filepicker_selected_idx < filepicker_scroll) {
        filepicker_scroll = filepicker_selected_idx;
    }
    if (filepicker_selected_idx >= filepicker_scroll + visible_rows) {
        filepicker_scroll = filepicker_selected_idx - visible_rows + 1;
    }

    for (int r = 0; r < visible_rows; ++r) {
        int idx = filepicker_scroll + r;
        int draw_y = popup_y + 3 + r;

        if (idx < static_cast<int>(filepicker_filtered_files.size())) {
            bool is_sel = (idx == filepicker_selected_idx);
            if (is_sel) {
                ncplane_set_bg_rgb8(stdplane, 45, 65, 115);
                ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            } else {
                ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
                ncplane_set_fg_rgb8(stdplane, 210, 215, 225);
            }

            // Fill row background
            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            std::string prefix = is_sel ? " ▶ " : "   ";
            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 205, 60);
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, prefix.c_str());

            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            else ncplane_set_fg_rgb8(stdplane, 210, 215, 225);

            std::string fn = filepicker_filtered_files[idx];
            int max_len = popup_w - 7;
            if (static_cast<int>(fn.size()) > max_len) {
                fn = "..." + fn.substr(fn.size() - max_len + 3);
            }
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 5, fn.c_str());
        }
    }

    // Footer
    std::string footer = " [▲/▼] Navigate  [Enter] Open  [Esc] Cancel ";
    ncplane_set_fg_rgb8(stdplane, 130, 140, 160);
    ncplane_set_bg_rgb8(stdplane, 20, 22, 28);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}

std::string VimEngine::get_word_under_cursor() {
    auto& win = active_win();
    auto& buf = active_buf();
    if (win.cursors.empty()) return "";
    Cursor c = win.cursors.front();
    if (c.y < 0 || c.y >= static_cast<int>(buf.lines.size())) return "";
    const std::string& line = buf.lines[c.y];
    if (line.empty()) return "";

    int cx = std::clamp(c.x, 0, static_cast<int>(line.size()) - 1);
    auto is_word_char = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };

    if (!is_word_char(line[cx])) {
        // Try nearby word char
        if (cx > 0 && is_word_char(line[cx - 1])) cx--;
        else if (cx + 1 < static_cast<int>(line.size()) && is_word_char(line[cx + 1])) cx++;
        else return "";
    }

    int start = cx;
    while (start > 0 && is_word_char(line[start - 1])) start--;
    int end = cx;
    while (end + 1 < static_cast<int>(line.size()) && is_word_char(line[end + 1])) end++;

    return line.substr(start, end - start + 1);
}

std::string VimEngine::get_rg_cache_path() const {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path cdir;
    if (xdg && *xdg != '\0') {
        cdir = fs::path(xdg) / "fe";
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home != '\0') {
            cdir = fs::path(home) / ".config" / "fe";
        } else {
            cdir = ".config/fe";
        }
    }
    return (cdir / "rg_search.json").string();
}

void VimEngine::save_rg_cache() {
    std::string path = get_rg_cache_path();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"query\": \"";
    for (char c : rg_query) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else out << c;
    }
    out << "\",\n";
    out << "  \"groups\": [\n";

    for (size_t gi = 0; gi < rg_groups.size(); ++gi) {
        const auto& grp = rg_groups[gi];
        out << "    {\n";
        out << "      \"file\": \"";
        for (char c : grp.file) {
            if (c == '"') out << "\\\"";
            else if (c == '\\') out << "\\\\";
            else out << c;
        }
        out << "\",\n";
        out << "      \"matches\": [\n";
        for (size_t mi = 0; mi < grp.matches.size(); ++mi) {
            const auto& m = grp.matches[mi];
            out << "        {\"line\": " << m.line << ", \"col\": " << m.col << ", \"text\": \"";
            for (char c : m.text) {
                if (c == '"') out << "\\\"";
                else if (c == '\\') out << "\\\\";
                else if (c == '\t') out << "  ";
                else if (c >= 32 && c <= 126) out << c;
            }
            out << "\"}";
            if (mi + 1 < grp.matches.size()) out << ",";
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (gi + 1 < rg_groups.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
}

void VimEngine::load_rg_cache() {
    std::string path = get_rg_cache_path();
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    rg_groups.clear();
    rg_query.clear();

    std::string cur_file;
    std::vector<RgMatch> cur_matches;

    while (std::getline(in, line)) {
        size_t q_pos = line.find("\"query\": \"");
        if (q_pos != std::string::npos) {
            size_t end_q = line.find("\"", q_pos + 10);
            if (end_q != std::string::npos) {
                rg_query = line.substr(q_pos + 10, end_q - (q_pos + 10));
            }
            continue;
        }

        size_t f_pos = line.find("\"file\": \"");
        if (f_pos != std::string::npos) {
            if (!cur_file.empty() && !cur_matches.empty()) {
                rg_groups.push_back({cur_file, cur_matches});
                cur_matches.clear();
            }
            size_t end_f = line.find("\"", f_pos + 9);
            if (end_f != std::string::npos) {
                cur_file = line.substr(f_pos + 9, end_f - (f_pos + 9));
            }
            continue;
        }

        size_t m_pos = line.find("{\"line\":");
        if (m_pos != std::string::npos) {
            int ln = 1, col = 1;
            std::string txt;

            size_t l_idx = line.find("\"line\":", m_pos);
            if (l_idx != std::string::npos) {
                try { ln = std::stoi(line.substr(l_idx + 7)); } catch (...) {}
            }
            size_t c_idx = line.find("\"col\":", m_pos);
            if (c_idx != std::string::npos) {
                try { col = std::stoi(line.substr(c_idx + 6)); } catch (...) {}
            }
            size_t t_idx = line.find("\"text\": \"", m_pos);
            if (t_idx != std::string::npos) {
                size_t end_t = line.rfind("\"}");
                if (end_t != std::string::npos && end_t > t_idx + 9) {
                    txt = line.substr(t_idx + 9, end_t - (t_idx + 9));
                }
            }
            cur_matches.push_back({cur_file, ln, col, txt});
        }
    }

    if (!cur_file.empty() && !cur_matches.empty()) {
        rg_groups.push_back({cur_file, cur_matches});
    }

    rebuild_rg_display_lines();
}

void VimEngine::run_ripgrep(const std::string& pattern) {
    if (pattern.empty()) return;

    rg_query = pattern;
    rg_groups.clear();

    std::string root = !project_dir.empty() ? project_dir : ".";

    std::string escaped_pattern;
    for (char c : pattern) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') escaped_pattern += '\\';
        escaped_pattern += c;
    }

    std::string cmd = "rg -n --column --no-heading --hidden -g '!.git' --max-count 100 \"" +
                      escaped_pattern + "\" \"" + root + "\" 2>/dev/null";

    FILE* fp = popen(cmd.c_str(), "r");
    bool ran_grep_fallback = false;
    if (!fp) ran_grep_fallback = true;

    char buf[4096];
    std::map<std::string, std::vector<RgMatch>> grouped_map;

    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

            // Format: file:line:col:text
            size_t first_colon = line.find(':');
            if (first_colon == std::string::npos) continue;
            size_t sec_colon = line.find(':', first_colon + 1);
            if (sec_colon == std::string::npos) continue;
            size_t third_colon = line.find(':', sec_colon + 1);
            if (third_colon == std::string::npos) continue;

            std::string file = line.substr(0, first_colon);
            int ln = 1, col = 1;
            try { ln = std::stoi(line.substr(first_colon + 1, sec_colon - first_colon - 1)); } catch (...) {}
            try { col = std::stoi(line.substr(sec_colon + 1, third_colon - sec_colon - 1)); } catch (...) {}
            std::string text = line.substr(third_colon + 1);

            std::error_code ec;
            std::string rel_file = fs::relative(file, root, ec).string();
            if (ec || rel_file.empty()) rel_file = file;

            grouped_map[rel_file].push_back({rel_file, ln, col, text});
        }
        int status = pclose(fp);
        if (status != 0 && grouped_map.empty()) {
            ran_grep_fallback = true;
        }
    }

    if (ran_grep_fallback) {
        std::string fb_cmd = "grep -rnI --exclude-dir=.git \"" + escaped_pattern + "\" \"" + root + "\" 2>/dev/null";
        FILE* fb_fp = popen(fb_cmd.c_str(), "r");
        if (fb_fp) {
            while (fgets(buf, sizeof(buf), fb_fp)) {
                std::string line(buf);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                size_t first_colon = line.find(':');
                if (first_colon == std::string::npos) continue;
                size_t sec_colon = line.find(':', first_colon + 1);
                if (sec_colon == std::string::npos) continue;

                std::string file = line.substr(0, first_colon);
                int ln = 1;
                try { ln = std::stoi(line.substr(first_colon + 1, sec_colon - first_colon - 1)); } catch (...) {}
                std::string text = line.substr(sec_colon + 1);

                std::error_code ec;
                std::string rel_file = fs::relative(file, root, ec).string();
                if (ec || rel_file.empty()) rel_file = file;

                grouped_map[rel_file].push_back({rel_file, ln, 1, text});
            }
            pclose(fb_fp);
        }
    }

    for (auto& pair : grouped_map) {
        rg_groups.push_back({pair.first, pair.second});
    }

    rebuild_rg_display_lines();
    save_rg_cache();

    rg_selected_match_idx = 0;
    rg_scroll = 0;
    show_rg_popup = true;

    set_info_msg("Ripgrep: \"" + rg_query + "\" (" + std::to_string(rg_flattened_matches.size()) +
                 " matches in " + std::to_string(rg_groups.size()) + " files)");
}

void VimEngine::rebuild_rg_display_lines() {
    rg_display_lines.clear();
    rg_flattened_matches.clear();

    for (const auto& grp : rg_groups) {
        rg_display_lines.push_back({true, grp.file, -1, 0, 0, ""});
        for (const auto& m : grp.matches) {
            int midx = static_cast<int>(rg_flattened_matches.size());
            rg_flattened_matches.push_back(m);
            rg_display_lines.push_back({false, m.file, midx, m.line, m.col, m.text});
        }
    }

    if (rg_selected_match_idx >= static_cast<int>(rg_flattened_matches.size())) {
        rg_selected_match_idx = std::max(0, static_cast<int>(rg_flattened_matches.size()) - 1);
    }
}

void VimEngine::open_selected_rg_match() {
    if (rg_selected_match_idx < 0 || rg_selected_match_idx >= static_cast<int>(rg_flattened_matches.size())) {
        return;
    }

    const auto& m = rg_flattened_matches[rg_selected_match_idx];
    std::string root = !project_dir.empty() ? project_dir : ".";
    std::string full_path = (fs::path(root) / m.file).lexically_normal().string();

    save_window_position(active_win(), active_buf());

    size_t found_idx = buffers.size();
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (buffers[i]->file_path == full_path || buffers[i]->name == m.file || buffers[i]->file_path == m.file) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == buffers.size()) {
        buffers.push_back(TextBuffer::from_file(full_path));
        found_idx = buffers.size() - 1;
    }

    active_win().buffer_idx = found_idx;
    auto& nb = active_buf();

    int target_y = std::clamp(m.line - 1, 0, std::max(0, static_cast<int>(nb.lines.size()) - 1));
    int target_x = 0;
    if (target_y < static_cast<int>(nb.lines.size())) {
        target_x = std::clamp(m.col - 1, 0, static_cast<int>(nb.lines[target_y].size()));
    }

    active_win().cursors = {{target_y, target_x}};
    active_win().clamp_all_cursors(nb, mode);
    update_window_scroll(active_win(), nb);

    show_rg_popup = false;
    set_info_msg("\"" + nb.name + "\" [" + std::to_string(target_y + 1) + ":" + std::to_string(target_x + 1) + "]");
}

void VimEngine::handle_rg_popup_input(const ncinput& ni, uint32_t key) {
    if (key == NCKEY_ESC || key == NCKEY_F11 || (ni.id == NCKEY_F11) || key == 'q' || key == 'Q') {
        show_rg_popup = false;
        set_info_msg("");
        return;
    }

    if (key == NCKEY_ENTER || key == '\n' || key == '\r') {
        open_selected_rg_match();
        return;
    }

    if (key == NCKEY_UP || key == 'k' || key == 'K' || (ni.ctrl && (key == 'p' || key == 'P'))) {
        if (rg_selected_match_idx > 0) {
            rg_selected_match_idx--;
        } else if (!rg_flattened_matches.empty()) {
            rg_selected_match_idx = static_cast<int>(rg_flattened_matches.size()) - 1;
        }
        return;
    }

    if (key == NCKEY_DOWN || key == 'j' || key == 'J' || (ni.ctrl && (key == 'n' || key == 'N'))) {
        if (rg_selected_match_idx + 1 < static_cast<int>(rg_flattened_matches.size())) {
            rg_selected_match_idx++;
        } else {
            rg_selected_match_idx = 0;
        }
        return;
    }

    // Next / Prev File group
    if (key == ']' || key == '}') {
        if (!rg_flattened_matches.empty()) {
            std::string cur_file = rg_flattened_matches[rg_selected_match_idx].file;
            for (size_t i = rg_selected_match_idx + 1; i < rg_flattened_matches.size(); ++i) {
                if (rg_flattened_matches[i].file != cur_file) {
                    rg_selected_match_idx = static_cast<int>(i);
                    return;
                }
            }
        }
        return;
    }
    if (key == '[' || key == '{') {
        if (!rg_flattened_matches.empty() && rg_selected_match_idx > 0) {
            std::string cur_file = rg_flattened_matches[rg_selected_match_idx].file;
            for (int i = rg_selected_match_idx - 1; i >= 0; --i) {
                if (rg_flattened_matches[i].file != cur_file) {
                    // find first match of that previous file
                    std::string prev_file = rg_flattened_matches[i].file;
                    while (i > 0 && rg_flattened_matches[i - 1].file == prev_file) i--;
                    rg_selected_match_idx = i;
                    return;
                }
            }
        }
        return;
    }

    if (key == 'r' || key == 'R') {
        run_ripgrep(rg_query);
        return;
    }
}

void VimEngine::render_rg_popup(unsigned int screen_h, unsigned int screen_w) {
    int popup_x = 0;
    int popup_y = 0;
    int popup_w = static_cast<int>(screen_w);
    int popup_h = std::max(5, static_cast<int>(screen_h) - 2); // full screen popup

    // Background fill
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    for (int r = 0; r < popup_h; ++r) {
        for (int c = 0; c < popup_w; ++c) {
            ncplane_putchar_yx(stdplane, popup_y + r, popup_x + c, ' ');
        }
    }

    // Border (roundbox)
    ncplane_set_fg_rgb8(stdplane, 100, 180, 255);
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

    // Title
    std::string title = " Ripgrep Grouped Search (:vg / F11) ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
    ncplane_putstr_yx(stdplane, popup_y, popup_x + 2, title.c_str());

    // Query & Stats line
    ncplane_set_fg_rgb8(stdplane, 240, 200, 80);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 2, "🔍 Query: ");
    ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
    ncplane_putstr_yx(stdplane, popup_y + 1, popup_x + 12, rg_query.c_str());

    char stats_buf[128];
    snprintf(stats_buf, sizeof(stats_buf), "[%zu matches in %zu files | match %d/%zu]",
             rg_flattened_matches.size(), rg_groups.size(),
             rg_flattened_matches.empty() ? 0 : (rg_selected_match_idx + 1),
             rg_flattened_matches.size());
    int stats_x = popup_x + popup_w - static_cast<int>(std::string(stats_buf).size()) - 3;
    if (stats_x > popup_x + 14 + static_cast<int>(rg_query.size())) {
        ncplane_set_fg_rgb8(stdplane, 140, 150, 175);
        ncplane_putstr_yx(stdplane, popup_y + 1, stats_x, stats_buf);
    }

    // Find display line index corresponding to selected match
    int target_display_idx = 0;
    for (size_t i = 0; i < rg_display_lines.size(); ++i) {
        if (!rg_display_lines[i].is_file_header && rg_display_lines[i].match_idx == rg_selected_match_idx) {
            target_display_idx = static_cast<int>(i);
            break;
        }
    }

    int visible_rows = popup_h - 4;
    if (target_display_idx < rg_scroll) {
        rg_scroll = target_display_idx;
    }
    if (target_display_idx >= rg_scroll + visible_rows) {
        rg_scroll = target_display_idx - visible_rows + 1;
    }
    rg_scroll = std::max(0, rg_scroll);

    // Render grouped rows
    for (int r = 0; r < visible_rows; ++r) {
        int d_idx = rg_scroll + r;
        int draw_y = popup_y + 3 + r;

        if (d_idx >= static_cast<int>(rg_display_lines.size())) {
            break;
        }

        const auto& dline = rg_display_lines[d_idx];
        bool is_sel = (!dline.is_file_header && dline.match_idx == rg_selected_match_idx);

        if (dline.is_file_header) {
            // Group Header (Filename)
            ncplane_set_bg_rgb8(stdplane, 26, 30, 42);
            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }
            ncplane_set_fg_rgb8(stdplane, 255, 140, 180);
            std::string header_text = "📁 " + dline.file;
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, header_text.c_str());
        } else {
            // Matching line item under group
            if (is_sel) {
                ncplane_set_bg_rgb8(stdplane, 40, 60, 110);
            } else {
                ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
            }

            for (int c = 1; c < popup_w - 1; ++c) {
                ncplane_putchar_yx(stdplane, draw_y, popup_x + c, ' ');
            }

            // Pointer
            if (is_sel) {
                ncplane_set_fg_rgb8(stdplane, 255, 215, 60);
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, "▶ ");
            } else {
                ncplane_putstr_yx(stdplane, draw_y, popup_x + 2, "  ");
            }

            // Line number:col
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%5d:%-3d ", dline.line, dline.col);
            if (is_sel) ncplane_set_fg_rgb8(stdplane, 100, 220, 255);
            else ncplane_set_fg_rgb8(stdplane, 80, 160, 200);
            ncplane_putstr_yx(stdplane, draw_y, popup_x + 4, num_buf);

            // Match text
            if (is_sel) ncplane_set_fg_rgb8(stdplane, 255, 255, 255);
            else ncplane_set_fg_rgb8(stdplane, 215, 220, 230);

            std::string t = dline.text;
            int text_start_x = popup_x + 15;
            int max_text_w = popup_w - 17;
            if (static_cast<int>(t.size()) > max_text_w) {
                t = t.substr(0, max_text_w);
            }
            ncplane_putstr_yx(stdplane, draw_y, text_start_x, t.c_str());
        }
    }

    // Footer actions
    std::string footer = " [j/k/▲/▼] Match  [{/}] File Group  [Enter] Open  [r] Rescan  [F11/Esc/q] Close ";
    ncplane_set_fg_rgb8(stdplane, 255, 215, 80);
    ncplane_set_bg_rgb8(stdplane, 18, 20, 26);
    ncplane_putstr_yx(stdplane, popup_y + popup_h - 1, popup_x + 3, footer.c_str());
}