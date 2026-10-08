#include "engine.hpp"
#include "clipboard_os.hpp"
#include "keymap.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Keymap;

void VimEngine::copy_to_system_clipboard(const std::string& text) {
    if (text.empty()) return;
    // 1. Broadcast via OSC 52
    osc52_copy(text);

    // 2. Local OS tool mirror
#ifdef __APPLE__
    FILE* fp = popen("pbcopy 2>/dev/null", "w");
    if (fp) {
        fwrite(text.data(), 1, text.size(), fp);
        pclose(fp);
    }
#elif defined(_WIN32)
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
            if (hMem) {
                wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
                if (pMem) {
                    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pMem, wlen);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                } else {
                    GlobalFree(hMem);
                }
            }
        }
        CloseClipboard();
    }
#elif !defined(_WIN32)
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* cmd1 = (wayland && *wayland != '\0') ? "wl-copy 2>/dev/null" : "xclip -selection clipboard 2>/dev/null";
    const char* cmd2 = (wayland && *wayland != '\0') ? "xclip -selection clipboard 2>/dev/null" : "xsel --clipboard --input 2>/dev/null";
    FILE* fp = popen(cmd1, "w");
    bool ok = false;
    if (fp) {
        fwrite(text.data(), 1, text.size(), fp);
        ok = (pclose(fp) == 0);
    }
    if (!ok) {
        FILE* fp2 = popen(cmd2, "w");
        if (fp2) {
            fwrite(text.data(), 1, text.size(), fp2);
            pclose(fp2);
        }
    }
#endif
}

std::string VimEngine::get_system_clipboard() {
    // 1. Try local desktop clipboard tools first (bypasses Kitty terminal prompts)
#ifdef __APPLE__
    FILE* fp = popen("pbpaste 2>/dev/null", "r");
    if (fp) {
        std::string out;
        char buf[4096];
        while (fgets(buf, sizeof(buf), fp)) {
            out += buf;
        }
        int status = pclose(fp);
        if (status == 0 && !out.empty()) {
            return out;
        }
    }
#elif defined(_WIN32)
    if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
            if (pszText) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, nullptr, 0, nullptr, nullptr);
                std::string res;
                if (len > 0) {
                    res.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pszText, -1, &res[0], len, nullptr, nullptr);
                }
                GlobalUnlock(hData);
                CloseClipboard();
                return res;
            }
        }
        CloseClipboard();
    }
#else
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    std::vector<std::string> cmds;
    if (wayland && *wayland != '\0') {
        cmds.push_back("wl-paste --no-newline 2>/dev/null");
        cmds.push_back("xclip -selection clipboard -o 2>/dev/null");
        cmds.push_back("xsel --clipboard --output 2>/dev/null");
    } else {
        cmds.push_back("xclip -selection clipboard -o 2>/dev/null");
        cmds.push_back("xsel --clipboard --output 2>/dev/null");
        cmds.push_back("wl-paste --no-newline 2>/dev/null");
    }

    for (const auto& cmd : cmds) {
        FILE* pfp = popen(cmd.c_str(), "r");
        if (pfp) {
            std::string out;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pfp)) {
                out += buf;
            }
            int status = pclose(pfp);
            if (status == 0 && !out.empty()) {
                return out;
            }
        }
    }
#endif

    // 2. Over SSH / remote sessions, query terminal host via OSC 52
    const char* ssh = std::getenv("SSH_CLIENT");
    const char* ssh_tty = std::getenv("SSH_TTY");
    if (ssh || ssh_tty) {
        std::string osc_clip = read_osc52_clipboard();
        if (!osc_clip.empty()) {
            return osc_clip;
        }
    }

    // 3. Fallback to internal yank register
    if (!yank_reg.text.empty()) {
        return yank_reg.text;
    }
    return "";
}

bool VimEngine::handle_bracketed_paste_fast() {
    bool popup_active = show_git_status ||
                        show_hunk_diff ||
                        show_theme_popup ||
                        show_mini_help ||
                        show_buffer_list ||
                        show_settings_popup ||
                        show_rg_popup ||
                        show_filepicker ||
                        show_git_hunk_popup ||
                        show_workspace_list ||
                        show_whichkey_popup ||
                        leader_pending ||
                        ctrl_w_pending ||
                        leader_p_pending ||
                        show_cmd_completion;
    if (popup_active) {
        return false;
    }

    // Check if \033 is followed by [ 2 0 0 ~ in the Notcurses queue.
    // A zero timeout here was racy: when the terminal delivered the paste in
    // chunks, the header check failed and the pasted text was then processed
    // key by key (auto-indent on every Enter and a render per key, which is
    // very slow). Wait briefly for each byte of the escape sequence instead.
    struct timespec poll_zero = {0, 50000000L}; // 50ms per escape-sequence byte
    ncinput n2, n3, n4, n5, n6;
    uint32_t k2 = notcurses_get(nc, &poll_zero, &n2);
    if (k2 == 0 || k2 == (uint32_t)-1) return false;
    if (k2 != '[' && n2.id != '[') return false;

    uint32_t k3 = notcurses_get(nc, &poll_zero, &n3);
    if (k3 != '2' && n3.id != '2') return false;

    uint32_t k4 = notcurses_get(nc, &poll_zero, &n4);
    if (k4 != '0' && n4.id != '0') return false;

    uint32_t k5 = notcurses_get(nc, &poll_zero, &n5);
    if (k5 != '0' && n5.id != '0') return false;

    uint32_t k6 = notcurses_get(nc, &poll_zero, &n6);
    if (k6 != '~' && n6.id != '~') return false;

    // Flush/drain all characters until the end marker \033[201~ without rendering!
    std::string stream_text;
    bool found_end = false;

    while (!found_end && running) {
        // Large pastes over slow terminals or SSH can stall between chunks.
        // A short timeout ended the paste early and the rest was replayed as
        // typed input, which caused the indent mess and the slowdown.
        struct timespec wait_ts = {1, 0}; // 1s max between chunks
        ncinput pi;
        uint32_t pk = notcurses_get(nc, &wait_ts, &pi);
        if (pk == 0 || pk == (uint32_t)-1) break;

        if (pk == NCKEY_ESC || pi.id == NCKEY_ESC || pk == 27) {
            ncinput e2, e3, e4, e5, e6;
            uint32_t ek2 = notcurses_get(nc, &poll_zero, &e2);
            if (ek2 == '[' || e2.id == '[') {
                uint32_t ek3 = notcurses_get(nc, &poll_zero, &e3);
                if (ek3 == '2' || e3.id == '2') {
                    uint32_t ek4 = notcurses_get(nc, &poll_zero, &e4);
                    if (ek4 == '0' || e4.id == '0') {
                        uint32_t ek5 = notcurses_get(nc, &poll_zero, &e5);
                        if (ek5 == '1' || e5.id == '1') {
                            uint32_t ek6 = notcurses_get(nc, &poll_zero, &e6);
                            if (ek6 == '~' || e6.id == '~') {
                                found_end = true;
                                break;
                            }
                        }
                    }
                }
            }
            stream_text += '\x1b';
            continue;
        }

        if (pk == '\n' || pk == '\r' || pi.id == '\n' || pi.id == '\r' || pk == NCKEY_ENTER) {
            stream_text += '\n';
            continue;
        }
        if (pk == '\t' || pi.id == '\t' || pk == NCKEY_TAB) {
            stream_text += '\t';
            continue;
        }

        std::string ch = Keymap::get_input_text(pi, pk);
        if (!ch.empty()) {
            stream_text += ch;
        } else if (pk >= 32 && pk < 127) {
            stream_text += static_cast<char>(pk);
        }
    }

    // If stream reading captured the text, insert it at once!
    // Otherwise fallback to system clipboard in one shot.
    if (!stream_text.empty()) {
        paste_text_raw(stream_text);
    } else {
        paste_from_clipboard(true);
    }

    return true;
}

bool VimEngine::handle_paste_burst(const ncinput& first_ni, uint32_t first_key) {
    if (mode != Mode::INSERT) return false;
    bool popup_active = show_git_status ||
                        show_hunk_diff ||
                        show_theme_popup ||
                        show_mini_help ||
                        show_buffer_list ||
                        show_settings_popup ||
                        show_rg_popup ||
                        show_filepicker ||
                        show_git_hunk_popup ||
                        show_workspace_list ||
                        show_whichkey_popup ||
                        leader_pending ||
                        ctrl_w_pending ||
                        leader_p_pending ||
                        show_cmd_completion;
    if (popup_active) return false;

    // Appends the key's text to `out` and returns true if it is plain text input.
    auto text_of = [](const ncinput& i, uint32_t k, std::string& out) -> bool {
        if (i.ctrl || i.alt) return false;
        if (k == NCKEY_ENTER || k == '\n' || k == '\r') { out += '\n'; return true; }
        if (k == '\t' || k == NCKEY_TAB) { out += '\t'; return true; }
        if (k == NCKEY_ESC || k == 27) return false;
        if (i.shift) {
            uint32_t raw = (k >= 32 && k < 127) ? k : i.id;
            if (raw >= 32 && raw < 127) {
                k = static_cast<uint32_t>(Keymap::get_shifted_ascii(static_cast<char>(raw)));
            }
        }
        if (nckey_synthesized_p(k)) return false;
        std::string ch = Keymap::get_input_text(i, k);
        if (ch.empty()) return false;
        out += ch;
        return true;
    };

    std::string text;
    if (!text_of(first_ni, first_key, text)) return false;

    struct Pending { ncinput ni; uint32_t key; };
    std::vector<Pending> batch;
    batch.push_back({first_ni, first_key});

    bool have_leftover = false;
    Pending leftover{};
    bool bursting = false;

    while (true) {
        // Short wait for the first follow-up key (keeps normal typing snappy),
        // longer wait once a burst has started (slow terminals / SSH chunks).
        struct timespec wait_ts = {0, bursting ? 30000000L : 2000000L};
        ncinput ni2{};
        uint32_t k2 = notcurses_get(nc, &wait_ts, &ni2);
        if (k2 == 0 || k2 == (uint32_t)-1) break;
        if (ni2.evtype == NCTYPE_RELEASE) continue;
        if (Keymap::is_modifier_key(k2)) continue;

        if (text_of(ni2, k2, text)) {
            batch.push_back({ni2, k2});
            bursting = true;
        } else {
            leftover = {ni2, k2};
            have_leftover = true;
            break;
        }
    }

    if (batch.size() >= 3) {
        // Real paste: raw insert, no per-line auto-indent, single syntax update.
        paste_text_raw(text);
    } else {
        // Ordinary typing: replay keys normally.
        for (const auto& p : batch) {
            handle_key_input(p.ni, p.key);
        }
    }

    if (have_leftover) {
        handle_key_input(leftover.ni, leftover.key);
    }
    return true;
}

void VimEngine::copy_selection_to_clipboard() {
    auto& win = active_win();
    auto& buf = active_buf();

    if (mode == Mode::VISUAL_BLOCK) {
        Cursor primary = win.cursors.front();
        yank_reg.lines.clear();
        yank_reg.text.clear();
        yank_reg.is_linewise = false;
        int min_y = std::min(win.visual_anchor.y, primary.y);
        int max_y = std::max(win.visual_anchor.y, primary.y);
        int min_x = std::min(win.visual_anchor.x, primary.x);
        int max_x = std::max(win.visual_anchor.x, primary.x);
        for (int y = min_y; y <= max_y && y < static_cast<int>(buf.lines.size()); ++y) {
            std::string& l = buf.lines[y];
            if (min_x < static_cast<int>(l.size())) {
                int count = std::min(max_x - min_x + 1, static_cast<int>(l.size()) - min_x);
                std::string part = l.substr(min_x, count);
                yank_reg.lines.push_back(part);
                yank_reg.text += part + "\n";
            } else {
                yank_reg.lines.push_back("");
                yank_reg.text += "\n";
            }
        }
        copy_to_system_clipboard(yank_reg.text);
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
        set_info_msg("Block copied to system clipboard via OSC 52 (" + std::to_string(yank_reg.text.size()) + " chars) [<Space>y].");
        return;
    } else if (mode == Mode::VISUAL) {
        Cursor primary = win.cursors.front();
        yank_reg.lines.clear();
        yank_reg.text.clear();
        yank_reg.is_linewise = false;
        Cursor start = std::min(win.visual_anchor, primary);
        Cursor end = std::max(win.visual_anchor, primary);
        if (start.y == end.y) {
            if (start.y < static_cast<int>(buf.lines.size())) {
                std::string& l = buf.lines[start.y];
                int count = std::min(end.x - start.x + 1, static_cast<int>(l.size()) - start.x);
                if (count > 0 && start.x < static_cast<int>(l.size())) {
                    yank_reg.text = l.substr(start.x, count);
                    yank_reg.lines = {yank_reg.text};
                }
            }
        } else {
            for (int y = start.y; y <= end.y && y < static_cast<int>(buf.lines.size()); ++y) {
                std::string& l = buf.lines[y];
                if (y == start.y) {
                    std::string part = (start.x < static_cast<int>(l.size())) ? l.substr(start.x) : "";
                    yank_reg.lines.push_back(part);
                    yank_reg.text += part + "\n";
                } else if (y == end.y) {
                    int count = std::min(end.x + 1, static_cast<int>(l.size()));
                    std::string part = l.substr(0, count);
                    yank_reg.lines.push_back(part);
                    yank_reg.text += part;
                } else {
                    yank_reg.lines.push_back(l);
                    yank_reg.text += l + "\n";
                }
            }
        }
        copy_to_system_clipboard(yank_reg.text);
        mode = Mode::NORMAL;
        win.clamp_all_cursors(buf, mode);
        set_info_msg("Selection copied to system clipboard via OSC 52 (" + std::to_string(yank_reg.text.size()) + " chars) [<Space>y].");
        return;
    }

    // Normal mode: copy current line
    Cursor primary = win.cursors.front();
    if (primary.y >= 0 && primary.y < static_cast<int>(buf.lines.size())) {
        std::string line = buf.lines[primary.y];
        yank_reg.is_linewise = true;
        yank_reg.lines = {line};
        yank_reg.text = line + "\n";
        copy_to_system_clipboard(yank_reg.text);
        set_info_msg("Line copied to system clipboard via OSC 52 [<Space>y].");
    } else if (!yank_reg.text.empty()) {
        copy_to_system_clipboard(yank_reg.text);
        set_info_msg("Copied internal buffer to system clipboard via OSC 52 [<Space>y].");
    } else {
        set_info_msg("Nothing to copy.");
    }
}

void VimEngine::paste_full_replace() {
    std::string clip = get_system_clipboard();
    if (clip.empty()) {
        set_info_msg("Clipboard is empty.");
        return;
    }

    auto& win = active_win();
    auto& buf = active_buf();

    buf.push_undo(win.cursors);

    std::vector<std::string> new_lines;
    std::string cur;
    for (size_t i = 0; i < clip.size(); ++i) {
        if (clip[i] == '\r') {
            if (i + 1 < clip.size() && clip[i + 1] == '\n') continue;
            new_lines.push_back(cur);
            cur.clear();
        } else if (clip[i] == '\n') {
            new_lines.push_back(cur);
            cur.clear();
        } else {
            cur += clip[i];
        }
    }
    if (!cur.empty() || new_lines.empty()) {
        new_lines.push_back(cur);
    }

    buf.lines = std::move(new_lines);
    buf.modified = true;
    buf.version++;
    buf.invalidate_hunks();
    if (buf.syntax) buf.syntax->update_text(buf.lines);

    // Every window showing this buffer must be reset, otherwise split windows
    // keep cursors/scroll positions from the old (shorter or longer) content.
    for (auto& w : windows) {
        if (w.buffer_idx == active_win().buffer_idx) {
            w.cursors = {{0, 0}};
            w.scroll_y = 0;
            w.scroll_x = 0;
            w.clamp_all_cursors(buf, mode);
        }
    }
    update_window_scroll(win, buf);

    yank_reg.text = clip;
    yank_reg.lines = buf.lines;
    yank_reg.is_linewise = true;

    set_info_msg("Buffer replaced from clipboard (" + std::to_string(buf.lines.size()) + " lines) [<Space>pp].");
}

void VimEngine::paste_text_raw(const std::string& text) {
    if (text.empty()) return;

    if (mode == Mode::COMMAND) {
        std::string flat;
        for (char c : text) {
            if (c == '\r' || c == '\n') break;
            flat += c;
        }
        cmd_buffer.insert(cmd_cursor_pos, flat);
        cmd_cursor_pos += static_cast<int>(flat.size());
        return;
    }

    auto& win = active_win();
    auto& buf = active_buf();

    buf.push_undo(win.cursors);

    std::vector<std::string> clip_lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            clip_lines.push_back(cur);
            cur.clear();
        } else if (text[i] == '\n') {
            clip_lines.push_back(cur);
            cur.clear();
        } else {
            cur += text[i];
        }
    }
    if (!cur.empty() || clip_lines.empty()) {
        clip_lines.push_back(cur);
    }

    yank_reg.text = text;
    yank_reg.lines = clip_lines;
    yank_reg.is_linewise = (!text.empty() && text.back() == '\n');

    Mode prev_mode = mode;

    if (mode == Mode::VISUAL_BLOCK) {
        Cursor primary = win.cursors.front();
        int min_y = std::min(win.visual_anchor.y, primary.y);
        int max_y = std::max(win.visual_anchor.y, primary.y);
        int min_x = std::min(win.visual_anchor.x, primary.x);
        int max_x = std::max(win.visual_anchor.x, primary.x);

        for (int y = min_y; y <= max_y && y < static_cast<int>(buf.lines.size()); ++y) {
            std::string& l = buf.lines[y];
            if (min_x < static_cast<int>(l.size())) {
                int count = std::min(max_x - min_x + 1, static_cast<int>(l.size()) - min_x);
                l.erase(min_x, count);
            }
        }
        win.cursors = {{min_y, min_x}};
        mode = Mode::NORMAL;
    } else if (mode == Mode::VISUAL) {
        Cursor primary = win.cursors.front();
        Cursor start = std::min(win.visual_anchor, primary);
        Cursor end = std::max(win.visual_anchor, primary);
        if (start.y == end.y) {
            if (start.y < static_cast<int>(buf.lines.size())) {
                int count = std::min(end.x - start.x + 1, static_cast<int>(buf.lines[start.y].size()) - start.x);
                buf.lines[start.y].erase(start.x, count);
            }
        } else {
            if (start.y < static_cast<int>(buf.lines.size())) {
                buf.lines[start.y].erase(start.x);
                std::string rest = (end.y < static_cast<int>(buf.lines.size()) &&
                                    end.x + 1 < static_cast<int>(buf.lines[end.y].size())) ?
                    buf.lines[end.y].substr(end.x + 1) : "";
                buf.lines[start.y] += rest;
                int del_count = end.y - start.y;
                int avail = static_cast<int>(buf.lines.size()) - (start.y + 1);
                int actual_del = std::min(del_count, avail);
                if (actual_del > 0) {
                    buf.lines.erase(buf.lines.begin() + start.y + 1, buf.lines.begin() + start.y + 1 + actual_del);
                }
            }
        }
        win.cursors = {start};
        mode = Mode::NORMAL;
    }

    if (buf.lines.empty()) {
        buf.lines.push_back("");
    }

    if (win.cursors.empty()) win.cursors = {{0, 0}};

    if (clip_lines.size() == 1) {
        const std::string& line_content = clip_lines[0];
        for (auto& c : win.cursors) {
            c.y = std::clamp(c.y, 0, static_cast<int>(buf.lines.size()) - 1);
            std::string& line = buf.lines[c.y];
            int ins_pos = std::clamp(c.x, 0, static_cast<int>(line.size()));
            line.insert(ins_pos, line_content);
            c.x = ins_pos + static_cast<int>(line_content.size());
        }
    } else {
        Cursor& c = win.cursors.front();
        c.y = std::clamp(c.y, 0, static_cast<int>(buf.lines.size()) - 1);
        std::string cur_line = buf.lines[c.y];
        int ins_pos = std::clamp(c.x, 0, static_cast<int>(cur_line.size()));

        std::string before = cur_line.substr(0, ins_pos);
        std::string after = cur_line.substr(ins_pos);

        buf.lines[c.y] = before + clip_lines[0];

        int insert_y = c.y + 1;
        for (size_t i = 1; i + 1 < clip_lines.size(); ++i) {
            buf.lines.insert(buf.lines.begin() + insert_y, clip_lines[i]);
            insert_y++;
        }

        std::string last_line = clip_lines.back() + after;
        buf.lines.insert(buf.lines.begin() + insert_y, last_line);

        int new_cy = insert_y;
        int new_cx = static_cast<int>(clip_lines.back().size());
        win.cursors = {{new_cy, new_cx}};
    }

    buf.modified = true;
    buf.version++;
    buf.invalidate_hunks();
    if (buf.syntax) buf.syntax->update_text(buf.lines);

    if (prev_mode == Mode::INSERT) {
        mode = Mode::INSERT;
    }

    win.clamp_all_cursors(buf, mode);
    win.deduplicate_cursors();
    update_window_scroll(win, buf);

    set_info_msg("Pasted " + std::to_string(clip_lines.size()) +
                 (clip_lines.size() == 1 ? " line" : " lines") + ".");
}

void VimEngine::paste_from_clipboard(bool bracket_paste) {
    (void)bracket_paste;
    std::string clip = get_system_clipboard();
    if (clip.empty()) {
        set_info_msg("Clipboard is empty.");
        return;
    }
    paste_text_raw(clip);
}