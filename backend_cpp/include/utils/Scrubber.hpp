#pragma once
#include <string>
#include <algorithm>

namespace code_assistance {

// High-performance, JSON-safe string scrubber
inline std::string scrub_json_string(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (unsigned char c : str) {
        // 1. Allow standard whitespace (Tab, Newline, CR)
        if (c == 0x09 || c == 0x0A || c == 0x0D) {
            out += (char)c;
            continue;
        }
        // 2. Allow ONLY Standard Printable ASCII (Range 32 to 126)
        // This explicitly rejects the "High Bit" (bytes > 127) 
        // which are the cause of UTF-8 sequence crashes.
        if (c >= 32 && c <= 126) {
            out += (char)c;
            continue;
        }
        // 3. Replace any "Magic" or "Hidden" bytes with a safe space
        out += ' ';
    }
    return out;
}

}