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
        auto results = vector_store_->search(query_vec, 3); // Top 3 relevant skills
        std::stringstream ss;
        
        if (!results.empty()) {
            ss << "### 🏢 BUSINESS CONTEXT & SKILLS (Strictly Follow These)\n";
            for (const auto& res : results) {
                // Threshold to ensure relevance
                if (res.faiss_score < 1.2) { 
                    ss << "SOURCE: " << res.node->name << "\n"
                       << "CONTENT:\n" << res.node->content << "\n"
                       << "--------------------------------------------------\n";
                }
            }
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