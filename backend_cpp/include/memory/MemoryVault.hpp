#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "faiss_vector_store.hpp"

namespace code_assistance {

namespace fs = std::filesystem;

class MemoryVault {
public:
    MemoryVault(const std::string& storage_path, int dimension = 768) 
        : path_(storage_path) {
        // Reuse the robust FAISS wrapper we built for code
        store_ = std::make_shared<FaissVectorStore>(dimension);
        load();
    }

    // 🧠 STORE: Save a successful interaction
    void add_experience(const std::string& user_intent, const std::string& successful_action, 
                       const std::vector<float>& embedding, double quality_score) {
        auto node = std::make_shared<CodeNode>();
        // Unique ID based on timestamp
        node->id = "EXP_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        node->name = "Experience_Entry";
        
        // We hijack the CodeNode fields for memory storage:
        node->docstring = user_intent;       // The Trigger (Problem)
        node->content = successful_action;   // The Solution (Fix)
        node->embedding = embedding;         // The Vector DNA
        node->weights["quality"] = quality_score;
        
        store_->add_nodes({node});
        save();
        spdlog::info("🧠 Experience Vault: Synapse learned a new pattern. Total Memories: {}", store_->get_all_nodes().size());
    }

    // 🧠 RECALL: Find similar past problems
    std::string recall(const std::vector<float>& query_vec) {
        if (!store_ || store_->get_all_nodes().empty()) return "";

        // Search for the 2 most relevant memories
        auto results = store_->search(query_vec, 2); 
        if (results.empty()) return "";

        std::string memory_block = "\n### 🧠 LONG-TERM MEMORY (Past Successes)\n";
        bool found = false;

        for (const auto& res : results) {
            // L2 Distance threshold: Lower is closer. 
            // 1.5 is a loose match, 0.5 is an exact match.
            if (res.faiss_score < 1.3) { 
                 found = true;
                 memory_block += "- SITUATION: " + res.node->docstring + "\n"
                                 "  SOLUTION: " + res.node->content + "\n"
                                 "  (Confidence: " + std::to_string(res.node->weights["quality"]) + ")\n\n";
            }
        }
        
        return found ? memory_block : "";
    }

private:
    void save() {
        if (!fs::exists(path_)) fs::create_directories(path_);
        store_->save(path_);
    }

    void load() {
        if (fs::exists(path_ + "/faiss.index")) {
            try { 
                store_->load(path_); 
                spdlog::info("🧠 Memory Vault Loaded: {} items", store_->get_all_nodes().size());
            } catch(...) { 
                spdlog::warn("⚠️ Memory Vault corrupted or version mismatch. Starting fresh."); 
            }
        }
    }

    std::string path_;
    std::shared_ptr<FaissVectorStore> store_;
};

}