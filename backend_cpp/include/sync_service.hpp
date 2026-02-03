#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include "code_graph.hpp"
#include "embedding_service.hpp"

namespace code_assistance {

namespace fs = std::filesystem;

struct FilterConfig {
    std::unordered_set<std::string> allowed_extensions;
    std::vector<std::string> blacklist; 
    std::vector<std::string> whitelist; 
};

struct SyncResult {
    std::vector<std::shared_ptr<CodeNode>> nodes;
    int updated_count = 0;
    int deleted_count = 0;
    std::vector<std::string> logs;
};

class SyncService {
public:
    explicit SyncService(std::shared_ptr<EmbeddingService> embedding_service);

    // Main sync entry point
    SyncResult perform_sync(
        const std::string& project_id,
        const std::string& source_dir,
        const std::string& storage_path, 
        const std::vector<std::string>& allowed_extensions,
        const std::vector<std::string>& ignored_paths,
        const std::vector<std::string>& included_paths
    );

    // Atomic file sync
    std::vector<std::shared_ptr<CodeNode>> sync_single_file(
        const std::string& project_id,
        const std::string& local_root,
        const std::string& storage_path,
        const std::string& relative_path
    );

    // Logic Gatekeepers (Only one declaration of each!)
    bool should_index(const fs::path& rel_path, const FilterConfig& cfg);
    
    void recursive_scan(
        const fs::path& current_dir,
        const fs::path& root_dir,
        const fs::path& storage_dir,
        const FilterConfig& cfg,
        std::vector<fs::path>& results
    );

private:
    std::shared_ptr<EmbeddingService> embedding_service_;

    // Internal Helpers
    std::string calculate_file_hash(const std::filesystem::path& file_path);
    std::unordered_map<std::string, std::string> load_manifest(const std::string& project_id);
    void save_manifest(const std::string& project_id, const std::unordered_map<std::string, std::string>& manifest);
    void generate_embeddings_batch(std::vector<std::shared_ptr<CodeNode>>& nodes, int batch_size = 50);
    void generate_tree_file(const fs::path& base_dir, const std::vector<fs::path>& files, const fs::path& output_file);
    std::unordered_map<std::string, std::shared_ptr<CodeNode>> load_existing_nodes(const std::string& storage_path);
    void update_file_context(const std::string& file_path, const std::string& content);
};

} // namespace code_assistance