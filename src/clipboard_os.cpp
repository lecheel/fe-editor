#include "clipboard_os.hpp"
#include "util/base64.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

void osc52_copy(const std::string& text) {
    if (text.empty()) return;
    std::string b64 = base64_encode(text);

    std::string seq;
    const char* tmux = std::getenv("TMUX");
    const char* term = std::getenv("TERM");
    bool in_tmux = (tmux != nullptr) || (term && strstr(term, "tmux") != nullptr);
    bool in_screen = (term && strstr(term, "screen") != nullptr && !in_tmux);

    if (in_tmux) {
        seq = "\033Ptmux;\033\033]52;c;" + b64 + "\007\033\\";
    } else if (in_screen) {
        seq = "\033P\033]52;c;" + b64 + "\007\033\\";
    } else {
        seq = "\033]52;c;" + b64 + "\007";
    }

#ifndef _WIN32
    int fd = open("/dev/tty", O_WRONLY | O_NOCTTY);
    if (fd >= 0) {
        ssize_t written = 0;
        while (written < static_cast<ssize_t>(seq.size())) {
            ssize_t n = write(fd, seq.data() + written, seq.size() - written);
            if (n <= 0) break;
            written += n;
        }
        close(fd);
        return;
    }
#endif
    printf("%s", seq.c_str());
    fflush(stdout);
}

std::string read_osc52_clipboard() {
#ifndef _WIN32
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return "";

    std::string query;
    const char* tmux = std::getenv("TMUX");
    const char* term = std::getenv("TERM");
    bool in_tmux = (tmux != nullptr) || (term && strstr(term, "tmux") != nullptr);
    bool in_screen = (term && strstr(term, "screen") != nullptr && !in_tmux);

    if (in_tmux) {
        query = "\033Ptmux;\033\033]52;c;?\007\033\\";
    } else if (in_screen) {
        query = "\033P\033]52;c;?\007\033\\";
    } else {
        query = "\033]52;c;?\007";
    }

    if (write(fd, query.data(), query.size()) <= 0) {
        close(fd);
        return "";
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    // The terminal may take a while to answer, and a large clipboard arrives
    // in many chunks. The old 50ms wait plus a 10 x 4KB read cap truncated the
    // reply, so only the first part of the clipboard (a few lines) was decoded
    // and pasted. Wait for the real terminator with an overall deadline.
    int ret = poll(&pfd, 1, 500);
    if (ret <= 0 || !(pfd.revents & POLLIN)) {
        close(fd);
        return "";
    }

    std::string resp;
    char buf[65536];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            resp.append(buf, n);
            // Only look for the terminator after the OSC 52 data start
            size_t osc = resp.find("]52;");
            if (osc != std::string::npos) {
                size_t semi = resp.find(';', osc + 4);
                if (semi != std::string::npos &&
                    resp.find_first_of("\007\033", semi + 1) != std::string::npos) {
                    break;
                }
            }
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pfd.revents = 0;
            if (poll(&pfd, 1, 100) <= 0) break;
        } else {
            break;
        }
    }
    close(fd);

    if (resp.empty()) return "";

    size_t osc_pos = resp.find("]52;");
    if (osc_pos == std::string::npos) return "";

    size_t second_semi = resp.find(';', osc_pos + 4);
    if (second_semi == std::string::npos) return "";

    size_t data_start = second_semi + 1;
    size_t data_end = resp.find_first_of("\007\033", data_start);
    if (data_end == std::string::npos) {
        data_end = resp.size();
    }

    std::string b64 = resp.substr(data_start, data_end - data_start);
    if (b64 == "?" || b64.empty()) return "";

    return base64_decode(b64);
#else
    return "";
#endif
}