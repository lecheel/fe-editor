#pragma once
#include "action.hpp"
#include <notcurses/notcurses.h>
#include <cstdint>
#include <cctype>
#include <string>

#ifndef NCKEY_F02
#define NCKEY_F02 (NCKEY_F01 + 1)
#endif
#ifndef NCKEY_F03
#define NCKEY_F03 (NCKEY_F01 + 2)
#endif
#ifndef NCKEY_F04
#define NCKEY_F04 (NCKEY_F01 + 3)
#endif
#ifndef NCKEY_F05
#define NCKEY_F05 (NCKEY_F01 + 4)
#endif
#ifndef NCKEY_F06
#define NCKEY_F06 (NCKEY_F01 + 5)
#endif
#ifndef NCKEY_F07
#define NCKEY_F07 (NCKEY_F01 + 6)
#endif
#ifndef NCKEY_F08
#define NCKEY_F08 (NCKEY_F01 + 7)
#endif
#ifndef NCKEY_F10
#define NCKEY_F10 (NCKEY_F01 + 9)
#endif
#ifndef NCKEY_F11
#define NCKEY_F11 (NCKEY_F01 + 10)
#endif
#ifndef NCKEY_F12
#define NCKEY_F12 (NCKEY_F01 + 11)
#endif

namespace Keymap {

inline bool is_fkey(const ncinput& ni, uint32_t key, int n) {
    if (n < 1 || n > 12) return false;
    uint32_t target = NCKEY_F01 + (n - 1);
    return key == target || ni.id == target;
}

inline bool is_alt(const ncinput& ni, uint32_t key, char ch) {
    if (!ni.alt) return false;
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return ni.id == lower || ni.id == upper || key == static_cast<uint32_t>(lower) || key == static_cast<uint32_t>(upper);
}

inline bool is_ctrl(const ncinput& ni, uint32_t key, char ch) {
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (ni.ctrl && (ni.id == lower || ni.id == upper)) return true;
    if (lower >= 'a' && lower <= 'z') {
        uint32_t ctrl_code = lower - 'a' + 1;
        if (key == ctrl_code) return true;
    }
    return false;
}

inline bool is_esc(const ncinput& ni, uint32_t key) {
    return key == NCKEY_ESC || ni.id == NCKEY_ESC || key == 27;
}

inline bool is_enter(const ncinput& ni, uint32_t key) {
    return key == NCKEY_ENTER || ni.id == NCKEY_ENTER || key == '\n' || key == '\r';
}

inline bool is_backspace(const ncinput& ni, uint32_t key) {
    return key == NCKEY_BACKSPACE || ni.id == NCKEY_BACKSPACE || key == 127 || key == '\b';
}

inline bool is_colon(const ncinput& ni, uint32_t key) {
    return key == ':' || ni.id == ':' || (ni.utf8[0] == ':' && ni.utf8[1] == '\0') ||
           (ni.shift && (key == ';' || ni.id == ';'));
}

inline char get_shifted_ascii(char ch) {
    switch (ch) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case ';': return ':';
        case '\'': return '"';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        case '`': return '~';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        default:
            if (ch >= 'a' && ch <= 'z') {
                return static_cast<char>(ch - 'a' + 'A');
            }
            return ch;
    }
}

inline std::string get_input_text(const ncinput& ni, uint32_t key) {
    if (ni.ctrl || ni.alt) return "";

    // 1. Check utf8 representation
    if (ni.utf8[0] != '\0') {
        unsigned char u0 = static_cast<unsigned char>(ni.utf8[0]);
        if (u0 >= 128) {
            return std::string(reinterpret_cast<const char*>(ni.utf8));
        }
        char c = ni.utf8[0];
        if (ni.shift) {
            c = get_shifted_ascii(c);
        }
        if (c >= 32 && c <= 126) {
            return std::string(1, c);
        }
    }

    // 2. Fallback to key or ni.id
    uint32_t raw = (key >= 32 && key < 127) ? key : ni.id;
    if (raw >= 32 && raw <= 126) {
        char c = static_cast<char>(raw);
        if (ni.shift) {
            c = get_shifted_ascii(c);
        }
        return std::string(1, c);
    }

    return "";
}

inline std::string key_to_string(const ncinput& ni, uint32_t key) {
    if (is_fkey(ni, key, 1)) return "<F1>";
    if (is_fkey(ni, key, 2)) return "<F2>";
    if (is_fkey(ni, key, 3)) return "<F3>";
    if (is_fkey(ni, key, 4)) return "<F4>";
    if (is_fkey(ni, key, 5)) return "<F5>";
    if (is_fkey(ni, key, 6)) return "<F6>";
    if (is_fkey(ni, key, 7)) return "<F7>";
    if (is_fkey(ni, key, 8)) return "<F8>";
    if (is_fkey(ni, key, 9)) return "<F9>";
    if (is_fkey(ni, key, 10)) return "<F10>";
    if (is_fkey(ni, key, 11)) return "<F11>";
    if (is_fkey(ni, key, 12)) return "<F12>";

    if (is_esc(ni, key)) return "<Esc>";
    if (is_enter(ni, key)) return "<CR>";
    if (is_backspace(ni, key)) return "<BS>";
    if (key == '\t' || key == NCKEY_TAB || ni.id == '\t' || ni.id == NCKEY_TAB) {
        if (ni.shift) return "<S-Tab>";
        return "<Tab>";
    }
    if (key == NCKEY_UP || ni.id == NCKEY_UP) return "<Up>";
    if (key == NCKEY_DOWN || ni.id == NCKEY_DOWN) return "<Down>";
    if (key == NCKEY_LEFT || ni.id == NCKEY_LEFT) return "<Left>";
    if (key == NCKEY_RIGHT || ni.id == NCKEY_RIGHT) return "<Right>";
    if (key == NCKEY_HOME || ni.id == NCKEY_HOME) return "<Home>";
    if (key == NCKEY_END || ni.id == NCKEY_END) return "<End>";
    if (key == NCKEY_PGUP || ni.id == NCKEY_PGUP) return "<PageUp>";
    if (key == NCKEY_PGDOWN || ni.id == NCKEY_PGDOWN) return "<PageDown>";
    if (key == NCKEY_DEL || ni.id == NCKEY_DEL) return "<Del>";

    if (key == ' ' && !ni.alt && !ni.ctrl) return "<Space>";

    if (ni.alt) {
        char ch = '\0';
        if (ni.id >= 32 && ni.id < 127) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ni.id)));
        else if (key >= 32 && key < 127) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
        if (ch != '\0') {
            return std::string("<A-") + ch + ">";
        }
    }

    if (ni.ctrl) {
        char ch = '\0';
        if (ni.id >= 32 && ni.id < 127) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ni.id)));
        else if (key >= 1 && key <= 26) ch = static_cast<char>('a' + key - 1);
        else if (key >= 32 && key < 127) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
        if (ch != '\0') {
            return std::string("<C-") + ch + ">";
        }
    }

    if (key >= 1 && key <= 26 && key != '\t' && key != '\n' && key != '\r' && key != 27) {
        char ch = static_cast<char>('a' + key - 1);
        return std::string("<C-") + ch + ">";
    }

    if (!ni.alt && !ni.ctrl) {
        std::string txt = get_input_text(ni, key);
        if (!txt.empty()) return txt;
    }

    return "";
}

inline std::string normalize_key_chord(const std::string& raw) {
    if (raw.empty()) return "";
    std::string s = raw;

    if (s.rfind("<leader>", 0) == 0 || s.rfind("<Leader>", 0) == 0) {
        s = "<Space>" + s.substr(8);
    }

    if (s.front() == '<' && s.back() == '>') {
        std::string inner = s.substr(1, s.size() - 2);
        std::string lower_inner = inner;
        for (char& c : lower_inner) c = std::tolower(static_cast<unsigned char>(c));

        if (lower_inner == "space") return "<Space>";
        if (lower_inner == "cr" || lower_inner == "enter" || lower_inner == "return") return "<CR>";
        if (lower_inner == "esc" || lower_inner == "escape") return "<Esc>";
        if (lower_inner == "bs" || lower_inner == "backspace") return "<BS>";
        if (lower_inner == "tab") return "<Tab>";
        if (lower_inner == "s-tab" || lower_inner == "shift-tab") return "<S-Tab>";
        if (lower_inner == "up") return "<Up>";
        if (lower_inner == "down") return "<Down>";
        if (lower_inner == "left") return "<Left>";
        if (lower_inner == "right") return "<Right>";
        if (lower_inner == "home") return "<Home>";
        if (lower_inner == "end") return "<End>";
        if (lower_inner == "pageup" || lower_inner == "pgup") return "<PageUp>";
        if (lower_inner == "pagedown" || lower_inner == "pgdown") return "<PageDown>";
        if (lower_inner == "del" || lower_inner == "delete") return "<Del>";

        if (lower_inner.size() >= 2 && lower_inner[0] == 'f' && std::isdigit(static_cast<unsigned char>(lower_inner[1]))) {
            int num = 0;
            try { num = std::stoi(lower_inner.substr(1)); } catch (...) {}
            if (num >= 1 && num <= 12) return "<F" + std::to_string(num) + ">";
        }

        if (lower_inner.rfind("c-", 0) == 0 && lower_inner.size() == 3) {
            return std::string("<C-") + lower_inner[2] + ">";
        }
        if (lower_inner.rfind("ctrl-", 0) == 0 && lower_inner.size() == 6) {
            return std::string("<C-") + lower_inner[5] + ">";
        }

        if (lower_inner.rfind("a-", 0) == 0 && lower_inner.size() == 3) {
            return std::string("<A-") + lower_inner[2] + ">";
        }
        if (lower_inner.rfind("alt-", 0) == 0 && lower_inner.size() == 5) {
            return std::string("<A-") + lower_inner[4] + ">";
        }
        if (lower_inner.rfind("m-", 0) == 0 && lower_inner.size() == 3) {
            return std::string("<A-") + lower_inner[2] + ">";
        }
    }

    if (s.size() >= 2 && (s[0] == 'F' || s[0] == 'f') && std::isdigit(static_cast<unsigned char>(s[1]))) {
        int num = 0;
        try { num = std::stoi(s.substr(1)); } catch (...) {}
        if (num >= 1 && num <= 12) return "<F" + std::to_string(num) + ">";
    }

    return s;
}

} // namespace Keymap

class VimEngine;
enum class Mode;

struct KeymapConfig {
    std::string keymap_path;
    std::unordered_map<std::string, std::string> normal_map;
    std::unordered_map<std::string, std::string> insert_map;
    std::unordered_map<std::string, std::string> visual_map;

    void load(const std::string& config_dir);
    bool execute_action(VimEngine& engine, Mode mode, const std::string& action);
    bool handle_key(VimEngine& engine, Mode mode, const ncinput& ni, uint32_t key);
    bool handle_whichkey(VimEngine& engine, const ncinput& ni, uint32_t key);
};