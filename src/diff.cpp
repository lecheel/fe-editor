#include "diff.hpp"
#include <algorithm>

// Myers diff implementation producing grouped hunks.
// The algorithm computes the shortest edit script between two line vectors
// using the standard V-array approach, backtracks through the trace to
// produce a sequence of EQUAL/INSERT/DELETE edit operations, and then
// coalesces adjacent non-EQUAL operations into hunks.
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

int AlignedDiff::next_hunk_row(int cur_row) const {
    if (hunks.empty() || rows.empty()) return cur_row;
    for (const auto& h : hunks) {
        if (h.first_row > cur_row) {
            return h.first_row;
        }
    }
    return hunks.front().first_row;
}

int AlignedDiff::prev_hunk_row(int cur_row) const {
    if (hunks.empty() || rows.empty()) return cur_row;
    for (int i = static_cast<int>(hunks.size()) - 1; i >= 0; --i) {
        if (hunks[i].first_row < cur_row) {
            return hunks[i].first_row;
        }
    }
    return hunks.back().first_row;
}

AlignedDiff compute_aligned_diff(const std::vector<std::string>& left,
                                 const std::vector<std::string>& right) {
    AlignedDiff diff;
    diff.left_lines = left.empty() ? std::vector<std::string>{""} : left;
    diff.right_lines = right.empty() ? std::vector<std::string>{""} : right;

    const auto& a = diff.left_lines;
    const auto& b = diff.right_lines;
    int n = static_cast<int>(a.size());
    int m = static_cast<int>(b.size());

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
            AlignedRow row;
            row.left_idx = ops[i].a_idx;
            row.right_idx = ops[i].b_idx;
            row.hunk_idx = -1;
            diff.rows.push_back(row);
            i++;
            continue;
        }

        size_t j = i;
        std::vector<int> left_indices;
        std::vector<int> right_indices;

        while (j < ops.size() && ops[j].op != EQUAL) {
            if (ops[j].op == DELETE) {
                left_indices.push_back(ops[j].a_idx);
            } else if (ops[j].op == INSERT) {
                right_indices.push_back(ops[j].b_idx);
            }
            j++;
        }

        AlignedHunk hunk;
        hunk.id = static_cast<int>(diff.hunks.size());
        int hunk_idx = hunk.id;
        hunk.first_row = static_cast<int>(diff.rows.size());

        hunk.left_start = left_indices.empty() ? ((i > 0) ? ops[i - 1].a_idx + 1 : 0) : left_indices.front();
        hunk.left_count = static_cast<int>(left_indices.size());
        hunk.right_start = right_indices.empty() ? ((i > 0) ? ops[i - 1].b_idx + 1 : 0) : right_indices.front();
        hunk.right_count = static_cast<int>(right_indices.size());

        for (int l_idx : left_indices) {
            hunk.left_lines.push_back(a[l_idx]);
        }
        for (int r_idx : right_indices) {
            hunk.right_lines.push_back(b[r_idx]);
        }

        if (hunk.left_count > 0 && hunk.right_count > 0) {
            hunk.kind = HunkType::MODIFIED;
        } else if (hunk.right_count > 0) {
            hunk.kind = HunkType::ADDED;
        } else {
            hunk.kind = HunkType::DELETED;
        }

        int align_len = std::max(hunk.left_count, hunk.right_count);
        for (int k = 0; k < align_len; ++k) {
            AlignedRow row;
            row.left_idx = (k < hunk.left_count) ? left_indices[k] : -1;
            row.right_idx = (k < hunk.right_count) ? right_indices[k] : -1;
            row.hunk_idx = hunk_idx;
            diff.rows.push_back(row);
        }

        diff.hunks.push_back(std::move(hunk));
        i = j;
    }

    return diff;
}