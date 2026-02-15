#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include "GraphTypes.hpp"
#include "faiss_vector_store.hpp" // Reuse your existing robust HNSW wrapper

namespace code_assistance {

class PointerGraph {
public:
    // Initialize with storage path and vector dimension (default Gemini=768)
    PointerGraph(const std::string& storage_path, int dimension = 768);
    ~PointerGraph();

    // --- WRITE OPERATIONS ---
    
    // Records a new event in the episodic memory
    // Returns the UUID of the new node
    std::string add_node(const std::string& content, 
                         NodeType type, 
                         const std::string& parent_id = "", 
                         const std::vector<float>& embedding = {},
                         const std::unordered_map<std::string, std::string>& metadata = {});

    // Updates metadata (e.g., marking a tool call as "failed" after execution)
    void update_metadata(const std::string& node_id, const std::string& key, const std::string& value);

    // --- READ OPERATIONS ---

    // Semantic Search: "Find me similar code/thoughts"
    std::vector<PointerNode> semantic_search(const std::vector<float>& query_vec, int k = 5);

    // Graph Traversal: "What happened after node X?"
    std::vector<PointerNode> get_children(const std::string& node_id);

    // Graph Trace: "Reconstruct the chain that led to node X" (Backwards walk)
    std::vector<PointerNode> get_trace(const std::string& end_node_id);

    // Metadata Filter: "Find all failed tool calls"
    std::vector<PointerNode> query_by_metadata(const std::string& key, const std::string& value);

    // Returns concatenated code snippets relevant to the query
    std::string get_relevant_context(const std::string& query, int max_chars = 4000);

    void clear();

    // --- PERSISTENCE ---
    void save();
    void load();

    size_t get_node_count() const { 
        std::shared_lock lock(data_mutex_);
        return nodes_.size(); 
    }

private:
    std::string storage_path_;
    int dimension_;
    
    // Dual Index System
    std::unique_ptr<FaissVectorStore> vector_store_; // HNSW Index
    std::unordered_map<std::string, PointerNode> nodes_; // Graph Adjacency
    std::unordered_map<long, std::string> faiss_to_uuid_; // Bridge Vector ID -> UUID

    mutable std::shared_mutex data_mutex_;

    std::string generate_uuid();

    void save_internal(); 
};

}