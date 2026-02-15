#include "memory/PointerGraph.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include "utils/Scrubber.hpp"

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

    if (!parent_id.empty() && nodes_.count(parent_id)) {
        nodes_[parent_id].children_ids.push_back(node.id);
    }

    if (!embedding.empty()) {
        auto wrapper_node = std::make_shared<CodeNode>();
        wrapper_node->id = node.id;
        wrapper_node->content = content;
        wrapper_node->embedding = embedding;
        
        // ✅ FIX: Copy ALL metadata fields properly
        if(metadata.count("file_path")) 
            wrapper_node->file_path = metadata.at("file_path");
        if(metadata.count("node_name")) 
            wrapper_node->name = metadata.at("node_name");
        if(metadata.count("node_type"))  // ← ADD THIS
            wrapper_node->type = metadata.at("node_type");
        
        // Optional: Copy dependencies if present
        if(metadata.count("dependencies")) {
            std::istringstream ss(metadata.at("dependencies"));
            std::string dep;
            while(std::getline(ss, dep, ',')) {
                if(!dep.empty()) wrapper_node->dependencies.insert(dep);
            }
        }

        vector_store_->add_nodes({wrapper_node});
        
        long internal_id = vector_store_->get_all_nodes().size() - 1; 
        node.faiss_id = internal_id;
        faiss_to_uuid_[internal_id] = node.id;
    }

    nodes_[node.id] = node;
    
    if (nodes_.size() % 10 == 0) {
        // 🚀 FIX: Call the INTERNAL version because we already hold the lock
        save_internal(); 
    }

    return node.id;
}

std::string PointerGraph::get_relevant_context(const std::string& query, int max_chars) {
    // Placeholder: In a real high-frequency scenario, we can't run BERT/Gecko every 50ms.
    // We will stick to the "Project Cache" strategy for now, but filter it using the graph later.
    // For this specific turn, let's keep the Architecture Simple.
    
    return ""; // Placeholder to allow compilation
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
    std::shared_lock lock(data_mutex_); 
    save_internal();
    spdlog::info("💾 Pointer Graph Saved: {} nodes", nodes_.size());
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

void PointerGraph::clear() {
    std::unique_lock lock(data_mutex_); // 🛡️ Thread-safe wipe
    
    // 1. Wipe the episodic memory map
    nodes_.clear();
    
    // 2. Wipe the ID mapping
    faiss_to_uuid_.clear();
    
    // 3. Re-initialize the Vector Store to clear the FAISS index
    // This ensures that old vectors are completely removed from memory
    vector_store_ = std::make_unique<FaissVectorStore>(dimension_);
    
    spdlog::warn("🧠 [GRAPH WIPE] All episodic and semantic memory has been cleared.");
}

void PointerGraph::save_internal() {
    if (!fs::exists(storage_path_)) fs::create_directories(storage_path_);
    vector_store_->save(storage_path_);

    nlohmann::json j = nlohmann::json::array();
    for (auto& [id, node] : nodes_) {
        PointerNode scrubbed_node = node;
        scrubbed_node.content = code_assistance::scrub_json_string(node.content);
        for (auto& [key, val] : scrubbed_node.metadata) {
            val = code_assistance::scrub_json_string(val);
        }
        j.push_back(scrubbed_node.to_json());
    }
    
    std::ofstream f(fs::path(storage_path_) / "graph.json");
    f << j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}