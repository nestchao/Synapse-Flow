#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "tools/ToolRegistry.hpp"

namespace code_assistance {

struct ProjectFilter {
    std::vector<std::string> allowed_extensions;
    std::vector<std::string> ignored_paths;
    std::vector<std::string> included_paths;
};

class FileSystemTools {
public:
    static ProjectFilter load_config(const std::string& project_id); // Changed arg name for clarity
    
    static std::string resolve_project_root(const std::string& project_id);

    static bool is_safe_path(const std::filesystem::path& root, const std::filesystem::path& target);

    static bool is_path_allowed(const std::string& project_id, const std::filesystem::path& target_path);

    static std::string list_dir_deep(
        const std::string& project_id, // Pass ID, resolve internally
        const std::string& sub_path, 
        const ProjectFilter& filter, 
        int max_depth = 2
    );

    static std::string read_file_safe(const std::string& project_id, const std::string& relative_path);
};

// 🔧 Tool Registry Wrappers
class ListDirTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {"list_dir", "Lists files. Input: {'path': 'string', 'depth': number}", 
                "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"depth\":{\"type\":\"number\"}}}"};
    }
    std::string execute(const std::string& args_json) override;
};

class ReadFileTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {"read_file", "Reads file content. Input: {'path': 'string'}", 
                "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}"};
    }
    std::string execute(const std::string& args_json) override;
};

}