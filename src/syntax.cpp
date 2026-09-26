#include "syntax.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <dlfcn.h>
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
    if (ext == ".diff" || ext == ".patch") return "diff";
    if (fn == "COMMIT_EDITMSG" || ext == ".gitcommit") return "gitcommit";
    return "";
}

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

HelixTheme::HelixTheme() {
    // Helix theme capture mappings with standard theme colors
    styles["keyword"]                     = {255, 110, 145};
    styles["keyword.control"]             = {255, 105, 140};
    styles["keyword.control.conditional"] = {255, 115, 150};
    styles["keyword.control.repeat"]      = {255, 125, 160};
    styles["keyword.control.return"]      = {255, 95, 130};
    styles["keyword.control.import"]      = {255, 140, 180};
    styles["keyword.function"]            = {100, 180, 245};
    styles["keyword.storage"]             = {100, 190, 240};
    styles["keyword.storage.type"]        = {100, 200, 240};
    styles["keyword.directive"]           = {225, 150, 255};

    styles["function"]                    = {130, 185, 255};
    styles["function.builtin"]            = {110, 210, 255};
    styles["function.method"]             = {140, 190, 255};
    styles["function.macro"]              = {230, 160, 255};
    styles["function.special"]            = {240, 170, 255};

    styles["type"]                        = {245, 200, 100};
    styles["type.builtin"]                = {235, 180, 80};
    styles["type.enum.variant"]           = {240, 160, 120};

    styles["variable"]                    = {220, 225, 235};
    styles["variable.parameter"]          = {245, 165, 110};
    styles["variable.builtin"]            = {255, 135, 135};
    styles["variable.other.member"]       = {170, 215, 230};

    styles["string"]                      = {150, 225, 140};
    styles["string.regexp"]               = {160, 230, 170};
    styles["string.special.path"]         = {180, 220, 160};

    styles["constant.numeric"]            = {255, 180, 100};
    styles["constant.builtin.boolean"]    = {255, 140, 100};
    styles["constant.character.escape"]   = {255, 190, 120};
    styles["constant"]                    = {255, 175, 115};

    styles["comment"]                     = {120, 130, 145};
    styles["comment.line"]                = {120, 130, 145};
    styles["comment.block"]               = {120, 130, 145};

    styles["operator"]                    = {200, 205, 215};
    styles["punctuation.bracket"]         = {180, 185, 195};
    styles["punctuation.delimiter"]       = {160, 165, 175};
    styles["punctuation.special"]         = {220, 170, 240};

    styles["diff.plus"]                   = {80, 220, 100};
    styles["diff.minus"]                  = {240, 80, 80};
    styles["label"]                       = {255, 190, 100};
    styles["constructor"]                 = {240, 190, 90};
    styles["namespace"]                   = {235, 170, 110};
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
    return {220, 220, 220};
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
            SyntaxStyle style = theme.resolve(cap_name);

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

    SyntaxStyle kw_style = theme.resolve("keyword");
    SyntaxStyle type_style = theme.resolve("type");
    SyntaxStyle str_style = theme.resolve("string");
    SyntaxStyle comment_style = theme.resolve("comment");
    SyntaxStyle num_style = theme.resolve("constant.numeric");
    SyntaxStyle fn_style = theme.resolve("function");
    SyntaxStyle op_style = theme.resolve("operator");

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