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
    auto seeds = vector_store_->search(query_embedding, 200); 
    
    // 2. Expand
    auto expanded = exponential_graph_expansion(seeds, 200, 3, 0.5);
    
    // 3. Score
    multi_dimensional_scoring(expanded);
    
    // 4. Sort and filter
    std::sort(expanded.begin(), expanded.end(), [](const auto& a, const auto& b) {
        return a.final_score > b.final_score;
    });

    if (expanded.size() > max_nodes) {
        expanded.resize(max_nodes);
    }

    // --- TELEMETRY END ---
    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    // Update global atomic metric
    SystemMonitor::global_vector_latency_ms.store(duration);
    
    spdlog::info("⏱️ Retrieval Pipeline Time: {:.2f} ms", duration);

    if (!expanded.empty()) {
        spdlog::info("🎯 Retrieval Audit (Top 3):");
        for (size_t i = 0; i < std::min((size_t)3, expanded.size()); ++i) {
            spdlog::info("  [{}] Path: '{}' | Name: '{}' | Score: {:.4f}", 
                i+1, 
                expanded[i].node->file_path, 
                expanded[i].node->name, 
                expanded[i].final_score);
        }
    } else {
        spdlog::warn("⚠️ Retrieval returned ZERO results.");
    }

    return expanded;
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
        double s_weight = c.node->weights.count("structural") ? c.node->weights["structural"] : 0.5;
        c.final_score = c.graph_score * (0.8 + (s_weight * 0.2));
    }
}

} // namespace code_assistance