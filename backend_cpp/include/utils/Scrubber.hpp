#pragma once
#include <string>

namespace code_assistance {
inline std::string scrub_json_string(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (unsigned char c : str) {
        // Allow ONLY standard ASCII (32-126) + Tab (9) + Newline (10) + CR (13)
        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 126)) {
            out += (char)c;
        } else {
            out += ' '; // Blow away everything else
        }
    }
    return out;
}
}