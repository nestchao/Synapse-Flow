#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "faiss_vector_store.hpp"
#include "embedding_service.hpp"

namespace code_assistance {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct SkillNode {
    std::string domain;
    std::string category;
    std::string content; // The actual rule/pattern text
    std::string source_file;
};

class SkillLibrary {
public:
    SkillLibrary(const std::string& metadata_root, std::shared_ptr<EmbeddingService> ai) 
        : root_path_(metadata_root), ai_(ai) {
        
        // Separate Vector Store for Skills (Domain Knowledge)
        // Dimension 768 for Gemini Embeddings
        vector_store_ = std::make_shared<FaissVectorStore>(768); 
        reload_skills();
    }

    void reload_skills() {
        if (!fs::exists(root_path_)) {
            fs::create_directories(root_path_);
            spdlog::warn("⚠️ Skill Library root created at: {}", root_path_);
            return;
        }

        std::vector<std::shared_ptr<CodeNode>> skill_nodes;
        int count = 0;

        for (const auto& entry : fs::recursive_directory_iterator(root_path_)) {
            if (entry.is_regular_file() && (entry.path().extension() == ".yaml" || entry.path().extension() == ".json")) {
                std::string content = read_file(entry.path().string());
                if (content.empty()) continue;

                auto node = std::make_shared<CodeNode>();
                node->id = "SKILL_" + entry.path().filename().string();
                node->name = entry.path().stem().string(); // e.g., "payment_processing"
                node->content = content;
                node->type = "BUSINESS_RULE";
                node->file_path = entry.path().string();
                
                // Generate Embedding for the Skill
                // In production, cache this to avoid re-embedding on every restart
                node->embedding = ai_->generate_embedding(content.substr(0, 1000)); 
                
                skill_nodes.push_back(node);
                count++;
            }
        }

        if (!skill_nodes.empty()) {
            vector_store_->add_nodes(skill_nodes);
            spdlog::info("🧠 Skill Library: Loaded {} business capability modules.", count);
        }
    }

    // Retrieve relevant business rules based on user query
    std::string retrieve_skills(const std::string& query, const std::vector<float>& query_vec) {
        // 1. Search Vector Store
        auto results = vector_store_->search(query_vec, 3); // Top 3
        std::stringstream ss;
        bool header_added = false;
        
        spdlog::info("🔍 [SKILL CHECK] Querying skills for: '{}'", query.substr(0, 50) + "...");

        if (!results.empty()) {
            for (const auto& res : results) {
                // 2. Strict Threshold (Lower is better in L2 distance)
                // 1.0 is a tight match, 1.4 is loose. 
                // Let's set it to 1.1 to avoid "Noise".
                if (res.faiss_score < 1.1) { 
                    
                    if (!header_added) {
                        ss << "### 🏢 BUSINESS CONTEXT & SKILLS (Strictly Follow)\n";
                        header_added = true;
                    }

                    // 3. Log what the Agent sees
                    spdlog::info("✅ [SKILL MATCH] File: {} | Score: {:.4f} (Accepted)", res.node->name, res.faiss_score);
                    
                    // 4. Inject Content (This is what the agent sees)
                    ss << "SOURCE: " << res.node->name << "\n"
                       << "RULES:\n" << res.node->content << "\n"
                       << "--------------------------------------------------\n";
                } else {
                    // Log rejected skills to prove we are filtering
                    spdlog::info("❌ [SKILL REJECT] File: {} | Score: {:.4f} (Too irrelevant)", res.node->name, res.faiss_score);
                }
            }
        } else {
            spdlog::info("⚪ [SKILL CHECK] No skills found in index.");
        }
        
        return ss.str();
    }

private:
    std::string root_path_;
    std::shared_ptr<EmbeddingService> ai_;
    std::shared_ptr<FaissVectorStore> vector_store_;

    std::string read_file(const std::string& path) {
        std::ifstream f(path);
        std::stringstream buffer;
        buffer << f.rdbuf();
        return buffer.str();
    }
};

}