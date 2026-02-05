#include "retrieval_engine.hpp"
#include <deque>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <chrono> 
#include "SystemMonitor.hpp" // Required for telemetry

namespace code_assistance {

std::vector<RetrievalResult> RetrievalEngine::retrieve(
    const std::string& query,
    const std::vector<float>& query_embedding,
    int max_nodes,
    bool use_graph)
{
    // --- TELEMETRY START ---
    auto start = std::chrono::high_resolution_clock::now();

    // 1. Search (Get seeds)
    size_t total_nodes = vector_store_->get_all_nodes().size();
    int k = (total_nodes < 10) ? (int)total_nodes : 20; 

    auto seeds = vector_store_->search(query_embedding, 20);
    
    // 2. Expand
    int hops = (total_nodes < 10) ? 1 : 2;
    auto expanded = exponential_graph_expansion(seeds, 50, hops, 0.9);
    
    // 3. Score
    multi_dimensional_scoring(expanded);
    
    // 4. Sort and filter
    std::sort(expanded.begin(), expanded.end(), [](const auto& a, const auto& b) {
        return a.final_score > b.final_score;
    });

    // 🚀 ADD DEDUPLICATION HERE:
    std::vector<RetrievalResult> unique_results;
    std::unordered_set<std::string> seen_ids;

    for (auto& res : expanded) {
        // Use a unique key: path + name
        std::string key = res.node->file_path + "::" + res.node->name;
        if (seen_ids.find(key) == seen_ids.end()) {
            unique_results.push_back(res);
            seen_ids.insert(key);
        }
    }

    if (unique_results.size() > max_nodes) {
        unique_results.resize(max_nodes);
    }

    // Update logging to use the unique list
    if (!unique_results.empty()) {
        spdlog::info("🎯 Retrieval Audit (Top 3 Unique):");
        for (size_t i = 0; i < std::min((size_t)3, unique_results.size()); ++i) {
            spdlog::info("  [{}] Path: '{}' | Name: '{}' | Score: {:.4f}", 
                i+1, unique_results[i].node->file_path, unique_results[i].node->name, unique_results[i].final_score);
        }
    }

    return unique_results;
}

std::string RetrievalEngine::build_hierarchical_context(
    const std::vector<RetrievalResult>& candidates,
    size_t max_chars)
{
    std::string context;
    std::unordered_set<std::string> included_files; 

    for (const auto& cand : candidates) {

        if (included_files.count(cand.node->file_path)) {
            continue; 
        }

        if (cand.node->type == "file") {
            included_files.insert(cand.node->file_path);
        }

        std::string entry = "\n\n# FILE: " + cand.node->file_path + 
                            " | NODE: " + cand.node->name + 
                            " (Type: " + cand.node->type + ")\n" +
                            std::string(50, '-') + "\n" +
                            cand.node->content + "\n" +
                            std::string(50, '-') + "\n";
        
        if (context.length() + entry.length() > max_chars) {
            break;
        }
        context += entry;
    }
    return context;
}

std::vector<RetrievalResult> RetrievalEngine::exponential_graph_expansion(
    const std::vector<FaissSearchResult>& seed_nodes,
    int max_nodes,
    int max_hops,
    double alpha)
{
    spdlog::info("Starting graph expansion with {} seed nodes", seed_nodes.size());

    std::unordered_map<std::string, RetrievalResult> visited;
    std::deque<std::tuple<std::shared_ptr<CodeNode>, int, double>> queue;

    for (const auto& seed : seed_nodes) {
        if (visited.find(seed.node->id) == visited.end()) {
            visited[seed.node->id] = {seed.node, seed.faiss_score, 0.0, 0};
            queue.emplace_back(seed.node, 0, seed.faiss_score);
        }
    }

    int scanned_count = visited.size(); 

    while (!queue.empty() && visited.size() < max_nodes) {
        auto [curr, dist, base_score] = queue.front();
        queue.pop_front();

        if (dist >= max_hops) continue;

        for (const auto& dep_name : curr->dependencies) {
            auto candidate_node = vector_store_->get_node_by_name(dep_name);

            scanned_count++; 

            if (candidate_node && visited.find(candidate_node->id) == visited.end()) {
                int new_dist = dist + 1;
                double new_score = base_score * std::exp(-alpha * new_dist);
                
                visited[candidate_node->id] = {candidate_node, new_score, 0.0, new_dist};
                queue.emplace_back(candidate_node, new_dist, new_score);
            }
        }
    }

    SystemMonitor::global_graph_nodes_scanned.store(scanned_count);
    
    std::vector<RetrievalResult> results;
    for(auto const& [key, val] : visited) {
        results.push_back(val);
    }
    spdlog::info("✅ Graph expansion complete. {} nodes selected.", results.size());
    return results;
}

void RetrievalEngine::multi_dimensional_scoring(std::vector<RetrievalResult>& candidates) {
    for (auto& c : candidates) {
        // 🚀 FIX: Stronger Type Boosting
        double type_boost = 1.0;
        if (c.node->type.find("function") != std::string::npos) type_boost = 2.0; // Massively reward functions
        if (c.node->type.find("class") != std::string::npos) type_boost = 1.5;

        double s_weight = c.node->weights.count("structural") ? c.node->weights.at("structural") : 0.5;
        
        // Final score is now extremely sensitive to semantic match + type
        c.final_score = c.graph_score * type_boost * (0.95 + s_weight * 0.05);
    }
}

} // namespace code_assistance