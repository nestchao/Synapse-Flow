import os
import stat
import re

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
ae_path = os.path.join(BASE, "src/agent/AgentExecutor.cpp")

# The PERFECT version of the function
perfect_function = """
nlohmann::json extract_json(const std::string& raw) {
    std::string clean = raw;
    
    // 1. Prioritize Explicit Markdown JSON Blocks
    size_t md_start = clean.find("```json");
    if (md_start != std::string::npos) {
        size_t start = clean.find('\\n', md_start);
        if (start != std::string::npos) {
            size_t end = clean.find("```", start + 1);
            if (end != std::string::npos) {
                std::string content = clean.substr(start + 1, end - start - 1);
                return nlohmann::json::parse(code_assistance::scrub_json_string(content), nullptr, false);
            }
        }
    }

    // 2. Scan for VALID JSON start
    size_t json_start = std::string::npos;
    char start_char = '\\0';
    char end_char = '\\0';

    for (size_t i = 0; i < clean.length(); ++i) {
        char c = clean[i];
        if (c == '{' || c == '[') {
            for (size_t j = i + 1; j < clean.length(); ++j) {
                char next = clean[j];
                if (std::isspace(next)) continue;
                if (c == '{' && (next == '"' || next == '}')) {
                    json_start = i; start_char = '{'; end_char = '}'; goto found;
                }
                if (c == '[' && (next == '{' || next == '"' || next == ']' || std::isdigit(next))) {
                    json_start = i; start_char = '['; end_char = ']'; goto found;
                }
                break; 
            }
        }
    }
    found:;

    if (json_start == std::string::npos) return nlohmann::json::object();

    int balance = 0;
    bool in_string = false;
    bool escape = false;
    size_t json_end = std::string::npos;

    for (size_t i = json_start; i < clean.length(); ++i) {
        char c = clean[i];
        if (escape) { escape = false; continue; }
        if (c == '\\\\') { escape = true; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        
        if (!in_string) {
            if (c == start_char) balance++;
            else if (c == end_char) {
                balance--;
                if (balance == 0) {
                    json_end = i;
                    break;
                }
            }
        }
    }

    std::string json_str = (json_end != std::string::npos) 
        ? clean.substr(json_start, json_end - json_start + 1) 
        : clean.substr(json_start);

    return nlohmann::json::parse(code_assistance::scrub_json_string(json_str), nullptr, false);
}
"""

def apply_overwrite():
    if not os.path.exists(ae_path):
        print("❌ AgentExecutor.cpp not found")
        return

    os.chmod(ae_path, stat.S_IWRITE)
    with open(ae_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # Find the start and end of the current extract_json function
    # It starts with 'nlohmann::json extract_json' and ends with 'return nlohmann::json::object();\n}' 
    # or the last bracket of the parser logic.
    
    # Using regex to find the whole function block
    pattern = r"nlohmann::json extract_json\(const std::string& raw\) \{[\s\S]*?return nlohmann::json::(?:object\(\)|parse\(.*?\));\s*\}"
    
    if re.search(pattern, content):
        new_content = re.sub(pattern, perfect_function.strip(), content)
        
        # Ensure include
        if '#include "utils/Scrubber.hpp"' not in new_content:
            new_content = '#include "utils/Scrubber.hpp"\n' + new_content

        with open(ae_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        print("✅ AgentExecutor.cpp: Function overwritten with perfect, safe version.")
    else:
        print("⚠️ Could not locate function block with regex. Please check the file.")

if __name__ == "__main__":
    apply_overwrite()