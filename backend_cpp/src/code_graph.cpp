#include "code_graph.hpp"
#include <regex>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <numeric>
#include <unordered_set>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace code_assistance {

namespace fs = std::filesystem; 
using json = nlohmann::json;

std::string sanitize_utf8(const std::string& str) {
    std::string safe_str;
    safe_str.reserve(str.size());
    for (size_t i = 0; i < str.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) safe_str += c;
        else if (c >= 0xC0) safe_str += c; 
        else if (c >= 0x80 && c < 0xC0) safe_str += c;
        else safe_str += '?';
    }
    return safe_str;
}

json CodeNode::to_json() const {
    try {
        return json{
            {"id", sanitize_utf8(id)},
            {"name", sanitize_utf8(name)},
            {"content", sanitize_utf8(content)}, 
            {"docstring", sanitize_utf8(docstring)},
            {"file_path", sanitize_utf8(file_path)},
            {"type", type},
            {"dependencies", dependencies},
            {"embedding", embedding},
            {"weights", weights},
            {"ai_summary", sanitize_utf8(ai_summary)},
            {"ai_quality_score", ai_quality_score}
        };
    } catch (...) { return json{{"id", "error"}}; }
}

CodeNode CodeNode::from_json(const json& j) {
    CodeNode node;
    auto safe_get = [&](const std::string& key) { return j.value(key, ""); };
    node.id = safe_get("id");
    node.name = safe_get("name");
    node.content = safe_get("content");
    node.docstring = safe_get("docstring");
    node.file_path = safe_get("file_path");
    node.type = safe_get("type");
    if (j.contains("dependencies")) node.dependencies = j["dependencies"].get<std::unordered_set<std::string>>();
    if (j.contains("embedding")) node.embedding = j["embedding"].get<std::vector<float>>();
    if (j.contains("weights")) node.weights = j["weights"].get<std::unordered_map<std::string, double>>();
    node.ai_summary = safe_get("ai_summary");
    node.ai_quality_score = j.value("ai_quality_score", 0.5);
    return node;
}

// --- ROBUST HYBRID PARSER ---
class BracketParser {
public:
    static std::vector<CodeNode> parse(const std::string& file_path, const std::string& content) {
        std::vector<CodeNode> nodes;
        std::istringstream stream(content);
        std::string line;
        
        std::string buffer;
        int brace_level = 0;
        bool in_function = false;
        std::string current_signature;
        std::unordered_set<std::string> file_imports;
        
        std::regex func_start_re(R"((?:class|struct|interface|function|const|let|var|void|int|auto)\s+([a-zA-Z0-9_:]+))");

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            std::string clean_line = line; 
            // Simple trim
            clean_line.erase(0, clean_line.find_first_not_of(" \t"));
            
            // 1. MANUAL IMPORT SCANNING (Reliable)
            if (clean_line.rfind("import ", 0) == 0) { // Starts with "import "
                size_t from_pos = clean_line.find("from");
                if (from_pos != std::string::npos) {
                    // Extract substring after 'from'
                    std::string after_from = clean_line.substr(from_pos + 4);
                    
                    // Find quotes
                    size_t first_quote = after_from.find_first_of("'\"");
                    size_t last_quote = after_from.find_last_of("'\"");
                    
                    if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
                        std::string path = after_from.substr(first_quote + 1, last_quote - first_quote - 1);
                        
                        // Clean Path Logic
                        size_t last_slash = path.find_last_of('/');
                        if (last_slash != std::string::npos) path = path.substr(last_slash + 1);

                        // size_t dot = path.find_last_of('.');
                        // if (dot != std::string::npos && dot > 0) path = path.substr(0, dot);
                        
                        file_imports.insert(path);
                        // DEBUG LOG
                        if(file_path.find("app.ts") != std::string::npos) {
                             spdlog::info("🔗 Import Detected in {}: {}", file_path, path);
                        }
                    }
                }
            }

            // 2. Brace Counting
            int open_braces = 0;
            int close_braces = 0;
            for (char c : clean_line) {
                if (c == '{') open_braces++;
                if (c == '}') close_braces++;
            }

            // 3. Function Extraction
            if (!in_function) {
                std::smatch match;
                if (open_braces > 0 && std::regex_search(clean_line, match, func_start_re)) {
                    in_function = true;
                    brace_level = 0;
                    current_signature = match[1].str();
                    buffer = line + "\n";
                    brace_level += (open_braces - close_braces);
                }
            } else {
                buffer += line + "\n";
                brace_level += (open_braces - close_braces);
                if (brace_level <= 0) {
                    CodeNode node;
                    node.name = current_signature;
                    node.file_path = file_path;
                    node.id = file_path + "::" + current_signature;
                    node.content = buffer;
                    node.type = "code_block";
                    node.weights = {{"structural", 0.7}};
                    node.dependencies = file_imports; 
                    nodes.push_back(node);
                    in_function = false;
                    buffer = "";
                }
            }
        }

        CodeNode file_node;
        file_node.name = fs::path(file_path).filename().string();
        file_node.file_path = file_path;
        file_node.id = file_path;
        file_node.content = content;
        file_node.type = "file";
        file_node.weights = {{"structural", 0.5}, {"specificity", 0.3}};
        file_node.dependencies = file_imports;
        nodes.push_back(file_node);

        return nodes;
    }
};

std::vector<CodeNode> CodeParser::extract_nodes_from_file(const std::string& file_path, const std::string& content) {
    return BracketParser::parse(file_path, content);
}

void CodeGraph::add_node(std::shared_ptr<CodeNode> node) {
    all_nodes_.push_back(node);
    name_to_node_map_[node->name] = node;
}

void CodeGraph::calculate_static_weights() {
    // Basic weight calculation
}

} // namespace code_assistance