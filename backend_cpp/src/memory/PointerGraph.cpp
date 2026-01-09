#include "memory/PointerGraph.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace code_assistance {

namespace fs = std::filesystem;

PointerGraph::PointerGraph(const std::string& storage_path, int dimension)
    : storage_path_(storage_path), dimension_(dimension) {
    
    vector_store_ = std::make_unique<FaissVectorStore>(dimension);
    load(); // Auto-load on startup
}

PointerGraph::~PointerGraph() {
    save(); // Auto-save on shutdown
}

std::string PointerGraph::generate_uuid() {
    // Simple timestamp + random UUID generation
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::stringstream ss;
    ss << "node_" << now << "_" << (rand() % 9999);
    return ss.str();
}

std::string PointerGraph::add_node(const std::string& content, 
                                   NodeType type, 
                                   const std::string& parent_id, 
                                   const std::vector<float>& embedding,
                                   const std::unordered_map<std::string, std::string>& metadata) {
    
    std::unique_lock lock(data_mutex_);

    PointerNode node;
    node.id = generate_uuid();
    node.type = type;
    node.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    node.content = content;
    node.parent_id = parent_id;
    node.metadata = metadata;

    // 1. Link Graph (Parent -> Child)
    if (!parent_id.empty() && nodes_.count(parent_id)) {
        nodes_[parent_id].children_ids.push_back(node.id);
    }

    // 2. Link Vector (If embedding provided)
    if (!embedding.empty()) {
        // Create a temporary CodeNode wrapper for the existing FaissVectorStore API
        // In Phase 3, we might refactor FaissVectorStore to be more generic, 
        // but for now, we adapt to it.
        auto wrapper_node = std::make_shared<CodeNode>();
        wrapper_node->id = node.id; // Use same ID
        wrapper_node->content = content;
        wrapper_node->embedding = embedding;
        
        vector_store_->add_nodes({wrapper_node});
        
        // Retrieve the internal ID assigned by FAISS (Assuming standard sequential insert)
        // A robust way requires FaissVectorStore to return IDs, but for now we calculate:
        long internal_id = vector_store_->get_all_nodes().size() - 1; 
        node.faiss_id = internal_id;
        faiss_to_uuid_[internal_id] = node.id;
    }

    // 3. Store
    nodes_[node.id] = node;
    
    // Auto-save every 10 nodes or if critical
    if (nodes_.size() % 10 == 0) save();

    return node.id;
}

void PointerGraph::update_metadata(const std::string& node_id, const std::string& key, const std::string& value) {
    std::unique_lock lock(data_mutex_);
    if (nodes_.count(node_id)) {
        nodes_[node_id].metadata[key] = value;
    }
}

std::vector<PointerNode> PointerGraph::semantic_search(const std::vector<float>& query_vec, int k) {
    std::shared_lock lock(data_mutex_);
    
    // Use existing HNSW search
    auto results = vector_store_->search(query_vec, k);
    
    std::vector<PointerNode> pointer_results;
    for (const auto& res : results) {
        // Resolve FAISS result back to PointerNode
        // Since we synced IDs in add_node, we can look up by ID string
        if (nodes_.count(res.node->id)) {
            pointer_results.push_back(nodes_.at(res.node->id));
        }
    }
    return pointer_results;
}

std::vector<PointerNode> PointerGraph::get_children(const std::string& node_id) {
    std::shared_lock lock(data_mutex_);
    std::vector<PointerNode> children;
    if (nodes_.count(node_id)) {
        for (const auto& child_id : nodes_.at(node_id).children_ids) {
            if (nodes_.count(child_id)) {
                children.push_back(nodes_.at(child_id));
            }
        }
    }
    return children;
}

std::vector<PointerNode> PointerGraph::get_trace(const std::string& end_node_id) {
    std::shared_lock lock(data_mutex_);
    std::vector<PointerNode> trace;
    std::string curr = end_node_id;
    
    // Walk up the parent chain
    while (!curr.empty() && nodes_.count(curr)) {
        trace.push_back(nodes_.at(curr));
        curr = nodes_.at(curr).parent_id;
        
        // Safety break for cycles (though logic shouldn't allow them)
        if (trace.size() > 50) break; 
    }
    // Reverse to get Chronological order
    std::reverse(trace.begin(), trace.end());
    return trace;
}

std::vector<PointerNode> PointerGraph::query_by_metadata(const std::string& key, const std::string& value) {
    std::shared_lock lock(data_mutex_);
    std::vector<PointerNode> matches;
    for (const auto& [id, node] : nodes_) {
        if (node.metadata.count(key) && node.metadata.at(key) == value) {
            matches.push_back(node);
        }
    }
    return matches;
}

void PointerGraph::save() {
    // 1. Save Vector Index
    if (!fs::exists(storage_path_)) fs::create_directories(storage_path_);
    vector_store_->save(storage_path_);

    // 2. Save Graph Structure (JSON)
    nlohmann::json j = nlohmann::json::array();
    for (const auto& [id, node] : nodes_) {
        j.push_back(node.to_json());
    }
    
    std::ofstream f(fs::path(storage_path_) / "graph.json");
    f << j.dump(2);
}

void PointerGraph::load() {
    // 1. Load Vector Index
    try {
        if (fs::exists(fs::path(storage_path_) / "faiss.index")) {
            vector_store_->load(storage_path_);
        }
    } catch (const std::exception& e) {
        spdlog::error("⚠️ Failed to load Vector Store: {}", e.what());
    }

    // 2. Load Graph Structure
    fs::path graph_path = fs::path(storage_path_) / "graph.json";
    if (fs::exists(graph_path)) {
        try {
            std::ifstream f(graph_path);
            nlohmann::json j;
            f >> j;
            
            nodes_.clear();
            faiss_to_uuid_.clear();
            
            for (const auto& item : j) {
                PointerNode node = PointerNode::from_json(item);
                nodes_[node.id] = node;
                if (node.faiss_id != -1) {
                    faiss_to_uuid_[node.faiss_id] = node.id;
                }
            }
            spdlog::info("🧠 Pointer Graph Loaded: {} nodes", nodes_.size());
        } catch (const std::exception& e) {
            spdlog::error("⚠️ Failed to load Graph JSON: {}", e.what());
        }
    }
}

}