#pragma once
#include <string>

// Standard RFC 4648 base64. Used for OSC 52 clipboard transport.
std::string base64_encode(const std::string& in);
std::string base64_decode(const std::string& in);