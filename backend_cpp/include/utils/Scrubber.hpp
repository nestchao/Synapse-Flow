#pragma once
#include <string>
namespace code_assistance {
inline std::string scrub_json_string(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (unsigned char c : str) {
        if (c == 0x09 || c == 0x0A || c == 0x0D || (c >= 32 && c <= 126)) {
            out += (char)c;
        } else {
            out += ' ';
        }
    }
    return out;
}
}