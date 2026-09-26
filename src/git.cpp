#include "git.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Run a `git show` command and collect its output as lines.
// Returns true when the command exits successfully (status 0), false otherwise.
bool read_show_output(const std::string& cmd, std::vector<std::string>& out) {
    out.clear();
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return false;

    std::string cur;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                out.push_back(cur);
                cur.clear();
            } else {
                cur += buf[i];
            }
        }
    }
    int status = pclose(fp);
    if (status != 0) {
        out.clear();
        return false;
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return true;
}

} // namespace

GitStatus detect_git_status(const std::string& file_path) {
    GitStatus status;
    if (file_path.empty()) return status;

    std::error_code ec;
    fs::path p = fs::absolute(file_path, ec);
    if (ec) p = file_path;
    std::string dir = p.parent_path().string();
    if (dir.empty()) dir = ".";

    // Locate the git work-tree root.
    std::string cmd = "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return status;
    char buf[1024];
    std::string root;
    if (fgets(buf, sizeof(buf), fp)) {
        root = buf;
        while (!root.empty() && (root.back() == '\n' || root.back() == '\r')) {
            root.pop_back();
        }
    }
    pclose(fp);
    if (root.empty()) return status;
    status.is_repo = true;

    // Detect current branch name.
    std::string b_cmd = "git -C \"" + root + "\" rev-parse --abbrev-ref HEAD 2>/dev/null";
    FILE* b_fp = popen(b_cmd.c_str(), "r");
    if (b_fp) {
        char b_buf[256];
        if (fgets(b_buf, sizeof(b_buf), b_fp)) {
            status.branch = b_buf;
            while (!status.branch.empty() && (status.branch.back() == '\n' || status.branch.back() == '\r')) {
                status.branch.pop_back();
            }
        }
        pclose(b_fp);
    }
    if (status.branch.empty() || status.branch == "HEAD") {
        status.branch = "git";
    }

    // Compute path relative to the repo root.
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

    // Try the staged (index) version first, then fall back to HEAD.
    std::vector<std::string> lines;
    if (read_show_output("git -C \"" + root + "\" show \":" + rel_path + "\" 2>/dev/null", lines)) {
        status.base_lines = std::move(lines);
        status.tracked = true;
        return status;
    }

    if (read_show_output("git -C \"" + root + "\" show \"HEAD:" + rel_path + "\" 2>/dev/null", lines)) {
        status.base_lines = std::move(lines);
        status.tracked = true;
        return status;
    }

    return status;
}