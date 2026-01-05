#pragma once
#include "tools/ToolRegistry.hpp"
#include "tools/AtomicJournal.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace code_assistance {

class FileSurgicalTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {
            "apply_edit",
            "Safely edits a file with AST validation and atomic rollback. Input: {'path': 'string', 'content': 'string'}",
            "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}"
        };
    }

    std::string execute(const std::string& args_json) override {
        try {
            auto j = nlohmann::json::parse(args_json);
            std::string project_root = j.value("project_id", ""); 
            std::string rel_path = j.value("path", "");
            std::string new_content = j.value("content", "");
            
            // Normalize path
            if (project_root.empty() || rel_path.empty()) return "ERROR: Missing path.";
            std::filesystem::path target = std::filesystem::path(project_root) / rel_path;

            // 🚀 EXECUTE ATOMIC SURGERY
            // This handles: AST Validation -> Backup -> Write -> Commit/Rollback
            bool success = AtomicJournal::apply_surgery_safe(target.string(), new_content);

            if (success) {
                spdlog::info("🏗️ Surgery Successful: {}", target.string());
                return "SUCCESS: File updated safely. Integrity verified.";
            } else {
                spdlog::error("💥 Surgery Failed: {}", target.string());
                return "ERROR: Edit rejected by Safety Engine (Syntax Error or Write Failure).";
            }

        } catch (const std::exception& e) {
            return "ERROR: Surgical Tool Exception: " + std::string(e.what());
        }
    }
};

}