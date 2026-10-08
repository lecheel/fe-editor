#include "dot_command.hpp"
#include "engine.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

DotCommand s_last_dot_cmd = DotCommand::NONE;

// Shared post-mutation bookkeeping: stamp version, refresh syntax, clamp
// cursors, scroll, set the "last dot" pointer for `.`-repeat, and post the
// status message. Removes ~6 duplicated tail lines per branch.
inline void commit_buffer_edit(VimEngine& engine,
                               TextBuffer& buf,
                               Window& win,
                               DotCommand last_cmd,
                               bool is_repeat,
                               const char* repeat_msg,
                               const char* fresh_msg) {
    buf.modified = true;
    buf.version++;
    buf.invalidate_hunks();
    if (buf.syntax) buf.syntax->update_text(buf.lines);
    win.clamp_all_cursors(buf, Mode::NORMAL);
    win.deduplicate_cursors();
    engine.update_window_scroll(win, buf);
    if (!is_repeat) s_last_dot_cmd = last_cmd;
    engine.set_info_msg(is_repeat ? repeat_msg : fresh_msg);
}

// Shared "delete from line 0 through cursor line inclusive" used by d0/dgg.
inline void delete_to_top_of_file(VimEngine& engine,
                                  TextBuffer& buf,
                                  Window& win,
                                  bool is_repeat,
                                  const char* repeat_msg,
                                  const char* fresh_msg) {
    buf.push_undo(win.cursors);
    int target_y = win.cursors.empty() ? 0 : win.cursors.front().y;
    target_y = std::clamp(target_y, 0, static_cast<int>(buf.lines.size()) - 1);
    buf.lines.erase(buf.lines.begin(), buf.lines.begin() + target_y + 1);
    if (buf.lines.empty()) {
        buf.lines.push_back("");
    }
    win.cursors = {{0, 0}};
    commit_buffer_edit(engine, buf, win, DotCommand::D_TOP, is_repeat,
                       repeat_msg, fresh_msg);
}

// ---- Action interface ------------------------------------------------------
struct IDotAction {
    virtual ~IDotAction() = default;
    virtual void run(VimEngine& engine, bool is_repeat) = 0;
};

// ---- Registry (the "Map" in ARM) ------------------------------------------
class DotActionRegistry {
public:
    static DotActionRegistry& instance() {
        static DotActionRegistry r;
        return r;
    }
    void register_action(DotCommand cmd, std::unique_ptr<IDotAction> a) {
        table_[cmd] = std::move(a);
    }
    IDotAction* lookup(DotCommand cmd) const {
        auto it = table_.find(cmd);
        return it == table_.end() ? nullptr : it->second.get();
    }
private:
    std::unordered_map<DotCommand, std::unique_ptr<IDotAction>> table_;
    DotActionRegistry() = default;
};

// ---- Concrete actions ------------------------------------------------------
struct DotDD final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::set<int> lines_to_delete;
        for (const auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                lines_to_delete.insert(c.y);
            }
        }
        engine.yank_reg.is_linewise = true;
        engine.yank_reg.lines.clear();
        engine.yank_reg.text.clear();
        for (int y : lines_to_delete) {
            engine.yank_reg.lines.push_back(buf.lines[y]);
            engine.yank_reg.text += buf.lines[y] + "\n";
        }
        std::vector<int> sorted_lines(lines_to_delete.rbegin(), lines_to_delete.rend());
        for (int y : sorted_lines) {
            if (buf.lines.size() > 1) {
                buf.lines.erase(buf.lines.begin() + y);
            } else {
                buf.lines[0] = "";
            }
        }
        commit_buffer_edit(engine, buf, win, DotCommand::DD, is_repeat,
                           "Repeated: dd", "Deleted line (dd)");
    }
};

struct DotDW final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                int end_x = compute_dw_end(line, c.x);
                if (end_x > c.x) {
                    line.erase(c.x, end_x - c.x);
                }
            }
        }
        commit_buffer_edit(engine, buf, win, DotCommand::DW, is_repeat,
                           "Repeated: dw", "Deleted word (dw)");
    }
};

struct DotDB final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x > 0) {
                    int p = std::min(c.x, static_cast<int>(line.size()));
                    while (p > 0 && std::isspace(static_cast<unsigned char>(line[p - 1]))) p--;
                    auto is_word = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
                    if (p > 0) {
                        bool word = is_word(line[p - 1]);
                        while (p > 0 && is_word(line[p - 1]) == word && !std::isspace(static_cast<unsigned char>(line[p - 1]))) p--;
                    }
                    line.erase(p, c.x - p);
                    c.x = p;
                }
            }
        }
        commit_buffer_edit(engine, buf, win, DotCommand::DB, is_repeat,
                           "Repeated: db", "Deleted word backward (db)");
    }
};

struct DotDE final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                int len = static_cast<int>(line.size());
                if (c.x < len) {
                    int p = c.x;
                    auto is_word = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
                    if (std::isspace(static_cast<unsigned char>(line[p]))) {
                        while (p < len && std::isspace(static_cast<unsigned char>(line[p]))) p++;
                    } else {
                        bool word = is_word(line[p]);
                        while (p < len && is_word(line[p]) == word && !std::isspace(static_cast<unsigned char>(line[p]))) p++;
                    }
                    if (p > c.x) {
                        line.erase(c.x, p - c.x);
                    }
                }
            }
        }
        commit_buffer_edit(engine, buf, win, DotCommand::DE, is_repeat,
                           "Repeated: de", "Deleted to word end (de)");
    }
};

struct DotDCaret final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x > 0) {
                    int del_count = std::min(c.x, static_cast<int>(line.size()));
                    line.erase(0, del_count);
                    c.x = 0;
                }
            }
        }
        commit_buffer_edit(engine, buf, win, DotCommand::D_CARET, is_repeat,
                           "Repeated: d^", "Deleted to line start (d^)");
    }
};

// Both d0 and dgg dispatch to DotCommand::D_TOP at the call site, and the
// original first-branch-wins ladder reported "d0" for either — preserved here.
struct DotDTop final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        delete_to_top_of_file(engine, engine.active_buf(), engine.active_win(),
                              is_repeat, "Repeated: d0", "Deleted to top of file (d0)");
    }
};

struct DotDJ final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        int cur_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        if (cur_y >= 0 && cur_y < static_cast<int>(buf.lines.size())) {
            int count = std::min(2, static_cast<int>(buf.lines.size()) - cur_y);
            buf.lines.erase(buf.lines.begin() + cur_y, buf.lines.begin() + cur_y + count);
            if (buf.lines.empty()) buf.lines.push_back("");
        }
        commit_buffer_edit(engine, buf, win, DotCommand::DJ, is_repeat,
                           "Repeated: dj", "Deleted 2 lines (dj)");
    }
};

struct DotDK final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        int cur_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        int start_y = std::max(0, cur_y - 1);
        int count = std::min(2, static_cast<int>(buf.lines.size()) - start_y);
        if (start_y < static_cast<int>(buf.lines.size())) {
            buf.lines.erase(buf.lines.begin() + start_y, buf.lines.begin() + start_y + count);
            if (buf.lines.empty()) buf.lines.push_back("");
        }
        commit_buffer_edit(engine, buf, win, DotCommand::DK, is_repeat,
                           "Repeated: dk", "Deleted 2 lines (dk)");
    }
};

struct DotX final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        std::string deleted_text;
        bool any_deleted = false;
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (!line.empty() && c.x < static_cast<int>(line.size())) {
                    deleted_text += line[c.x];
                    line.erase(c.x, 1);
                    any_deleted = true;
                }
            }
        }
        if (any_deleted) {
            engine.yank_reg.is_linewise = false;
            engine.yank_reg.text = deleted_text;
            engine.yank_reg.lines = {deleted_text};
        }
        commit_buffer_edit(engine, buf, win, DotCommand::X, is_repeat,
                           "Repeated: x", "Deleted char (x)");
    }
};

struct DotCapX final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        std::string deleted_text;
        bool any_deleted = false;
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (!line.empty() && c.x > 0 && c.x <= static_cast<int>(line.size())) {
                    deleted_text += line[c.x - 1];
                    line.erase(c.x - 1, 1);
                    c.x--;
                    any_deleted = true;
                }
            }
        }
        if (any_deleted) {
            engine.yank_reg.is_linewise = false;
            engine.yank_reg.text = deleted_text;
            engine.yank_reg.lines = {deleted_text};
        }
        commit_buffer_edit(engine, buf, win, DotCommand::CAP_X, is_repeat,
                           "Repeated: X", "Deleted char before (X)");
    }
};

struct DotDEnd final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        int target_y = win.cursors.empty() ? 0 : win.cursors.front().y;
        target_y = std::clamp(target_y, 0, static_cast<int>(buf.lines.size()) - 1);

        engine.yank_reg.is_linewise = true;
        engine.yank_reg.lines.clear();
        engine.yank_reg.text.clear();
        for (int y = target_y; y < static_cast<int>(buf.lines.size()); ++y) {
            engine.yank_reg.lines.push_back(buf.lines[y]);
            engine.yank_reg.text += buf.lines[y] + "\n";
        }

        buf.lines.erase(buf.lines.begin() + target_y, buf.lines.end());
        if (buf.lines.empty()) {
            buf.lines.push_back("");
            win.cursors = {{0, 0}};
        } else {
            int new_y = std::min(target_y, static_cast<int>(buf.lines.size()) - 1);
            win.cursors = {{new_y, 0}};
        }
        commit_buffer_edit(engine, buf, win, DotCommand::D_END, is_repeat,
                           "Repeated: dG", "Deleted to end of file (dG)");
    }
};

struct DotDDollar final : IDotAction {
    void run(VimEngine& engine, bool is_repeat) override {
        auto& win = engine.active_win();
        auto& buf = engine.active_buf();
        buf.push_undo(win.cursors);
        std::sort(win.cursors.begin(), win.cursors.end());
        std::string deleted_text;
        bool any_deleted = false;
        for (auto& c : win.cursors) {
            if (c.y >= 0 && c.y < static_cast<int>(buf.lines.size())) {
                std::string& line = buf.lines[c.y];
                if (c.x < static_cast<int>(line.size())) {
                    deleted_text = line.substr(c.x);
                    line.erase(c.x);
                    any_deleted = true;
                }
            }
        }
        if (any_deleted) {
            engine.yank_reg.is_linewise = false;
            engine.yank_reg.text = deleted_text;
            engine.yank_reg.lines = {deleted_text};
        }
        commit_buffer_edit(engine, buf, win, DotCommand::D_DOLLAR, is_repeat,
                           "Repeated: d$", "Deleted to line end (d$)");
    }
};

// ---- RAII registrar: wires every DotCommand enum to its action at startup --
struct DotActionsRegistrar {
    DotActionsRegistrar() {
        auto& R = DotActionRegistry::instance();
        R.register_action(DotCommand::DD,        std::make_unique<DotDD>());
        R.register_action(DotCommand::DW,       std::make_unique<DotDW>());
        R.register_action(DotCommand::DB,       std::make_unique<DotDB>());
        R.register_action(DotCommand::DE,       std::make_unique<DotDE>());
        R.register_action(DotCommand::D_CARET,  std::make_unique<DotDCaret>());
        // D_ZERO was never dispatched at runtime but is registered for safety;
        // both D_ZERO and D_TOP map to the same action (matches original
        // first-branch-wins ladder behavior).
        R.register_action(DotCommand::D_ZERO,   std::make_unique<DotDTop>());
        R.register_action(DotCommand::D_TOP,    std::make_unique<DotDTop>());
        R.register_action(DotCommand::DJ,       std::make_unique<DotDJ>());
        R.register_action(DotCommand::DK,       std::make_unique<DotDK>());
        R.register_action(DotCommand::X,        std::make_unique<DotX>());
        R.register_action(DotCommand::CAP_X,    std::make_unique<DotCapX>());
        R.register_action(DotCommand::D_END,    std::make_unique<DotDEnd>());
        R.register_action(DotCommand::D_DOLLAR, std::make_unique<DotDDollar>());
    }
} g_dot_actions_registrar;

} // namespace

int compute_dw_end(const std::string& line, int cx) {
    int len = static_cast<int>(line.size());
    if (cx >= len) return cx;
    auto is_word = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };
    auto is_space = [](char ch) {
        return std::isspace(static_cast<unsigned char>(ch));
    };

    int p = cx;
    if (is_word(line[p])) {
        while (p < len && is_word(line[p])) p++;
        while (p < len && is_space(line[p])) p++;
    } else if (is_space(line[p])) {
        while (p < len && is_space(line[p])) p++;
    } else {
        while (p < len && !is_word(line[p]) && !is_space(line[p])) p++;
        while (p < len && is_space(line[p])) p++;
    }
    return p;
}

DotCommand get_last_dot_command() {
    return s_last_dot_cmd;
}

void execute_dot_command(VimEngine& engine, DotCommand cmd, bool is_repeat) {
    if (auto* action = DotActionRegistry::instance().lookup(cmd)) {
        action->run(engine, is_repeat);
    }
}