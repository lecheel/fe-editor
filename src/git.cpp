#include "git.hpp"
#include <git2.h>
#include <filesystem>
#include <string>
#include <vector>
#include <set>
#include <cstring>

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