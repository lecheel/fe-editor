#include "buffer.hpp"
#include <fstream>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

namespace {

std::vector<GitHunk> compute_myers_diff(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<GitHunk> hunks;
    int n = static_cast<int>(a.size());
    int m = static_cast<int>(b.size());

    if (n == 0 && m == 0) return hunks;

    if (n == 0) {
        GitHunk h;
        h.type = HunkType::ADDED;
        h.orig_start = 0;
        h.orig_count = 0;
        h.cur_start = 0;
        h.cur_count = m;
        h.cur_lines = b;
        hunks.push_back(h);
        return hunks;
    }

    if (m == 0) {
        GitHunk h;
        h.type = HunkType::DELETED;
        h.orig_start = 0;
        h.orig_count = n;
        h.cur_start = 0;
        h.cur_count = 0;
        h.orig_lines = a;
        hunks.push_back(h);
        return hunks;
    }

    int max_d = n + m;
    int v_offset = max_d;
    std::vector<int> v(2 * max_d + 1, 0);
    std::vector<std::vector<int>> trace;

    bool found = false;
    for (int d = 0; d <= max_d && !found; ++d) {
        trace.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int k_idx = k + v_offset;
            int x;
            if (k == -d || (k != d && v[k_idx - 1] < v[k_idx + 1])) {
                x = v[k_idx + 1];
            } else {
                x = v[k_idx - 1] + 1;
            }
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) {
                x++;
                y++;
            }
            v[k_idx] = x;
            if (x >= n && y >= m) {
                found = true;
                break;
            }
        }
    }

    enum OpType { EQUAL, INSERT, DELETE };
    struct EditOp { OpType op; int a_idx; int b_idx; };
    std::vector<EditOp> ops;

    int cur_x = n;
    int cur_y = m;
    for (int d = static_cast<int>(trace.size()) - 1; d >= 0; --d) {
        const auto& prev_v = trace[d];
        int k = cur_x - cur_y;
        int prev_k;
        if (k == -d || (k != d && prev_v[k - 1 + v_offset] < prev_v[k + 1 + v_offset])) {
            prev_k = k + 1;
        } else {
            prev_k = k - 1;
        }
        int prev_k_idx = prev_k + v_offset;
        int prev_x = (d == 0) ? 0 : prev_v[prev_k_idx];
        int prev_y = prev_x - prev_k;

        while (cur_x > prev_x && cur_y > prev_y) {
            ops.push_back({EQUAL, cur_x - 1, cur_y - 1});
            cur_x--;
            cur_y--;
        }

        if (d > 0) {
            if (cur_x == prev_x) {
                ops.push_back({INSERT, prev_x, cur_y - 1});
                cur_y--;
            } else {
                ops.push_back({DELETE, cur_x - 1, prev_y});
                cur_x--;
            }
        }
    }

    std::reverse(ops.begin(), ops.end());

    size_t i = 0;
    while (i < ops.size()) {
        if (ops[i].op == EQUAL) {
            i++;
            continue;
        }

        size_t j = i;
        int del_count = 0;
        int ins_count = 0;
        int o_start = -1;
        int c_start = -1;

        std::vector<std::string> hunk_orig;
        std::vector<std::string> hunk_cur;

        while (j < ops.size() && ops[j].op != EQUAL) {
            if (ops[j].op == DELETE) {
                if (o_start == -1) o_start = ops[j].a_idx;
                hunk_orig.push_back(a[ops[j].a_idx]);
                del_count++;
            } else if (ops[j].op == INSERT) {
                if (c_start == -1) c_start = ops[j].b_idx;
                hunk_cur.push_back(b[ops[j].b_idx]);
                ins_count++;
            }
            j++;
        }

        if (o_start == -1) o_start = (i > 0) ? ops[i - 1].a_idx + 1 : 0;
        if (c_start == -1) c_start = (i > 0) ? ops[i - 1].b_idx + 1 : 0;

        GitHunk hunk;
        hunk.orig_start = o_start;
        hunk.orig_count = del_count;
        hunk.cur_start = c_start;
        hunk.cur_count = ins_count;
        hunk.orig_lines = std::move(hunk_orig);
        hunk.cur_lines = std::move(hunk_cur);

        if (del_count > 0 && ins_count > 0) {
            hunk.type = HunkType::MODIFIED;
        } else if (ins_count > 0) {
            hunk.type = HunkType::ADDED;
        } else {
            hunk.type = HunkType::DELETED;
        }

        hunks.push_back(hunk);
        i = j;
    }

    return hunks;
}

} // namespace

TextBuffer::TextBuffer(std::string name, std::vector<std::string> initial_lines, std::string path)
    : name(std::move(name)), file_path(std::move(path)), lines(std::move(initial_lines)) {
    init_git_status();
    syntax = std::make_shared<SyntaxHighlighter>();
    syntax->init_for_file(!file_path.empty() ? file_path : name);
    syntax->update_text(lines);
}

std::shared_ptr<TextBuffer> TextBuffer::from_file(const std::string& path) {
    std::vector<std::string> loaded_lines;
    std::ifstream in(path);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            loaded_lines.push_back(line);
        }
        if (loaded_lines.empty()) {
            loaded_lines.push_back("");
        }
    } else {
        loaded_lines.push_back("");
    }
    return std::make_shared<TextBuffer>(path, loaded_lines, path);
}

void TextBuffer::init_git_status() {
    is_git_repo = false;
    git_tracked = false;
    git_base_lines.clear();
    hunks_dirty = true;

    if (file_path.empty()) return;

    std::error_code ec;
    fs::path p = fs::absolute(file_path, ec);
    if (ec) p = file_path;
    std::string dir = p.parent_path().string();
    if (dir.empty()) dir = ".";

    std::string cmd = "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return;
    char buf[1024];
    std::string root;
    if (fgets(buf, sizeof(buf), fp)) {
        root = buf;
        while (!root.empty() && (root.back() == '\n' || root.back() == '\r')) {
            root.pop_back();
        }
    }
    pclose(fp);

    if (root.empty()) return;
    is_git_repo = true;

    std::string rel_path;
    try {
        rel_path = fs::relative(p, fs::path(root)).string();
    } catch (...) {
        rel_path = p.filename().string();
    }

    while (rel_path.rfind("./", 0) == 0) {
        rel_path = rel_path.substr(2);
    }
    while (!rel_path.empty() && rel_path.front() == '/') {
        rel_path = rel_path.substr(1);
    }

    std::string show_cmd = "git -C \"" + root + "\" show \":" + rel_path + "\" 2>/dev/null";
    fp = popen(show_cmd.c_str(), "r");
    if (fp) {
        std::vector<std::string> glines;
        std::string cur;
        char read_buf[4096];
        size_t n;
        while ((n = fread(read_buf, 1, sizeof(read_buf), fp)) > 0) {
            for (size_t i = 0; i < n; ++i) {
                if (read_buf[i] == '\n') {
                    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                    glines.push_back(cur);
                    cur.clear();
                } else {
                    cur += read_buf[i];
                }
            }
        }
        int status = pclose(fp);
        if (status == 0) {
            if (!cur.empty()) {
                if (cur.back() == '\r') cur.pop_back();
                glines.push_back(cur);
            }
            git_base_lines = std::move(glines);
            git_tracked = true;
            return;
        }
    }

    show_cmd = "git -C \"" + root + "\" show \"HEAD:" + rel_path + "\" 2>/dev/null";
    fp = popen(show_cmd.c_str(), "r");
    if (fp) {
        std::vector<std::string> glines;
        std::string cur;
        char read_buf[4096];
        size_t n;
        while ((n = fread(read_buf, 1, sizeof(read_buf), fp)) > 0) {
            for (size_t i = 0; i < n; ++i) {
                if (read_buf[i] == '\n') {
                    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                    glines.push_back(cur);
                    cur.clear();
                } else {
                    cur += read_buf[i];
                }
            }
        }
        int status = pclose(fp);
        if (status == 0) {
            if (!cur.empty()) {
                if (cur.back() == '\r') cur.pop_back();
                glines.push_back(cur);
            }
            git_base_lines = std::move(glines);
            git_tracked = true;
            return;
        }
    }

    git_tracked = false;
    git_base_lines.clear();
}

const std::vector<GitHunk>& TextBuffer::get_hunks() const {
    if (!is_git_repo) {
        cached_hunks.clear();
        return cached_hunks;
    }
    if (hunks_dirty || last_diff_version != version) {
        cached_hunks = compute_myers_diff(git_base_lines, lines);
        last_diff_version = version;
        hunks_dirty = false;
    }
    return cached_hunks;
}

void TextBuffer::push_undo(const std::vector<Cursor>& cursors) {
    undo_stack.push_back({lines, cursors});
    redo_stack.clear();
    modified = true;
    version++;
    invalidate_hunks();
    if (syntax) syntax->update_text(lines);
    if (undo_stack.size() > 100) {
        undo_stack.erase(undo_stack.begin());
    }
}

bool TextBuffer::undo(std::vector<Cursor>& cursors) {
    if (undo_stack.empty()) return false;
    redo_stack.push_back({lines, cursors});
    auto state = undo_stack.back();
    undo_stack.pop_back();
    lines = state.lines;
    cursors = state.cursors;
    modified = true;
    version++;
    invalidate_hunks();
    if (syntax) syntax->update_text(lines);
    return true;
}

bool TextBuffer::redo(std::vector<Cursor>& cursors) {
    if (redo_stack.empty()) return false;
    undo_stack.push_back({lines, cursors});
    auto state = redo_stack.back();
    redo_stack.pop_back();
    lines = state.lines;
    cursors = state.cursors;
    modified = true;
    version++;
    invalidate_hunks();
    if (syntax) syntax->update_text(lines);
    return true;
}

bool TextBuffer::save_to_file(const std::string& path_override) {
    std::string target = !path_override.empty() ? path_override : (!file_path.empty() ? file_path : name);
    std::ofstream out(target);
    if (!out.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i) {
        out << lines[i] << "\n";
    }
    file_path = target;
    name = target;
    modified = false;
    init_git_status();
    if (syntax) {
        syntax->init_for_file(target);
        syntax->update_text(lines);
    }
    return true;
}