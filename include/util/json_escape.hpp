#pragma once
#include <string>

// Escapes a string for safe inclusion as a JSON string value
// (handles ", \, \n, \r, \t). Does not add surrounding quotes.
std::string escape_json_value(const std::string& s);