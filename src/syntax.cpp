#include "syntax.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define RTLD_LAZY 0
inline void* dlopen(const char* filename, int) {
    return reinterpret_cast<void*>(LoadLibraryA(filename));
}
inline void* dlsym(void* handle, const char* symbol) {
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol));
}
inline int dlclose(void* handle) {
    return FreeLibrary(reinterpret_cast<HMODULE>(handle)) ? 0 : -1;
}
#else
#include <dlfcn.h>
#endif
#include <algorithm>

namespace fs = std::filesystem;

// Tree-sitter C ABI declarations (loaded dynamically via dlopen)
extern "C" {
    typedef struct TSLanguage TSLanguage;
    typedef struct TSParser TSParser;
    typedef struct TSTree TSTree;
    typedef struct TSQuery TSQuery;
    typedef struct TSQueryCursor TSQueryCursor;

    typedef struct {
        uint32_t row;
        uint32_t column;
    } TSPoint;

    typedef struct {
        uint32_t context[4];
        uint32_t id;
        const void* tree;
    } TSNode;

    typedef struct {
        TSNode node;
        uint32_t index;
    } TSQueryCapture;

    typedef struct {
        uint32_t id;
        uint16_t pattern_index;
        uint16_t capture_count;
        const TSQueryCapture* captures;
    } TSQueryMatch;

    typedef enum {
        TSQueryErrorNone = 0,
        TSQueryErrorSyntax,
        TSQueryErrorNodeType,
        TSQueryErrorField,
        TSQueryErrorCapture,
        TSQueryErrorStructure,
        TSQueryErrorLanguage
    } TSQueryError;

    typedef TSParser* (*fn_ts_parser_new)(void);
    typedef void (*fn_ts_parser_delete)(TSParser*);
    typedef bool (*fn_ts_parser_set_language)(TSParser*, const TSLanguage*);
    typedef TSTree* (*fn_ts_parser_parse_string)(TSParser*, const TSTree*, const char*, uint32_t);
    typedef void (*fn_ts_tree_delete)(TSTree*);
    typedef TSNode (*fn_ts_tree_root_node)(const TSTree*);
    typedef TSQuery* (*fn_ts_query_new)(const TSLanguage*, const char*, uint32_t, uint32_t*, TSQueryError*);
    typedef void (*fn_ts_query_delete)(TSQuery*);
    typedef const char* (*fn_ts_query_capture_name_for_id)(const TSQuery*, uint32_t, uint32_t*);
    typedef TSQueryCursor* (*fn_ts_query_cursor_new)(void);
    typedef void (*fn_ts_query_cursor_delete)(TSQueryCursor*);
    typedef void (*fn_ts_query_cursor_exec)(TSQueryCursor*, const TSQuery*, TSNode);
    typedef bool (*fn_ts_query_cursor_next_match)(TSQueryCursor*, TSQueryMatch*);
    typedef TSPoint (*fn_ts_node_start_point)(TSNode);
    typedef TSPoint (*fn_ts_node_end_point)(TSNode);
}

namespace {

struct TsLib {
    void* handle{nullptr};
    fn_ts_parser_new ts_parser_new{nullptr};
    fn_ts_parser_delete ts_parser_delete{nullptr};
    fn_ts_parser_set_language ts_parser_set_language{nullptr};
    fn_ts_parser_parse_string ts_parser_parse_string{nullptr};
    fn_ts_tree_delete ts_tree_delete{nullptr};
    fn_ts_tree_root_node ts_tree_root_node{nullptr};
    fn_ts_query_new ts_query_new{nullptr};
    fn_ts_query_delete ts_query_delete{nullptr};
    fn_ts_query_capture_name_for_id ts_query_capture_name_for_id{nullptr};
    fn_ts_query_cursor_new ts_query_cursor_new{nullptr};
    fn_ts_query_cursor_delete ts_query_cursor_delete{nullptr};
    fn_ts_query_cursor_exec ts_query_cursor_exec{nullptr};
    fn_ts_query_cursor_next_match ts_query_cursor_next_match{nullptr};
    fn_ts_node_start_point ts_node_start_point{nullptr};
    fn_ts_node_end_point ts_node_end_point{nullptr};

    bool loaded{false};

    void init() {
        if (loaded) return;
        const char* lib_names[] = {
#ifdef _WIN32
            "tree-sitter.dll",
            "libtree-sitter.dll",
            "libtree-sitter-0.dll",
#endif
            "libtree-sitter.so.0",
            "libtree-sitter.so",
            "/usr/lib/libtree-sitter.so.0",
            "/usr/lib/libtree-sitter.so",
            "/usr/local/lib/libtree-sitter.so",
            nullptr
        };

        for (int i = 0; lib_names[i]; ++i) {
            handle = dlopen(lib_names[i], RTLD_LAZY);
            if (handle) break;
        }

        if (!handle) return;

        #define LOAD_SYM(name) name = (fn_##name)dlsym(handle, #name); if (!name) { dlclose(handle); handle = nullptr; return; }
        LOAD_SYM(ts_parser_new);
        LOAD_SYM(ts_parser_delete);
        LOAD_SYM(ts_parser_set_language);
        LOAD_SYM(ts_parser_parse_string);
        LOAD_SYM(ts_tree_delete);
        LOAD_SYM(ts_tree_root_node);
        LOAD_SYM(ts_query_new);
        LOAD_SYM(ts_query_delete);
        LOAD_SYM(ts_query_capture_name_for_id);
        LOAD_SYM(ts_query_cursor_new);
        LOAD_SYM(ts_query_cursor_delete);
        LOAD_SYM(ts_query_cursor_exec);
        LOAD_SYM(ts_query_cursor_next_match);
        LOAD_SYM(ts_node_start_point);
        LOAD_SYM(ts_node_end_point);
        #undef LOAD_SYM

        loaded = true;
    }
};

TsLib& get_ts() {
    static TsLib lib;
    lib.init();
    return lib;
}

} // namespace

std::string detect_lang(const std::string& path) {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::string fn = p.filename().string();

    if (ext == ".c" || ext == ".h") return "c";
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".hxx") return "c";
    if (ext == ".rs") return "rust";
    if (ext == ".py") return "python";
    if (ext == ".go") return "go";
    if (ext == ".sh" || ext == ".bash") return "bash";
    if (ext == ".json") return "json";
    if (ext == ".toml") return "toml";
    if (ext == ".yaml" || ext == ".yml") return "yaml";
    if (ext == ".html" || ext == ".htm") return "html";
    if (ext == ".css") return "css";
    if (ext == ".js" || ext == ".jsx") return "javascript";
    if (ext == ".ts" || ext == ".tsx") return "typescript";
    if (ext == ".lua") return "lua";
    if (ext == ".java") return "java";
    if (ext == ".diff" || ext == ".patch") return "diff";
    if (fn == "COMMIT_EDITMSG" || ext == ".gitcommit") return "gitcommit";
    return "";
}

namespace {

std::string find_query_file(const std::string& lang) {
    const char* home = std::getenv("HOME");
    std::vector<std::string> candidates = {
        "./queries/" + lang + "/highlights.scm",
        "queries/" + lang + "/highlights.scm",
    };
    if (home) {
        candidates.push_back(std::string(home) + "/.config/fe/runtime/queries/" + lang + "/highlights.scm");
        candidates.push_back(std::string(home) + "/.local/share/fe/runtime/queries/" + lang + "/highlights.scm");
    }
    candidates.push_back("/usr/lib/fe/runtime/queries/" + lang + "/highlights.scm");
    candidates.push_back("/usr/share/fe/runtime/queries/" + lang + "/highlights.scm");

    for (const auto& path : candidates) {
        if (fs::exists(path)) return path;
    }
    return "";
}

void* load_lang_parser(const std::string& lang) {
    const char* home = std::getenv("HOME");
    std::vector<std::string> candidates = {
#ifdef _WIN32
        "tree-sitter-" + lang + ".dll",
        "libtree-sitter-" + lang + ".dll",
        "tree_sitter_" + lang + ".dll",
        "./grammars/" + lang + ".dll",
#endif
        "libtree-sitter-" + lang + ".so",
        "tree_sitter_" + lang + ".so",
        "./grammars/" + lang + ".so"
    };
    if (home) {
        candidates.push_back(std::string(home) + "/.config/fe/runtime/grammars/" + lang + ".so");
        candidates.push_back(std::string(home) + "/.local/share/fe/runtime/grammars/" + lang + ".so");
    }
    candidates.push_back("/usr/lib/fe/runtime/grammars/" + lang + ".so");
    candidates.push_back("/usr/local/lib/fe/runtime/grammars/" + lang + ".so");

    for (const auto& path : candidates) {
        void* handle = dlopen(path.c_str(), RTLD_LAZY);
        if (handle) return handle;
    }
    return nullptr;
}

} // namespace

static ColorRGB parse_hex_color(const std::string& hex) {
    if (hex.size() >= 7 && hex[0] == '#') {
        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            return ColorRGB{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        }
    }
    return ColorRGB{212, 212, 212};
}

HelixTheme& HelixTheme::instance() {
    static HelixTheme s_theme;
    return s_theme;
}

HelixTheme::HelixTheme() {
    init_defaults();
}

void HelixTheme::init_defaults() {
    palette["white"] = {255, 255, 255};
    palette["search_match"] = {81, 92, 107};
    palette["search_match_active"] = {101, 77, 46};
    palette["orange"] = {206, 145, 120};
    palette["gold"] = {215, 186, 125};
    palette["pale_green"] = {181, 206, 168};
    palette["dark_green"] = {106, 153, 85};
    palette["dark_green2"] = {72, 126, 2};
    palette["light_gray"] = {212, 212, 212};
    palette["light_gray2"] = {198, 198, 198};
    palette["light_gray3"] = {238, 238, 238};
    palette["dark_gray"] = {133, 133, 133};
    palette["dark_gray2"] = {30, 30, 30};
    palette["dark_gray3"] = {40, 40, 40};
    palette["dark_gray4"] = {64, 64, 64};
    palette["dark_gray5"] = {139, 148, 158};
    palette["blue"] = {0, 122, 204};
    palette["blue2"] = {86, 156, 214};
    palette["blue3"] = {103, 150, 230};
    palette["blue4"] = {27, 129, 168};
    palette["light_blue"] = {117, 190, 255};
    palette["dark_blue"] = {38, 79, 120};
    palette["dark_blue2"] = {9, 71, 113};
    palette["red"] = {255, 18, 18};
    palette["orange_red"] = {241, 76, 76};
    palette["hunk_add_bg"] = {32, 58, 32};
    palette["hunk_del_bg"] = {58, 32, 32};
    palette["type"] = {78, 201, 176};
    palette["special"] = {197, 134, 192};
    palette["variable"] = {156, 220, 254};
    palette["fn_declaration"] = {220, 220, 170};
    palette["constant"] = {79, 193, 255};
    palette["background"] = {30, 30, 30};
    palette["text"] = {212, 212, 212};
    palette["cursor"] = {166, 166, 166};
    palette["widget"] = {37, 37, 38};
    palette["borders"] = {50, 50, 50};
    palette["navy"] = {0, 43, 80};

    // Syntax defaults matching dark_plus.toml
    styles["comment"]                     = palette["dark_green"];
    styles["comment.line"]                = palette["dark_green"];
    styles["comment.block"]               = palette["dark_green"];
    styles["constant"]                    = palette["constant"];
    styles["constant.builtin"]            = palette["blue2"];
    styles["constant.character"]          = palette["orange"];
    styles["constant.character.escape"]   = palette["gold"];
    styles["constant.numeric"]            = palette["pale_green"];
    styles["constructor"]                 = palette["type"];
    styles["function"]                    = palette["fn_declaration"];
    styles["function.builtin"]            = palette["fn_declaration"];
    styles["function.macro"]              = palette["blue2"];
    styles["function.method"]             = palette["fn_declaration"];
    styles["keyword"]                     = palette["blue2"];
    styles["keyword.control"]             = palette["special"];
    styles["keyword.control.conditional"] = palette["special"];
    styles["keyword.control.repeat"]      = palette["special"];
    styles["keyword.control.return"]      = palette["special"];
    styles["keyword.control.import"]      = palette["special"];
    styles["keyword.directive"]           = palette["special"];
    styles["keyword.function"]            = palette["blue2"];
    styles["keyword.storage"]             = palette["blue2"];
    styles["keyword.storage.type"]        = palette["blue2"];
    styles["label"]                       = palette["blue2"];
    styles["namespace"]                   = palette["type"];
    styles["operator"]                    = palette["text"];
    styles["punctuation"]                 = palette["text"];
    styles["punctuation.delimiter"]       = palette["text"];
    styles["punctuation.bracket"]         = palette["text"];
    styles["special"]                     = palette["light_blue"];
    styles["string"]                      = palette["orange"];
    styles["string.regexp"]               = palette["gold"];
    styles["type"]                        = palette["type"];
    styles["type.builtin"]                = palette["type"];
    styles["type.enum.variant"]           = palette["constant"];
    styles["variable"]                    = palette["variable"];
    styles["variable.builtin"]            = palette["blue2"];
    styles["variable.other.member"]       = palette["variable"];
    styles["variable.parameter"]          = palette["variable"];
    styles["diff.plus"]                   = palette["dark_green2"];
    styles["diff.minus"]                  = palette["orange_red"];

    // UI defaults matching dark_plus.toml
    ui_styles["ui.background"]            = {palette["light_gray"], palette["dark_gray2"], true, true};
    ui_styles["ui.window"]                = {{}, palette["widget"], false, true};
    ui_styles["ui.popup"]                 = {palette["text"], palette["widget"], true, true};
    ui_styles["ui.popup.title"]           = {palette["gold"], {}, true, false};
    ui_styles["ui.cursor"]                = {{0, 0, 0}, palette["cursor"], true, true};
    ui_styles["ui.cursor.primary"]        = {palette["cursor"], {}, true, false};
    ui_styles["ui.selection.primary"]     = {{255, 255, 255}, palette["dark_blue"], true, true};
    ui_styles["ui.linenr"]                = {palette["dark_gray"], palette["dark_gray2"], true, true};
    ui_styles["ui.linenr.selected"]       = {palette["light_gray2"], palette["dark_gray3"], true, true};
    ui_styles["ui.cursorline.primary"]    = {{}, palette["dark_gray3"], false, true};
    ui_styles["ui.statusline"]            = {palette["white"], palette["blue"], true, true};
    ui_styles["ui.statusline.powerline.normal"] = {palette["white"], palette["navy"], true, true};
    ui_styles["ui.statusline.powerline.insert"] = {palette["white"], palette["dark_green2"], true, true};
    ui_styles["ui.statusline.powerline.visual"] = {palette["white"], palette["special"], true, true};
    ui_styles["ui.statusline.powerline.branch"] = {palette["white"], palette["dark_blue2"], true, true};
    ui_styles["ui.statusline.powerline.file"]   = {palette["light_gray3"], palette["dark_blue"], true, true};
    ui_styles["ui.statusline.powerline.fill"]   = {{}, palette["blue"], false, true};
    ui_styles["ui.statusline.powerline.pos"]    = {palette["white"], palette["navy"], true, true};
    ui_styles["ui.statusline.powerline.lang"]   = {palette["white"], palette["dark_blue2"], true, true};
    ui_styles["ui.statusline.powerline.ws"]     = {palette["light_gray"], palette["dark_blue"], true, true};
    ui_styles["ui.hunk-diff.left"]        = {{}, palette["dark_gray2"], false, true};
    ui_styles["ui.hunk-diff.right"]       = {{}, palette["dark_gray2"], false, true};
    ui_styles["ui.hunk-diff.add"]         = {palette["text"], palette["hunk_add_bg"], true, true};
    ui_styles["ui.hunk-diff.del"]         = {palette["text"], palette["hunk_del_bg"], true, true};
    ui_styles["ui.menu.selected"]         = {{}, palette["dark_blue2"], false, true};
    ui_styles["ui.whichkey.key"]          = {palette["dark_green"], {}, true, false};
}

ColorRGB HelixTheme::get_color(const std::string& name_or_hex, ColorRGB default_col) const {
    if (name_or_hex.rfind('#', 0) == 0) {
        return parse_hex_color(name_or_hex);
    }
    auto it = palette.find(name_or_hex);
    if (it != palette.end()) return it->second;
    return default_col;
}

UIStyle HelixTheme::get_ui_style(const std::string& scope, const UIStyle& default_style) const {
    auto it = ui_styles.find(scope);
    if (it != ui_styles.end()) return it->second;
    return default_style;
}

SyntaxStyle HelixTheme::resolve(const std::string& capture) const {
    std::string cur = capture;
    while (!cur.empty()) {
        auto it = styles.find(cur);
        if (it != styles.end()) return it->second;
        size_t dot = cur.rfind('.');
        if (dot == std::string::npos) break;
        cur = cur.substr(0, dot);
    }
    return {212, 212, 212};
}

bool HelixTheme::load_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    palette.clear();
    styles.clear();
    ui_styles.clear();
    init_defaults();

    std::string line;
    bool in_palette = false;
    std::unordered_map<std::string, std::string> syn_aliases;

    while (std::getline(in, line)) {
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            bool in_q = false;
            for (size_t i = 0; i < comment_pos; ++i) {
                if (line[i] == '"') in_q = !in_q;
            }
            if (!in_q) {
                line = line.substr(0, comment_pos);
            }
        }
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) start++;
        size_t end = line.size();
        while (end > start && std::isspace(static_cast<unsigned char>(line[end - 1]))) end--;
        if (start >= end) continue;
        std::string trimmed = line.substr(start, end - start);

        if (trimmed == "[palette]") {
            in_palette = true;
            continue;
        } else if (trimmed.front() == '[' && trimmed.back() == ']') {
            in_palette = false;
            continue;
        }

        size_t eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) continue;

        auto trim_str = [](const std::string& s) {
            size_t a = 0;
            while (a < s.size() && (std::isspace(static_cast<unsigned char>(s[a])) || s[a] == '"')) a++;
            size_t b = s.size();
            while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '"')) b--;
            return (a < b) ? s.substr(a, b - a) : "";
        };

        std::string raw_k = trimmed.substr(0, eq_pos);
        std::string raw_v = trimmed.substr(eq_pos + 1);
        std::string key = trim_str(raw_k);

        if (in_palette) {
            std::string val = trim_str(raw_v);
            if (!val.empty() && val[0] == '#') {
                palette[key] = parse_hex_color(val);
            }
        } else {
            size_t lbrace = raw_v.find('{');
            size_t rbrace = raw_v.rfind('}');
            if (lbrace != std::string::npos && rbrace != std::string::npos && rbrace > lbrace) {
                std::string inner = raw_v.substr(lbrace + 1, rbrace - lbrace - 1);
                // Store raw inline table string for second-pass resolution after [palette] is loaded
                syn_aliases[key] = "{" + inner + "}";
            } else {
                std::string val = trim_str(raw_v);
                syn_aliases[key] = val;
            }
        }
    }

    // Second pass: resolve all syntax aliases and UI styles with a fully populated palette
    auto resolve_token = [&](const std::string& start_val, ColorRGB def_c) -> ColorRGB {
        std::string cur = start_val;
        for (int depth = 0; depth < 5; ++depth) {
            if (cur.rfind('#', 0) == 0 || palette.find(cur) != palette.end()) break;
            auto it = syn_aliases.find(cur);
            if (it != syn_aliases.end() && !it->second.empty() && it->second.front() != '{') {
                cur = it->second;
            } else {
                break;
            }
        }
        return get_color(cur, def_c);
    };

    auto trim_token = [](const std::string& s) {
        size_t a = 0;
        while (a < s.size() && (std::isspace(static_cast<unsigned char>(s[a])) || s[a] == '"')) a++;
        size_t b = s.size();
        while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '"')) b--;
        return (a < b) ? s.substr(a, b - a) : "";
    };

    for (const auto& pair : syn_aliases) {
        const std::string& key = pair.first;
        const std::string& raw_val = pair.second;

        if (!raw_val.empty() && raw_val.front() == '{' && raw_val.back() == '}') {
            std::string inner = raw_val.substr(1, raw_val.size() - 2);
            UIStyle uistyle;
            std::istringstream iss(inner);
            std::string token;
            while (std::getline(iss, token, ',')) {
                size_t teq = token.find('=');
                if (teq == std::string::npos) continue;
                std::string pk = trim_token(token.substr(0, teq));
                std::string pv = trim_token(token.substr(teq + 1));
                if (pk == "fg") {
                    uistyle.fg = resolve_token(pv, {212, 212, 212});
                    uistyle.has_fg = true;
                } else if (pk == "bg") {
                    uistyle.bg = resolve_token(pv, {30, 30, 30});
                    uistyle.has_bg = true;
                }
            }
            ui_styles[key] = uistyle;
            if (uistyle.has_fg) {
                styles[key] = SyntaxStyle{uistyle.fg.r, uistyle.fg.g, uistyle.fg.b};
            }
        } else {
            ColorRGB col = resolve_token(raw_val, {212, 212, 212});
            styles[key] = SyntaxStyle{col.r, col.g, col.b};
            if (key.rfind("ui.", 0) == 0) {
                UIStyle u;
                u.fg = col;
                u.has_fg = true;
                ui_styles[key] = u;
            }
        }
    }

    return true;
}

bool HelixTheme::load_theme(const std::string& theme_name, const std::string& custom_dir) {
    std::vector<std::string> search_paths;
    if (!custom_dir.empty()) {
        search_paths.push_back((fs::path(custom_dir) / "themes" / (theme_name + ".toml")).string());
    }
    search_paths.push_back("themes/" + theme_name + ".toml");
    search_paths.push_back("./themes/" + theme_name + ".toml");

    const char* home = std::getenv("HOME");
    if (home) {
        search_paths.push_back(std::string(home) + "/.config/fe/themes/" + theme_name + ".toml");
        search_paths.push_back(std::string(home) + "/.local/share/fe/runtime/themes/" + theme_name + ".toml");
    }
    search_paths.push_back("/usr/lib/fe/runtime/themes/" + theme_name + ".toml");
    search_paths.push_back("/usr/share/fe/runtime/themes/" + theme_name + ".toml");

    for (const auto& p : search_paths) {
        std::error_code ec;
        if (fs::exists(p, ec) && load_from_file(p)) {
            current_theme_name = theme_name;
            return true;
        }
    }

    if (theme_name == "dark_plus") {
        init_defaults();
        current_theme_name = "dark_plus";
        return true;
    }
    return false;
}

struct SyntaxHighlighter::Impl {
    void* grammar_handle{nullptr};
    TSParser* parser{nullptr};
    TSTree* tree{nullptr};
    TSQuery* query{nullptr};
    TSQueryCursor* cursor{nullptr};
    std::vector<std::vector<SyntaxStyle>> line_styles;

    ~Impl() {
        auto& ts = get_ts();
        if (ts.loaded) {
            if (cursor) ts.ts_query_cursor_delete(cursor);
            if (query) ts.ts_query_delete(query);
            if (tree) ts.ts_tree_delete(tree);
            if (parser) ts.ts_parser_delete(parser);
        }
        if (grammar_handle) dlclose(grammar_handle);
    }
};

SyntaxHighlighter::SyntaxHighlighter() : pimpl(std::make_unique<Impl>()) {}
SyntaxHighlighter::~SyntaxHighlighter() = default;

bool SyntaxHighlighter::init_for_file(const std::string& file_path) {
    language = detect_lang(file_path);
    if (language.empty()) {
        active = false;
        return false;
    }

    active = true;
    auto& ts = get_ts();
    if (!ts.loaded) {
        return true; // Use fallback highlighter
    }

    pimpl->grammar_handle = load_lang_parser(language);
    if (!pimpl->grammar_handle) {
        return true; // Fallback
    }

    std::string sym_name = "tree_sitter_" + language;
    typedef const TSLanguage* (*fn_lang)(void);
    fn_lang lang_func = (fn_lang)dlsym(pimpl->grammar_handle, sym_name.c_str());
    if (!lang_func) {
        return true;
    }

    const TSLanguage* tslang = lang_func();
    pimpl->parser = ts.ts_parser_new();
    ts.ts_parser_set_language(pimpl->parser, tslang);

    std::string query_file = find_query_file(language);
    if (!query_file.empty()) {
        std::ifstream in(query_file);
        if (in.is_open()) {
            std::stringstream ss;
            ss << in.rdbuf();
            std::string q_str = ss.str();

            uint32_t err_offset = 0;
            TSQueryError err_type = TSQueryErrorNone;
            pimpl->query = ts.ts_query_new(tslang, q_str.c_str(), q_str.size(), &err_offset, &err_type);
            if (pimpl->query) {
                pimpl->cursor = ts.ts_query_cursor_new();
            }
        }
    }

    return true;
}

void SyntaxHighlighter::update_text(const std::vector<std::string>& lines) {
    if (!active) return;

    pimpl->line_styles.clear();
    pimpl->line_styles.resize(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        pimpl->line_styles[i].assign(lines[i].size(), {220, 220, 220});
    }

    auto& ts = get_ts();
    if (!ts.loaded || !pimpl->parser || !pimpl->query || !pimpl->cursor) {
        // Run regex fallback
        for (size_t i = 0; i < lines.size(); ++i) {
            pimpl->line_styles[i] = fallback_highlight(lines[i]);
        }
        return;
    }

    std::string full_text;
    for (const auto& l : lines) {
        full_text += l;
        full_text += '\n';
    }

    if (pimpl->tree) {
        ts.ts_tree_delete(pimpl->tree);
    }
    pimpl->tree = ts.ts_parser_parse_string(pimpl->parser, nullptr, full_text.c_str(), full_text.size());
    if (!pimpl->tree) return;

    TSNode root = ts.ts_tree_root_node(pimpl->tree);
    ts.ts_query_cursor_exec(pimpl->cursor, pimpl->query, root);

    TSQueryMatch match;
    while (ts.ts_query_cursor_next_match(pimpl->cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture& cap = match.captures[i];
            uint32_t name_len = 0;
            const char* name = ts.ts_query_capture_name_for_id(pimpl->query, cap.index, &name_len);
            if (!name) continue;

            std::string cap_name(name, name_len);
            SyntaxStyle style = HelixTheme::instance().resolve(cap_name);

            TSPoint start_pt = ts.ts_node_start_point(cap.node);
            TSPoint end_pt = ts.ts_node_end_point(cap.node);

            for (uint32_t r = start_pt.row; r <= end_pt.row && r < pimpl->line_styles.size(); ++r) {
                uint32_t c_start = (r == start_pt.row) ? start_pt.column : 0;
                uint32_t c_end = (r == end_pt.row) ? end_pt.column : pimpl->line_styles[r].size();

                c_start = std::min(c_start, static_cast<uint32_t>(pimpl->line_styles[r].size()));
                c_end = std::min(c_end, static_cast<uint32_t>(pimpl->line_styles[r].size()));

                for (uint32_t col = c_start; col < c_end; ++col) {
                    pimpl->line_styles[r][col] = style;
                }
            }
        }
    }
}

std::vector<SyntaxStyle> SyntaxHighlighter::fallback_highlight(const std::string& line) const {
    std::vector<SyntaxStyle> styles(line.size(), {220, 220, 220});
    size_t i = 0;

    SyntaxStyle kw_style = HelixTheme::instance().resolve("keyword");
    SyntaxStyle type_style = HelixTheme::instance().resolve("type");
    SyntaxStyle str_style = HelixTheme::instance().resolve("string");
    SyntaxStyle comment_style = HelixTheme::instance().resolve("comment");
    SyntaxStyle num_style = HelixTheme::instance().resolve("constant.numeric");
    SyntaxStyle fn_style = HelixTheme::instance().resolve("function");
    SyntaxStyle op_style = HelixTheme::instance().resolve("operator");

    while (i < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
            i++;
            continue;
        }

        // Line comment
        if ((line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') ||
            (line[i] == '#' && language != "c")) {
            for (size_t c = i; c < line.size(); ++c) styles[c] = comment_style;
            break;
        }

        // String
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t start = i++;
            while (i < line.size() && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < line.size()) i++;
                i++;
            }
            if (i < line.size()) i++;
            for (size_t c = start; c < i; ++c) styles[c] = str_style;
            continue;
        }

        // Number
        if (std::isdigit(static_cast<unsigned char>(line[i])) ||
            (line[i] == '0' && i + 1 < line.size() && (line[i + 1] == 'x' || line[i + 1] == 'b'))) {
            size_t start = i;
            while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '.')) i++;
            for (size_t c = start; c < i; ++c) styles[c] = num_style;
            continue;
        }

        // Word (identifier / keyword)
        if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
            size_t start = i;
            while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) i++;
            std::string word = line.substr(start, i - start);

            static const std::unordered_map<std::string, int> kw_map = {
                {"if", 1}, {"else", 1}, {"while", 1}, {"for", 1}, {"return", 1},
                {"break", 1}, {"continue", 1}, {"switch", 1}, {"case", 1}, {"default", 1},
                {"fn", 1}, {"def", 1}, {"func", 1}, {"function", 1}, {"let", 1}, {"mut", 1},
                {"const", 1}, {"var", 1}, {"struct", 1}, {"class", 1}, {"enum", 1},
                {"pub", 1}, {"impl", 1}, {"trait", 1}, {"type", 1}, {"import", 1}, {"export", 1},
                {"from", 1}, {"as", 1}, {"self", 1}, {"null", 1}, {"nullptr", 1}, {"nil", 1},
                {"int", 2}, {"char", 2}, {"void", 2}, {"bool", 2}, {"float", 2}, {"double", 2},
                {"u8", 2}, {"u16", 2}, {"u32", 2}, {"u64", 2}, {"i8", 2}, {"i16", 2},
                {"i32", 2}, {"i64", 2}, {"usize", 2}, {"isize", 2}, {"String", 2}, {"str", 2},
                {"true", 2}, {"false", 2}
            };

            auto it = kw_map.find(word);
            if (it != kw_map.end()) {
                SyntaxStyle s = (it->second == 1) ? kw_style : type_style;
                for (size_t c = start; c < i; ++c) styles[c] = s;
            } else {
                size_t p = i;
                while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) p++;
                if (p < line.size() && line[p] == '(') {
                    for (size_t c = start; c < i; ++c) styles[c] = fn_style;
                }
            }
            continue;
        }

        // Punctuation / operator
        if (std::ispunct(static_cast<unsigned char>(line[i]))) {
            styles[i] = op_style;
        }
        i++;
    }

    return styles;
}

std::vector<SyntaxStyle> SyntaxHighlighter::get_line_styles(int line_idx, const std::string& line) const {
    if (!active) {
        return std::vector<SyntaxStyle>(line.size(), {220, 220, 220});
    }

    if (line_idx >= 0 && line_idx < static_cast<int>(pimpl->line_styles.size())) {
        if (pimpl->line_styles[line_idx].size() == line.size()) {
            return pimpl->line_styles[line_idx];
        }
    }

    return fallback_highlight(line);
}