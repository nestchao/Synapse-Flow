#pragma once
#include "tools/ToolRegistry.hpp"
#include "tools/AtomicJournal.hpp"
#include "tools/FileSystemTools.hpp" 
#include <nlohmann/json.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace code_assistance {

class FileSurgicalTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {
            "apply_edit",
            "Safely edits a file. Input: {'path': 'string', 'content': 'string'}",
            "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}"
        };
    }

    std::string execute(const std::string& args_json) override {
        try {
            auto j = nlohmann::json::parse(args_json);
            std::string project_id = j.value("project_id", ""); 
            std::string rel_path = j.value("path", "");
            std::string new_content = j.value("content", "");
            
            // 1. Resolve Root
            std::string root_str = FileSystemTools::resolve_project_root(project_id);
            if (root_str.empty()) return "ERROR: Invalid Project ID.";

            std::filesystem::path target = std::filesystem::path(root_str) / rel_path;

            // 2. Sandbox Check
            if (!FileSystemTools::is_safe_path(root_str, target)) {
                return "ERROR: Security Violation. Path traversal is not allowed.";
            }

            // 3. 🚀 FILTER CHECK (The Missing Piece)
            // This ensures we respect the ignored_paths from config.json
            if (!FileSystemTools::is_path_allowed(project_id, target)) {
                spdlog::warn("🛑 WRITE BLOCKED (Ignored Path): {}", target.string());
                return "ERROR: Permission Denied. You cannot write to folders in the ignored list.";
            }

            if (!target.parent_path().empty() && !std::filesystem::exists(target.parent_path())) {
                std::filesystem::create_directories(target.parent_path());
            }

            spdlog::info("💾 Attempting to write to: {}", target.string());

            bool success = AtomicJournal::apply_surgery_safe(target.string(), new_content);

            if (success) {
                spdlog::info("🏗️ Surgery Successful: {}", target.string());
                return "SUCCESS: File updated safely.";
            } else {
                spdlog::error("💥 Surgery Failed: {}", target.string());
                return "ERROR: Edit rejected by Safety Engine.";
            }

        } catch (const std::exception& e) {
            return "ERROR: Surgical Tool Exception: " + std::string(e.what());
        }
    }
};

}