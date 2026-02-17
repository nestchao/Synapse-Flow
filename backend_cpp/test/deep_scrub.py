import os
import stat

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
main_path = os.path.join(BASE, "src/main.cpp")
scrubber_path = os.path.join(BASE, "include/utils/Scrubber.hpp")

def apply_fix():
    # 1. Ensure Scrubber is "Nuclear"
    scrubber_code = """#pragma once
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
}"""
    os.chmod(scrubber_path, stat.S_IWRITE)
    with open(scrubber_path, "w", encoding="utf-8") as f:
        f.write(scrubber_code)

    # 2. Patch main.cpp to scrub the disk buffer
    if os.path.exists(main_path):
        os.chmod(main_path, stat.S_IWRITE)
        with open(main_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        
        # Look for the context loading logic
        old_loading = "ss << f.rdbuf();"
        new_loading = """std::string raw_content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                ss << scrub_json_string(raw_content);"""
        
        if old_loading in content:
            content = content.replace(old_loading, new_loading)
            with open(main_path, "w", encoding="utf-8") as f:
                f.write(content)
            print("✅ main.cpp: Context loading sanitized.")
        else:
            print("⚠️ Could not find loading logic. It might already be patched.")

if __name__ == "__main__":
    apply_fix()