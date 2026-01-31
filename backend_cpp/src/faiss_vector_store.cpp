#include "faiss_vector_store.hpp"
#include <faiss/IndexHNSW.h>
#include <faiss/index_io.h>
#include <faiss/impl/FaissAssert.h>
#include <vector>
#include <numeric>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace code_assistance {

struct IndexDeleter {
    void operator()(faiss::Index* idx) {
        delete idx;
    }
};

FaissVectorStore::FaissVectorStore(int dimension) : dimension_(dimension) {
    // 🚀 THE ACCELERATOR: 32 links per node. efConstruction=128.
    // This allows the search to 'jump' across the code graph.
    faiss::IndexHNSWFlat* hnsw_idx = new faiss::IndexHNSWFlat(dimension, 32);
    hnsw_idx->hnsw.efConstruction = 128; // High precision indexing
    hnsw_idx->hnsw.efSearch = 64;       // Fast retrieval
    
    index_.reset(hnsw_idx); 
    spdlog::info("🚀 HNSW Accelerator Core Primed. Dimension: {}", dimension);
}

FaissVectorStore::~FaissVectorStore() {
}

void FaissVectorStore::add_nodes(const std::vector<std::shared_ptr<CodeNode>>& nodes) {
    std::unique_lock lock(rw_mutex_);

    if (nodes.empty()) return;

    std::vector<float> vectors_flat;
    std::vector<const std::shared_ptr<CodeNode>*> node_pointers;

    for (const auto& node : nodes) {
        if (!node->embedding.empty()) {
            vectors_flat.insert(vectors_flat.end(), node->embedding.begin(), node->embedding.end());
            node_pointers.push_back(&node);
        }
    }

    if (vectors_flat.empty()) return;

    long start_idx = index_->ntotal;
    long num_to_add = node_pointers.size();
    
    faiss::fvec_renorm_L2(dimension_, num_to_add, vectors_flat.data());
    
    index_->add(num_to_add, vectors_flat.data());

    for (long i = 0; i < num_to_add; ++i) {
        long current_id = start_idx + i;
        auto node = *node_pointers[i];
        
        nodes_list_.push_back(node);
        id_to_node_map_[current_id] = node;
        name_to_id_map_[node->id] = current_id;
    }

    spdlog::info("✅ Added {} nodes to FAISS. Total: {}", num_to_add, index_->ntotal);
}

std::vector<FaissSearchResult> FaissVectorStore::search(const std::vector<float>& query_vector, int k) {
    std::shared_lock lock(rw_mutex_);

    if (index_->ntotal == 0) return {};

    std::vector<float> query_copy = query_vector;
    faiss::fvec_renorm_L2(dimension_, 1, query_copy.data());

    std::vector<float> scores(k);
    std::vector<faiss::idx_t> indices(k);

    index_->search(1, query_copy.data(), k, scores.data(), indices.data());

    std::vector<FaissSearchResult> results;
    for (int i = 0; i < k; ++i) {
        if (indices[i] == -1) continue;
        
        auto it = id_to_node_map_.find(indices[i]);
        if (it != id_to_node_map_.end()) {
            results.push_back({it->second, scores[i]});
        }
    }
    return results;
}

void FaissVectorStore::save(const std::string& path) const {
    std::shared_lock lock(rw_mutex_); 

    fs::path dir(path);
    fs::create_directories(dir);

    // Use .get() to pass raw pointer to FAISS function
    faiss::write_index(index_.get(), (dir / "faiss.index").string().c_str());

    json metadata = json::array();
    for(const auto& node : nodes_list_){
        metadata.push_back(node->to_json());
    }

    std::ofstream meta_file(dir / "metadata.json");
    meta_file << metadata.dump(2);
}

void FaissVectorStore::load(const std::string& path) {
    std::unique_lock lock(rw_mutex_);

    fs::path dir(path);
    
    // reset() deletes the old index and takes ownership of the new one
    faiss::Index* raw_index = faiss::read_index((dir / "faiss.index").string().c_str());
    index_.reset(raw_index);
    
    std::ifstream meta_file(dir / "metadata.json");
    json metadata = json::parse(meta_file);
    
    nodes_list_.clear();
    id_to_node_map_.clear();
    name_to_id_map_.clear();
    
    for (const auto& j_node : metadata) {
        nodes_list_.push_back(std::make_shared<CodeNode>(CodeNode::from_json(j_node)));
    }

    for (long i = 0; i < nodes_list_.size(); ++i) {
        id_to_node_map_[i] = nodes_list_[i];
        name_to_id_map_[nodes_list_[i]->id] = i;
    }
    spdlog::info("✅ Loaded FAISS index with {} nodes from {}", index_->ntotal, path);
}

const std::vector<std::shared_ptr<CodeNode>>& FaissVectorStore::get_all_nodes() const {
    return nodes_list_;
}

std::shared_ptr<CodeNode> FaissVectorStore::get_node_by_name(const std::string& name) const {
    std::shared_lock lock(rw_mutex_);
    
    auto it = name_to_id_map_.find(name);
    if (it != name_to_id_map_.end()) {
        auto node_it = id_to_node_map_.find(it->second);
        if (node_it != id_to_node_map_.end()) {
            return node_it->second;
        }
    }
    return nullptr;
}

} // namespace code_assistance