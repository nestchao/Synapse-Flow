#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "faiss_vector_store.hpp"

namespace code_assistance {

namespace fs = std::filesystem;

struct MemoryRecallResult {
    std::string positive_hints; // "Try this..."
    std::string negative_warnings; // "Avoid this..."
    bool has_memories = false;
};

class MemoryVault {
public:
    MemoryVault(const std::string& storage_path, int dimension = 768) 
        : path_(storage_path) {
        // Reuse the robust FAISS wrapper
        store_ = std::make_shared<FaissVectorStore>(dimension);
        load();
    }

    // 🧠 STORE: Save a successful interaction
    void add_success(const std::string& situation, const std::string& solution, 
                     const std::vector<float>& embedding) {
        auto node = create_memory_node(situation, solution, embedding, 1.0); // 1.0 = Positive
        store_->add_nodes({node});
        save();
        spdlog::info("🧠 Experience Vault: Learned SUCCESS pattern. Total: {}", store_->get_all_nodes().size());
    }

    // ⛔ STORE: Save a failed attempt (Anti-Pattern)
    void add_failure(const std::string& situation, const std::string& failed_attempt, 
                     const std::vector<float>& embedding) {
        auto node = create_memory_node(situation, failed_attempt, embedding, -1.0); // -1.0 = Negative
        store_->add_nodes({node});
        save();
        spdlog::info("🧠 Experience Vault: Recorded FAILURE pattern. Total: {}", store_->get_all_nodes().size());
    }

    // 🧠 RECALL: Find relevant past experiences
    MemoryRecallResult recall(const std::vector<float>& query_vec) {
        MemoryRecallResult result;
        if (!store_ || store_->get_all_nodes().empty()) return result;

        // Search for top k most relevant memories
        auto results = store_->search(query_vec, 5); 
        if (results.empty()) return result;

        for (const auto& res : results) {
            // L2 Distance threshold: Lower is closer
            if (res.faiss_score < 1.4) { 
                 double valence = res.node->weights["valence"];
                 
                 if (valence > 0.5) {
                     result.positive_hints += "- [SUCCESS PATTERN] " + res.node->docstring + 
                                              " -> " + res.node->content + "\n";
                 } else if (valence < -0.5) {
                     result.negative_warnings += "- [AVOID] " + res.node->docstring + 
                                                 " -> " + res.node->content + " (Previously Failed)\n";
                 }
                 result.has_memories = true;
            }
        }
        
        return result;
    }

private:
    std::shared_ptr<CodeNode> create_memory_node(const std::string& situation, const std::string& action, 
                                                 const std::vector<float>& vec, double valence) {
        auto node = std::make_shared<CodeNode>();
        node->id = "MEM_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        node->name = (valence > 0) ? "Success_Entry" : "Failure_Entry";
        
        // Hijack CodeNode fields for memory
        node->docstring = situation; // The Trigger/Context
        node->content = action;      // The Action taken
        node->embedding = vec;
        node->weights["valence"] = valence; // +1 for Good, -1 for Bad
        
        return node;
    }

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
                spdlog::warn("⚠️ Memory Vault corrupted. Resetting."); 
            }
        }
    }

    std::string path_;
    std::shared_ptr<FaissVectorStore> store_;
};

}