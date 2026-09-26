#include "log.hpp"
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace {
    FILE* g_file = nullptr;
    bool g_enabled = false;
}

namespace Log {

void init(bool enabled, const std::string& path) {
    g_enabled = enabled;
    if (!g_enabled) return;
    g_file = fopen(path.c_str(), "w");
    if (g_file) {
        setvbuf(g_file, nullptr, _IOLBF, 0);
        write("=== fe debug log started ===");
    }
}

void write(const char* fmt, ...) {
    if (!g_enabled || !g_file) return;
    time_t t = time(nullptr);
    struct tm* tmv = localtime(&t);
    char tbuf[16];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tmv);
    fprintf(g_file, "[%s] ", tbuf);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);

    fprintf(g_file, "\n");
}

void shutdown() {
    if (g_file) {
        write("=== fe debug log ended ===");
        fclose(g_file);
        g_file = nullptr;
    }
}

} // namespace Log