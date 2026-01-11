#pragma once
#include "tools/ToolRegistry.hpp"
#include "tools/FileSystemTools.hpp"
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <sstream>

namespace code_assistance {

class PatternSearchTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {
            "pattern_search",
            "Search for text patterns (Regex) in a specific file. Returns line numbers and content. Use for large files to find specific functions/variables without reading the whole file.",
            "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"pattern\":{\"type\":\"string\"},\"context_lines\":{\"type\":\"integer\"}},\"required\":[\"path\",\"pattern\"]}"
        };
    }

    std::string execute(const std::string& args_json) override {
        try {
            auto j = nlohmann::json::parse(args_json);
            std::string project_id = j.value("project_id", "");
            std::string rel_path = j.value("path", "");
            std::string regex_str = j.value("pattern", "");
            int context_lines = j.value("context_lines", 0);

            std::string root_str = FileSystemTools::resolve_project_root(project_id);
            if (root_str.empty()) return "ERROR: Invalid Project ID.";

            std::filesystem::path target = std::filesystem::path(root_str) / rel_path;
            
            // ... (Security Checks) ...

            std::regex re;
            try {
                re = std::regex(regex_str, std::regex_constants::icase | std::regex_constants::ECMAScript);
            } catch (const std::regex_error& e) {
                return "ERROR: Invalid Regex Syntax: " + std::string(e.what());
            }

            std::stringstream result;
            int total_matches = 0;

            // 🚀 RECURSIVE SEARCH LOGIC
            auto search_file = [&](const std::filesystem::path& file_path) {
                std::ifstream file(file_path);
                std::string line;
                int line_num = 0;
                bool file_has_match = false;
                std::vector<std::string> window;

                while (std::getline(file, line)) {
                    line_num++;
                    if (std::regex_search(line, re)) {
                        if (!file_has_match) {
                            result << "\n📄 " << std::filesystem::relative(file_path, root_str).string() << ":\n";
                            file_has_match = true;
                        }
                        result << "  " << line_num << ": " << line << "\n";
                        total_matches++;
                    }
                }
            };

            if (std::filesystem::is_directory(target)) {
                // Recursive grep
                for (const auto& entry : std::filesystem::recursive_directory_iterator(target)) {
                    if (entry.is_regular_file()) {
                        // Optional: Filter by extension to speed up (skip .git, etc)
                        std::string ext = entry.path().extension().string();
                        if (ext == ".java" || ext == ".cpp" || ext == ".h" || ext == ".py" || ext == ".ts" || ext == ".js") {
                            search_file(entry.path());
                        }
                    }
                    if (total_matches > 50) break; // Limit
                }
            } else {
                search_file(target);
            }

            if (total_matches == 0) return "NO MATCHES FOUND.";
            
            if (total_matches > 50) result << "\n... (Limit reached) ...";
            return result.str();

        } catch (const std::exception& e) {
            return "ERROR: " + std::string(e.what());
        }
    }
};

}