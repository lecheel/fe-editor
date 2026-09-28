#include "git.hpp"
#include <git2.h>
#include <filesystem>
#include <string>
#include <vector>
#include <set>
#include <cstring>
#include <fstream>
#include <sstream>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

struct GitInitGuard {
    GitInitGuard() {
        git_libgit2_init();
    }
    ~GitInitGuard() {
        git_libgit2_shutdown();
    }
};

static GitInitGuard g_git_init;

void split_lines(const char* data, size_t size, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur += data[i];
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
}

} // namespace

std::string detect_git_repo_root(const std::string& start_path) {
    std::string dir = start_path;
    std::error_code ec;
    if (dir.empty()) {
        dir = fs::current_path(ec).string();
    } else {
        fs::path p = fs::absolute(start_path, ec);
        if (!ec) {
            dir = fs::is_directory(p, ec) ? p.string() : p.parent_path().string();
        }
    }
    if (dir.empty()) dir = ".";

    git_repository* repo = nullptr;
    if (git_repository_open_ext(&repo, dir.c_str(), 0, nullptr) == 0) {
        const char* workdir = git_repository_workdir(repo);
        std::string toplevel;
        if (workdir) {
            toplevel = fs::path(workdir).lexically_normal().string();
            while (toplevel.size() > 1 && (toplevel.back() == '/' || toplevel.back() == '\\')) {
                toplevel.pop_back();
            }
        }
        git_repository_free(repo);
        if (!toplevel.empty()) {
            return toplevel;
        }
    }
    return "";
}

bool get_git_project_files(const std::string& repo_root, std::vector<std::string>& files) {
    git_repository* repo = nullptr;
    if (git_repository_open_ext(&repo, repo_root.c_str(), 0, nullptr) != 0) {
        return false;
    }

    std::set<std::string> all_files;

    git_index* index = nullptr;
    if (git_repository_index(&index, repo) == 0) {
        size_t count = git_index_entrycount(index);
        for (size_t i = 0; i < count; ++i) {
            const git_index_entry* entry = git_index_get_byindex(index, i);
            if (entry && entry->path) {
                all_files.insert(entry->path);
            }
        }
        git_index_free(index);
    }

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
                 GIT_STATUS_OPT_EXCLUDE_SUBMODULES;

    git_status_list* status_list = nullptr;
    if (git_status_list_new(&status_list, repo, &opts) == 0) {
        size_t count = git_status_list_entrycount(status_list);
        for (size_t i = 0; i < count; ++i) {
            const git_status_entry* s = git_status_byindex(status_list, i);
            if (!s) continue;

            if (s->status & GIT_STATUS_WT_NEW) {
                if (s->index_to_workdir && s->index_to_workdir->new_file.path) {
                    all_files.insert(s->index_to_workdir->new_file.path);
                }
            } else if (s->status & GIT_STATUS_WT_DELETED) {
                if (s->index_to_workdir && s->index_to_workdir->old_file.path) {
                    all_files.erase(s->index_to_workdir->old_file.path);
                }
            }
        }
        git_status_list_free(status_list);
    }

    git_repository_free(repo);

    files.assign(all_files.begin(), all_files.end());
    if (files.size() > 5000) {
        files.resize(5000);
    }
    return true;
}

GitStatus detect_git_status(const std::string& file_path) {
    GitStatus status;
    if (file_path.empty()) return status;

    std::error_code ec;
    fs::path p = fs::absolute(file_path, ec);
    if (ec) p = file_path;
    std::string dir = p.parent_path().string();
    if (dir.empty()) dir = ".";

    git_repository* repo = nullptr;
    if (git_repository_open_ext(&repo, dir.c_str(), 0, nullptr) != 0) {
        return status;
    }

    status.is_repo = true;

    const char* workdir = git_repository_workdir(repo);
    std::string root = workdir ? fs::path(workdir).lexically_normal().string() : "";
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) {
        root.pop_back();
    }

    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, repo) == 0) {
        const char* bname = git_reference_shorthand(head_ref);
        if (bname && *bname != '\0') {
            status.branch = bname;
        }
        git_reference_free(head_ref);
    } else {
        git_reference* head_sym = nullptr;
        if (git_reference_lookup(&head_sym, repo, "HEAD") == 0) {
            if (git_reference_type(head_sym) == GIT_REFERENCE_SYMBOLIC) {
                const char* target = git_reference_symbolic_target(head_sym);
                if (target) {
                    const char* prefix = "refs/heads/";
                    if (std::strncmp(target, prefix, std::strlen(prefix)) == 0) {
                        status.branch = target + std::strlen(prefix);
                    } else {
                        status.branch = target;
                    }
                }
            }
            git_reference_free(head_sym);
        }
    }

    if (status.branch.empty() || status.branch == "HEAD") {
        status.branch = "git";
    }

    if (root.empty()) {
        git_repository_free(repo);
        return status;
    }

    std::string rel_path;
    try {
        rel_path = fs::relative(p, fs::path(root), ec).generic_string();
    } catch (...) {
        rel_path = p.filename().generic_string();
    }
    while (rel_path.rfind("./", 0) == 0) {
        rel_path = rel_path.substr(2);
    }
    while (!rel_path.empty() && rel_path.front() == '/') {
        rel_path = rel_path.substr(1);
    }

    git_index* index = nullptr;
    if (git_repository_index(&index, repo) == 0) {
        const git_index_entry* entry = git_index_get_bypath(index, rel_path.c_str(), 0);
        if (entry) {
            git_blob* blob = nullptr;
            if (git_blob_lookup(&blob, repo, &entry->id) == 0) {
                const char* content = static_cast<const char*>(git_blob_rawcontent(blob));
                size_t size = static_cast<size_t>(git_blob_rawsize(blob));
                split_lines(content, size, status.base_lines);
                status.tracked = true;
                git_blob_free(blob);
                git_index_free(index);
                git_repository_free(repo);
                return status;
            }
        }
        git_index_free(index);
    }

    std::string rev = "HEAD:" + rel_path;
    git_object* head_obj = nullptr;
    if (git_revparse_single(&head_obj, repo, rev.c_str()) == 0) {
        if (git_object_type(head_obj) == GIT_OBJECT_BLOB) {
            git_blob* blob = reinterpret_cast<git_blob*>(head_obj);
            const char* content = static_cast<const char*>(git_blob_rawcontent(blob));
            size_t size = static_cast<size_t>(git_blob_rawsize(blob));
            split_lines(content, size, status.base_lines);
            status.tracked = true;
        }
        git_object_free(head_obj);
    }

    git_repository_free(repo);
    return status;
}

static std::string exec_git_cmd(const std::string& repo_root, const std::string& git_args) {
    std::string cmd = "git -C \"" + repo_root + "\" " + git_args + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "";
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        out += buf;
    }
    pclose(fp);
    return out;
}

static int run_git_cmd_status(const std::string& repo_root, const std::string& git_args) {
    std::string cmd = "git -C \"" + repo_root + "\" " + git_args + " >/dev/null 2>&1";
    int res = system(cmd.c_str());
    return res;
}

GitViewData query_git_view_data(const std::string& repo_root) {
    GitViewData data;
    data.root = repo_root;
    if (repo_root.empty()) return data;

    std::string check = exec_git_cmd(repo_root, "rev-parse --is-inside-work-tree");
    while (!check.empty() && (check.back() == '\n' || check.back() == '\r')) check.pop_back();
    if (check != "true") {
        return data;
    }
    data.is_repo = true;

    // 1. Status porcelain v1
    std::string status_out = exec_git_cmd(repo_root, "status --porcelain=v1 -uall");
    std::istringstream s_iss(status_out);
    std::string line;
    while (std::getline(s_iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3) continue;
        char x = line[0];
        char y = line[1];
        std::string path_part = line.substr(3);
        size_t arrow = path_part.rfind(" -> ");
        if (arrow != std::string::npos) {
            path_part = path_part.substr(arrow + 4);
        }
        if (!path_part.empty() && path_part.front() == '"' && path_part.back() == '"') {
            path_part = path_part.substr(1, path_part.size() - 2);
        }

        if (x == '?' && y == '?') {
            data.untracked.push_back({'?', path_part});
        } else {
            if (x != ' ' && x != '?') {
                data.staged.push_back({x, path_part});
            }
            if (y != ' ' && y != '?') {
                data.unstaged.push_back({y, path_part});
            }
        }
    }

    // 2. Recent Commits (HEAD and HEAD~1)
    std::string log_out = exec_git_cmd(repo_root, "log -n 2 --format=\"%h%x1f%s\"");
    std::istringstream l_iss(log_out);
    std::vector<std::pair<std::string, std::string>> commits;
    while (std::getline(l_iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t sep = line.find('\x1f');
        if (sep != std::string::npos) {
            commits.push_back({line.substr(0, sep), line.substr(sep + 1)});
        } else {
            commits.push_back({line, ""});
        }
    }

    for (const auto& c : commits) {
        GitCommitInfo ci;
        ci.hash = c.first;
        ci.subject = c.second;
        std::string diff_out = exec_git_cmd(repo_root, "diff-tree --no-commit-id --name-status -r " + ci.hash);
        std::istringstream d_iss(diff_out);
        std::string d_line;
        while (std::getline(d_iss, d_line)) {
            if (!d_line.empty() && d_line.back() == '\r') d_line.pop_back();
            if (d_line.size() < 2) continue;
            char g = d_line[0];
            size_t tab = d_line.find('\t');
            if (tab != std::string::npos) {
                std::string p = d_line.substr(tab + 1);
                size_t tab2 = p.find('\t');
                if (tab2 != std::string::npos) {
                    p = p.substr(tab2 + 1);
                }
                ci.files.push_back({g, p});
            }
        }
        data.recent_commits.push_back(std::move(ci));
    }

    // 3. Stashes
    std::string stash_out = exec_git_cmd(repo_root, "stash list");
    std::istringstream st_iss(stash_out);
    while (std::getline(st_iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string ref = line.substr(0, colon);
            std::string subj = line.substr(colon + 1);
            while (!subj.empty() && subj.front() == ' ') subj.erase(0, 1);
            int idx = 0;
            size_t at = ref.find("@{");
            size_t cb = ref.find('}', at);
            if (at != std::string::npos && cb != std::string::npos) {
                try { idx = std::stoi(ref.substr(at + 2, cb - at - 2)); } catch (...) {}
            }
            data.stashes.push_back({idx, ref, subj});
        }
    }

    // 4. Branches
    std::string branch_out = exec_git_cmd(repo_root, "branch --sort=-committerdate --format=\"%(HEAD)%(refname:short)%00%(committerdate:relative)\"");
    std::istringstream b_iss(branch_out);
    while (std::getline(b_iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        bool is_cur = false;
        size_t start = 0;
        if (line[0] == '*') {
            is_cur = true;
            start = 1;
        }
        size_t null_pos = line.find('\0', start);
        std::string bname;
        std::string reltime;
        if (null_pos != std::string::npos) {
            bname = line.substr(start, null_pos - start);
            reltime = line.substr(null_pos + 1);
        } else {
            bname = line.substr(start);
        }
        while (!bname.empty() && bname.front() == ' ') bname.erase(0, 1);
        while (!bname.empty() && bname.back() == ' ') bname.pop_back();
        data.branches.push_back({bname, is_cur, reltime});
    }

    return data;
}

bool git_is_clean(const std::string& repo_root) {
    std::string out = exec_git_cmd(repo_root, "status --porcelain=v1");
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 2) {
            char x = line[0];
            char y = line[1];
            if (x != '?' || y != '?') {
                return false;
            }
        }
    }
    return true;
}

bool git_stage_file(const std::string& repo_root, const std::string& rel_path) {
    std::string cmd = "add -- \"" + rel_path + "\"";
    return run_git_cmd_status(repo_root, cmd) == 0;
}

bool git_unstage_file(const std::string& repo_root, const std::string& rel_path) {
    std::string cmd = "reset -q HEAD -- \"" + rel_path + "\"";
    int res = run_git_cmd_status(repo_root, cmd);
    if (res != 0) {
        cmd = "rm --cached -q -- \"" + rel_path + "\"";
        res = run_git_cmd_status(repo_root, cmd);
    }
    return res == 0;
}

bool git_checkout_branch(const std::string& repo_root, const std::string& branch_name) {
    std::string cmd = "checkout -q \"" + branch_name + "\"";
    return run_git_cmd_status(repo_root, cmd) == 0;
}

static GitCommandResult exec_git_capture(const std::string& repo_root, const std::string& args) {
    GitCommandResult res;
    std::string cmd = "git -C \"" + repo_root + "\" " + args + " 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        res.summary = "Failed to spawn git process";
        return res;
    }

    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty()) {
            res.raw_lines.push_back(line);
        }
    }
    int status = pclose(fp);
#ifdef _WIN32
    res.exit_code = status;
#else
    if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
    } else {
        res.exit_code = status;
    }
#endif
    res.success = (res.exit_code == 0);
    return res;
}

GitCommandResult git_stash_push_info(const std::string& repo_root) {
    auto res = exec_git_capture(repo_root, "stash push");
    if (res.success) {
        for (const auto& line : res.raw_lines) {
            size_t idx = line.find("Saved working directory and index state ");
            if (idx != std::string::npos) {
                res.summary = "Saved " + line.substr(idx + 40);
                return res;
            }
        }
        res.summary = "Stashed working changes";
    } else {
        std::string err = !res.raw_lines.empty() ? res.raw_lines[0] : "Failed to stash changes";
        res.summary = "Failed to stash: " + err;
    }
    return res;
}

GitCommandResult git_stash_pop_info(const std::string& repo_root, int stash_idx) {
    std::string target = "stash@{" + std::to_string(stash_idx) + "}";
    auto res = exec_git_capture(repo_root, "stash pop " + target);

    for (const auto& line : res.raw_lines) {
        if (line.find("CONFLICT") != std::string::npos) {
            size_t in_pos = line.find(" in ");
            std::string conflict_file = (in_pos != std::string::npos) ? line.substr(in_pos + 4) : "files";
            res.summary = "Conflict in " + conflict_file + " (" + target + " retained)";
            res.success = false;
            return res;
        }
    }

    if (res.success) {
        int mod_files = 0;
        bool dropped = false;
        for (const auto& line : res.raw_lines) {
            if (line.find("modified:") != std::string::npos || line.find("Auto-merging") != std::string::npos) {
                mod_files++;
            }
            if (line.find("Dropped refs/stash") != std::string::npos || line.find("Dropped stash") != std::string::npos) {
                dropped = true;
            }
        }
        res.summary = "Popped " + target;
        if (mod_files > 0) res.summary += " (" + std::to_string(mod_files) + (mod_files == 1 ? " file" : " files") + ")";
        if (dropped) res.summary += " - dropped";
        return res;
    }

    for (const auto& line : res.raw_lines) {
        if (line.find("error:") != std::string::npos) {
            res.summary = line;
            return res;
        }
    }

    std::string err = !res.raw_lines.empty() ? res.raw_lines[0] : ("Failed to pop " + target);
    res.summary = err;
    return res;
}

GitCommandResult git_stash_drop_info(const std::string& repo_root, int stash_idx) {
    std::string target = "stash@{" + std::to_string(stash_idx) + "}";
    auto res = exec_git_capture(repo_root, "stash drop " + target);
    if (res.success) {
        res.summary = "Dropped " + target;
    } else {
        std::string err = !res.raw_lines.empty() ? res.raw_lines[0] : ("Failed to drop " + target);
        res.summary = err;
    }
    return res;
}

bool git_stash_push(const std::string& repo_root) {
    return git_stash_push_info(repo_root).success;
}

bool git_stash_pop(const std::string& repo_root, int stash_idx) {
    return git_stash_pop_info(repo_root, stash_idx).success;
}

bool git_stash_drop(const std::string& repo_root, int stash_idx) {
    return git_stash_drop_info(repo_root, stash_idx).success;
}

std::vector<std::string> git_get_file_lines(const std::string& repo_root,
                                            const std::string& rev,
                                            const std::string& rel_path) {
    std::vector<std::string> lines;
    if (rev == "WORKING") {
        std::string full_path = (fs::path(repo_root) / rel_path).lexically_normal().string();
        std::ifstream in(full_path);
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
        return lines;
    }

    std::string arg;
    if (rev == "INDEX") {
        arg = "show :\"" + rel_path + "\"";
    } else {
        arg = "show \"" + rev + ":" + rel_path + "\"";
    }
    std::string content = exec_git_cmd(repo_root, arg);
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}