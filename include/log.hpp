#pragma once
#include <string>

namespace Log {
    void init(bool enabled, const std::string& path = "fe_debug.log");
    void write(const char* fmt, ...);
    void shutdown();
}

#define LOGD(...) Log::write(__VA_ARGS__)