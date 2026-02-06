#pragma once
#include "faiss_vector_store.hpp"
#include <string>
#include <vector>

namespace code_assistance {

struct RetrievalResult {
    std::shared_ptr<CodeNode> node;
    double graph_score;
    double final_score;
    int distance;
};

class RetrievalEngine {
public:
    explicit RetrievalEngine(std::shared_ptr<FaissVectorStore> store) : vector_store_(store) {}

    std::vector<RetrievalResult> retrieve(
        const std::string& query,
        const std::vector<float>& query_embedding,
        int max_nodes = 80,
        bool use_graph = true
    );
    
    std::string build_hierarchical_context(
        const std::vector<RetrievalResult>& candidates,
        size_t max_chars = 120000
    );

private:
    std::shared_ptr<FaissVectorStore> vector_store_;

    std::vector<RetrievalResult> exponential_graph_expansion(
        const std::vector<FaissSearchResult>& seed_nodes,
        int max_nodes,
        int max_hops,
        double alpha
    );
    
    void multi_dimensional_scoring(std::vector<RetrievalResult>& candidates, const std::string& query);
};

} // namespace code_assistance