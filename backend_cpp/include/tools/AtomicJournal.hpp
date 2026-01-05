#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <spdlog/spdlog.h>

// 🚀 Make sure this file exists and ASTBooster is implemented!
#include "parser_elite.hpp" 

namespace code_assistance {
namespace fs = std::filesystem;

class AtomicJournal {
public:
    // 🛡️ Creates a backup of the file before surgery
    static bool backup(const std::string& filePath) {
        fs::path p(filePath);
        fs::path journalPath = p.string() + ".synapse_journal";
        try {
            if (fs::exists(p)) {
                fs::copy(p, journalPath, fs::copy_options::overwrite_existing);
                return true;
            }
            // New file creation; backup not needed but operation is valid
            return true; 
        } catch (const std::exception& e) {
            spdlog::error("🚨 Journal Backup Failed: {}", e.what());
            return false;
        }
    }

    // ✅ Confirms the surgery was successful and deletes the backup
    static void commit(const std::string& filePath) {
        try {
            fs::remove(filePath + ".synapse_journal");
        } catch (...) {}
    }

    // 🔄 Restores the file to the state before the failed surgery
    static void rollback(const std::string& filePath) {
        fs::path journalPath = filePath + ".synapse_journal";
        if (fs::exists(journalPath)) {
            try {
                fs::copy(journalPath, filePath, fs::copy_options::overwrite_existing);
                fs::remove(journalPath);
                spdlog::warn("🔄 Rollback triggered for: {}", filePath);
            } catch (const std::exception& e) {
                spdlog::critical("💥 ROLLBACK FAILED: {}. Manual repair required!", e.what());
            }
        }
    }

    // 🚀 THE INTEGRATED SURGERY LOGIC
    static bool apply_surgery_safe(const std::string& path, const std::string& new_code) {
        fs::path p(path);
        std::string ext = p.extension().string();

        // 🛑 STEP 1: MEMORY-ONLY VALIDATION
        // We validate via Tree-sitter BEFORE we touch the disk
        if (!validate_ast_integrity(new_code, ext)) {
            return false; // Rejection, no file touched
        }

        // 🛡️ STEP 2: JOURNAL (Backup)
        if (!backup(path)) return false;

        // ✍️ STEP 3: WRITE
        std::ofstream out(path, std::ios::trunc); // Text mode usually fine, add | std::ios::binary if needed
        if (!out.is_open()) {
            rollback(path);
            return false;
        }

        out << new_code;
        out.close();

        // ✅ STEP 4: COMMIT (Delete Backup)
        commit(path);
        return true;
    }

    static bool validate_ast_integrity(const std::string& code, const std::string& ext) {
        // Instantiate the Elite Parser
        code_assistance::elite::ASTBooster parser;
        
        // 1. Syntax Check via Tree-sitter
        if (!parser.validate_syntax(code, ext)) {
            spdlog::error("❌ AST REJECTION: Syntax error detected in proposed code.");
            return false;
        }

        // 2. Critical Heuristic: Prevent wiping files
        if (code.length() < 10 && ext != ".txt" && ext != ".md") {
            spdlog::warn("⚠️ AST WARNING: Proposed code is dangerously short/empty.");
            return false;
        }

        return true;
    }
};
}