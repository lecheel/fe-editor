#include "rg_replace.hpp"

#include "engine.hpp"
#include "keymap.hpp"

#include <algorithm>
#include <fstream>
#include <map>

namespace rgreplace {

namespace {
int         g_field  = 0;   // 0 = search, 1 = replace
bool        g_input  = true;
std::string g_pattern;

bool is_tab(uint32_t k) { return k == NCKEY_TAB || k == '\t'; }
bool is_esc(uint32_t k) { return k == NCKEY_ESC || k == 27; }
bool is_del(uint32_t k) {
    return k == NCKEY_DEL || k == NCKEY_BACKSPACE || k == 127 || k == 8;
}
} // namespace

void reset()                   { g_field = 0; g_input = true; g_pattern.clear(); }
int  active_field()            { return g_field; }
bool input_active()            { return g_input; }
void set_input_active(bool on) { g_input = on; }

void toggle_field(bool shift) {
    // THE FIX: switching fields must (re)enable text input. Previously the
    // Tab branch only flipped a flag and printable keys still fell through
    // to result navigation, so nothing could be typed into the pattern.
    g_field = shift ? 0 : (g_field == 0 ? 1 : 0);
    g_input = true;
}

const std::string& pattern()            { return g_pattern; }
void set_pattern(const std::string& s)  { g_pattern = s; }

bool handle_key(const ncinput& ni, uint32_t key, std::string& search_field) {
    if (ni.evtype == NCTYPE_RELEASE) return true;      // swallow releases
    if (Keymap::is_modifier_key(key)) return true;     // swallow modifiers

    if (is_tab(key)) { toggle_field(ni.shift); return true; }

    if (is_esc(key)) {
        if (g_input && g_field == 1) {
            g_field = 0;      // Esc leaves the replace field first;
            return true;      // the next Esc closes the popup as before
        }
        return false;
    }

    if (!g_input) return false;   // Enter / arrows / j / k reach the popup

    std::string& field = (g_field == 0) ? search_field : g_pattern;

    if (is_del(key)) {
        if (!field.empty()) {
            size_t n = 1;                              // one UTF-8 code point
            while (n < field.size() &&
                   (field[field.size() - n] & 0xC0) == 0x80) n++;
            field.erase(field.size() - n);
        }
        return true;
    }

    std::string ch = Keymap::get_input_text(ni, key);
    if (ch.empty() && ni.shift) {                      // Shift fallback,
        uint32_t raw = (key >= 32 && key < 127) ? key : ni.id; // same rules
        if (raw >= 32 && raw < 127)                    // as paste bursts use
            ch = Keymap::get_input_text(
                ni, (uint32_t)Keymap::get_shifted_ascii((char)raw));
    }
    if (!ch.empty())              { field += ch;          return true; }
    if (key >= 32 && key < 127)   { field += (char)key;   return true; }
    return false;
}

std::string apply(VimEngine& eng, const std::vector<Match>& matches) {
    if (matches.empty())   return "rg replace: no matches";
    if (g_pattern.empty()) return "rg replace: empty pattern";

    std::map<std::string, std::vector<const Match*>> per_file;
    for (const Match& m : matches) per_file[m.file].push_back(&m);

    TextBuffer& buf = eng.active_buf();

    int nfiles = 0, nocc = 0;
    for (auto& kv : per_file) {
        const std::string& file = kv.first;
        auto& ms = kv.second;

        // Right-to-left so earlier replacements don't shift later spans.
        std::sort(ms.begin(), ms.end(), [](const Match* a, const Match* b) {
            if (a->line != b->line) return a->line > b->line;
            return a->col > b->col;
        });

        if (file == buf.name) {            // active buffer: edit in memory
            buf.push_undo(eng.active_win().cursors);
            for (const Match* mp : ms) {
                int y = mp->line - 1, s = mp->col - 1;
                if (y < 0 || y >= (int)buf.lines.size()) continue;
                if (s < 0 || s + mp->len > (int)buf.lines[y].size()) continue;
                buf.lines[y].replace(s, mp->len, g_pattern);
                nocc++;
            }
            buf.modified = true;
            buf.version++;
            buf.invalidate_hunks();
            if (buf.syntax) buf.syntax->update_text(buf.lines);
        } else {                           // not open here: rewrite on disk
            std::ifstream in(file);
            if (!in.is_open()) continue;
            std::vector<std::string> lines;
            std::string l;
            while (std::getline(in, l)) {
                if (!l.empty() && l.back() == '\r') l.pop_back();
                lines.push_back(l);
            }
            in.close();
            if (lines.empty()) lines.push_back("");
            for (const Match* mp : ms) {
                int y = mp->line - 1, s = mp->col - 1;
                if (y < 0 || y >= (int)lines.size()) continue;
                if (s < 0 || s + mp->len > (int)lines[y].size()) continue;
                lines[y].replace(s, mp->len, g_pattern);
                nocc++;
            }
            std::ofstream out(file, std::ios::trunc);
            if (!out.is_open()) continue;
            for (const auto& dl : lines) out << dl << "\n";
        }
        nfiles++;
    }

    return "Replaced " + std::to_string(nocc) + " occurrence(s) in " +
           std::to_string(nfiles) + " file(s)";
}

} // namespace rgreplace