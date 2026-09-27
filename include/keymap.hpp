#pragma once
#include <notcurses/notcurses.h>
#include <cstdint>
#include <cctype>

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

} // namespace Keymap