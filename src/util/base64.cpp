#include "util/base64.hpp"
#include <cstdint>

namespace {
constexpr char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

std::string base64_encode(const std::string& in) {
    std::string out;
    size_t len = in.size();
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (static_cast<uint8_t>(in[i])) << 16;
        if (i + 1 < len) b |= (static_cast<uint8_t>(in[i + 1])) << 8;
        if (i + 2 < len) b |= static_cast<uint8_t>(in[i + 2]);

        out.push_back(B64_CHARS[(b >> 18) & 0x3F]);
        out.push_back(B64_CHARS[(b >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? B64_CHARS[(b >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? B64_CHARS[b & 0x3F]        : '=');
    }
    return out;
}

std::string base64_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;

    for (unsigned char c : in) {
        int v = -1;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') break;
        else continue; // Ignore newlines, spaces, or tmux wrapping

        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}