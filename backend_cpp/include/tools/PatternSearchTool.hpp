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
            "Recursively search for regex patterns. Returns file paths and matching lines. Best for finding usages/definitions.",
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
            
            // Security Checks
            if (!FileSystemTools::is_safe_path(root_str, target)) return "ERROR: Security Violation.";
            if (!FileSystemTools::is_path_allowed(project_id, target)) return "ERROR: Access Denied (Ignored Path).";
            if (!std::filesystem::exists(target)) return "ERROR: Path not found.";

            std::regex re;
            try {
                re = std::regex(regex_str, std::regex_constants::icase | std::regex_constants::ECMAScript);
            } catch (const std::regex_error& e) {
                return "ERROR: Invalid Regex Syntax: " + std::string(e.what());
            }

            std::stringstream result;
            int total_matches = 0;
            int files_with_matches = 0;
            const int MAX_MATCHES = 200; // Increased Limit

            auto search_file = [&](const std::filesystem::path& file_path) {
                std::ifstream file(file_path);
                std::string line;
                int line_num = 0;
                bool file_has_match = false;
                std::stringstream file_buffer;

                while (std::getline(file, line)) {
                    line_num++;
                    if (std::regex_search(line, re)) {
                        // Compact Output Format: "15: import java.util..."
                        file_buffer << "  " << line_num << ": " << line << "\n";
                        total_matches++;
                        file_has_match = true;
                    }
                    if (total_matches >= MAX_MATCHES) break;
                }

                if (file_has_match) {
                    files_with_matches++;
                    result << "📄 " << std::filesystem::relative(file_path, root_str).string() << ":\n";
                    result << file_buffer.str() << "\n";
                }
            };

            if (std::filesystem::is_directory(target)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(target)) {
                    if (total_matches >= MAX_MATCHES) break;
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        // Filter common code extensions to speed up
                        if (ext == ".java" || ext == ".cpp" || ext == ".h" || ext == ".hpp" || 
                            ext == ".py" || ext == ".ts" || ext == ".js" || ext == ".cs" || ext == ".json") {
                            search_file(entry.path());
                        }
                    }
                }
            } else {
                search_file(target);
            }

            if (total_matches == 0) return "NO MATCHES FOUND.";
            
            result << "\n[SUMMARY] Found " << total_matches << " matches in " << files_with_matches << " files.";
            if (total_matches >= MAX_MATCHES) result << " (Search limit reached)";
            
            return result.str();

        } catch (const std::exception& e) {
            return "ERROR: " + std::string(e.what());
        }
    }
};

}