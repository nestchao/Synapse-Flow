#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <map>
#include <sstream> 

#include "PrefixTrie.hpp"
#include "code_graph.hpp"
#include "sync_service.hpp"
#include "parser_elite.hpp" 
#include "embedding_service.hpp"

namespace code_assistance {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct VisualNode {
    std::map<std::string, VisualNode> children;
};

// --- UTILITIES ---

uintmax_t get_directory_size(const fs::path& dir) {
    uintmax_t size = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (fs::is_regular_file(entry)) {
            size += fs::file_size(entry);
        }
    }
    return size;
}

bool paths_are_equal(const fs::path& p1, const fs::path& p2) {
    std::string s1 = p1.string();
    std::string s2 = p2.string();
    std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
    std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
    
    std::replace(s1.begin(), s1.end(), '\\', '/');
    std::replace(s2.begin(), s2.end(), '\\', '/');
    
    if (!s1.empty() && s1.back() == '/') s1.pop_back();
    if (!s2.empty() && s2.back() == '/') s2.pop_back();

    return s1 == s2;
}

bool is_inside(const fs::path& child, const fs::path& parent) {
    if (parent.empty()) return false;
    
    auto c = child.lexically_normal();
    auto p = parent.lexically_normal();

    auto it_c = c.begin();
    auto it_p = p.begin();

    while (it_p != p.end()) {
        if (it_p->string() == "." || it_p->string().empty()) {
            ++it_p;
            continue;
        }

        if (it_c == c.end()) return false;
        
        std::string s_c = it_c->string();
        std::string s_p = it_p->string();
        
        std::transform(s_c.begin(), s_c.end(), s_c.begin(), ::tolower);
        std::transform(s_p.begin(), s_p.end(), s_p.begin(), ::tolower);
        
        if (s_c != s_p) return false;
        
        ++it_c;
        ++it_p;
    }
    return true;
}

// --- SYNC SERVICE IMPLEMENTATION ---

SyncService::SyncService(std::shared_ptr<EmbeddingService> embedding_service)
    : embedding_service_(embedding_service) {}

bool SyncService::should_index(const fs::path& rel_path, const FilterConfig& cfg) {
    std::string p_str = rel_path.generic_string();

    for (const auto& white : cfg.whitelist) {
        if (p_str == white) return true; 
    }

    for (const auto& black : cfg.blacklist) {
        if (p_str.find(black) == 0) return false; 
    }

    std::string ext = rel_path.extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    return cfg.allowed_extensions.count(ext) > 0;
}

std::unordered_map<std::string, std::shared_ptr<CodeNode>> 
SyncService::load_existing_nodes(const std::string& storage_path) {
    std::unordered_map<std::string, std::shared_ptr<CodeNode>> map;
    fs::path meta_path = fs::path(storage_path) / "vector_store" / "metadata.json";
    if (fs::exists(meta_path)) {
        try {
            std::ifstream f(meta_path);
            json j = json::parse(f);
            for (const auto& j_node : j) {
                auto node = std::make_shared<CodeNode>(CodeNode::from_json(j_node));
                map[node->id] = node;
            }
        } catch (...) {}
    }
    return map;
}

void SyncService::generate_tree_file(
    const fs::path& base_dir, 
    const std::vector<fs::path>& files, 
    const fs::path& output_file
) {
    VisualNode root;

    for (const auto& file_path : files) {
        std::string rel = fs::relative(file_path, base_dir).string();
        std::replace(rel.begin(), rel.end(), '\\', '/');
        
        std::stringstream ss(rel);
        std::string part;
        VisualNode* current = &root;
        
        while (std::getline(ss, part, '/')) {
            if (part.empty()) continue;
            current = &(current->children[part]);
        }
    }

    std::ofstream out(output_file);
    out << base_dir.filename().string() << "/\n";

    std::function<void(VisualNode&, std::string)> draw_node;
    draw_node = [&](VisualNode& node, std::string prefix) {
        auto it = node.children.begin();
        while (it != node.children.end()) {
            bool is_last = (std::next(it) == node.children.end());
            
            std::string connector = is_last ? "└── " : "├── ";
            
            std::string name = it->first;
            if (!it->second.children.empty()) {
                name += "/";
            }

            out << prefix << connector << name << "\n";

            std::string new_prefix = prefix + (is_last ? "    " : "│   ");
            draw_node(it->second, new_prefix);
            
            ++it;
        }
    };

    draw_node(root, "");
    out.close();
}

std::string SyncService::calculate_file_hash(const fs::path& file_path) {
    try {
        auto size = fs::file_size(file_path);
        auto time = fs::last_write_time(file_path).time_since_epoch().count();
        return std::to_string(size) + "-" + std::to_string(time);
    } catch (...) { return "err"; }
}

void SyncService::generate_embeddings_batch(std::vector<std::shared_ptr<CodeNode>>& nodes, int batch_size) {
    spdlog::info("Generating embeddings for {} nodes...", nodes.size());
    for (size_t i = 0; i < nodes.size(); i += batch_size) {
        
        // 🚀 FIX: Declare the variable here!
        std::vector<std::string> texts_to_embed; 
        
        size_t end = std::min(i + batch_size, nodes.size());
        for (size_t j = i; j < end; ++j) {
            // 🚀 IDENTITY INJECTION: Forces AI to learn the filename
            std::string identity_text = 
                "This is a " + nodes[j]->type + " named '" + nodes[j]->name + "' " +
                "defined in the file '" + nodes[j]->file_path + "'.\n" +
                "Logic Implementation:\n" + utf8_safe_substr(nodes[j]->content, 1200);
                            
            texts_to_embed.push_back(identity_text);
        }

        try {
            auto embs = embedding_service_->generate_embeddings_batch(texts_to_embed);
            // Re-map embeddings to nodes
            for (size_t j = 0; j < embs.size(); ++j) {
                if (i + j < nodes.size()) nodes[i + j]->embedding = embs[j];
            }
        } catch(...) {
            spdlog::error("   - Batch embedding failed at index {}", i);
        }
        spdlog::info("  - Embedded batch {}/{}", (i/batch_size)+1, (nodes.size()/batch_size)+1);
    }
}

std::unordered_map<std::string, std::string> SyncService::load_manifest(const std::string& project_id) {
    fs::path p = fs::path("data") / project_id / "manifest.json";
    if (!fs::exists(p)) return {};
    try { std::ifstream f(p); json j; f >> j; return j; } catch(...) { return {}; }
}

void SyncService::save_manifest(const std::string& project_id, const std::unordered_map<std::string, std::string>& m) {
    fs::path p = fs::path("data") / project_id / "manifest.json";
    fs::create_directories(p.parent_path());
    std::ofstream f(p); json j = m; f << j.dump(2);
}

void SyncService::recursive_scan(
    const fs::path& current_dir,
    const fs::path& root_dir,
    const fs::path& storage_dir,
    const FilterConfig& cfg,
    std::vector<fs::path>& results
) {
    PrefixTrie trie;
    // Updated enum constants
    for(const auto& p : cfg.blacklist) trie.insert(p, PathFlag::PF_IGNORE);
    for(const auto& p : cfg.whitelist) trie.insert(p, PathFlag::PF_INCLUDE);

    try {
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            const auto& path = entry.path();
            if (fs::equivalent(path, storage_dir)) continue;

            fs::path rel_path = fs::relative(path, root_dir);
            
            uint8_t flag = trie.check(rel_path);
            
            // Updated check
            bool is_ignored = (flag & PathFlag::PF_IGNORE);
            bool is_included = (flag & PathFlag::PF_INCLUDE);

            if (entry.is_directory()) {
                if (!is_ignored || is_included) {
                    recursive_scan(path, root_dir, storage_dir, cfg, results);
                }
            } 
            else if (entry.is_regular_file()) {
                if (is_ignored && !is_included) continue;

                std::string ext = path.extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                
                if (cfg.allowed_extensions.count(ext)) {
                    results.push_back(path);
                }
            }
        }
    } catch (...) {}
}

SyncResult SyncService::perform_sync(
    const std::string& project_id,
    const std::string& source_dir_str,
    const std::string& storage_path_str, 
    const std::vector<std::string>& allowed_extensions,
    const std::vector<std::string>& ignored_paths,
    const std::vector<std::string>& included_paths
) {
    fs::path source_dir = fs::absolute(source_dir_str);
    fs::path storage_dir = fs::absolute(storage_path_str);
    fs::path converted_files_dir = storage_dir / "converted_files";
    fs::create_directories(converted_files_dir);

    uintmax_t total_bytes = get_directory_size(source_dir);
    spdlog::info("📊 [PROJECT STATS] Path: {}", source_dir_str);
    spdlog::info("   - Total Folder Size: {:.2f} MB", (double)total_bytes / (1024.0 * 1024.0));

    SyncResult result;
    auto manifest = load_manifest(project_id);
    auto existing_nodes_map = load_existing_nodes(storage_path_str);

    FilterConfig cfg;
    cfg.blacklist = ignored_paths;
    cfg.whitelist = included_paths;
    for (auto ext : allowed_extensions) {
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        std::string clean_ext = ext;
        std::transform(clean_ext.begin(), clean_ext.end(), clean_ext.begin(), ::tolower);
        cfg.allowed_extensions.insert(clean_ext);
    }

    spdlog::info("🔍 Mission Start: {} | Filters: [E:{} I:{} W:{}]", 
                 project_id, cfg.allowed_extensions.size(), cfg.blacklist.size(), cfg.whitelist.size());

    std::vector<fs::path> files_to_process;
    this->recursive_scan(source_dir, source_dir, storage_dir, cfg, files_to_process);
    spdlog::info("   - Files Found: {}", files_to_process.size());

    std::unordered_map<std::string, std::string> new_manifest;
    std::vector<std::shared_ptr<CodeNode>> nodes_to_embed;
    std::ofstream full_context_file(storage_dir / "_full_context.txt");
    full_context_file << "### AGGREGATED SOURCE CONTEXT\n";

    code_assistance::elite::ASTBooster ast_parser;

    std::vector<std::pair<std::string, std::string>> files_to_preload;

    for (const auto& file_path : files_to_process) {
        std::string rel_path_str = fs::relative(file_path, source_dir).generic_string();
        std::string current_hash = calculate_file_hash(file_path);
        std::string old_hash = manifest.count(rel_path_str) ? manifest.at(rel_path_str) : "";
        
        std::ifstream file_in(file_path);
        std::string content((std::istreambuf_iterator<char>(file_in)), std::istreambuf_iterator<char>());
        
        full_context_file << "\n\n--- FILE: " << rel_path_str << " ---\n" << content << "\n";
        
        // 🚀 NEW: Queue for context preloading
        files_to_preload.push_back({rel_path_str, content});

        bool is_changed = (current_hash != old_hash);
        new_manifest[rel_path_str] = current_hash;

        if (is_changed) {
            spdlog::info("🔼 UPDATE: {}", rel_path_str);
            result.logs.push_back("UPDATE: " + rel_path_str);
            
            std::vector<CodeNode> raw_nodes;
            fs::path p(rel_path_str);
            std::string ext = p.extension().string();

            if (ext == ".cpp" || ext == ".hpp" || ext == ".py" || ext == ".ts" || ext == ".js") {
                raw_nodes = ast_parser.extract_symbols(rel_path_str, content);
                if(raw_nodes.empty()) {
                    raw_nodes = CodeParser::extract_nodes_from_file(rel_path_str, content);
                }
            } else {
                raw_nodes = CodeParser::extract_nodes_from_file(rel_path_str, content);
            }

            if (raw_nodes.empty() || raw_nodes[0].type != "file") {
                CodeNode file_node;
                file_node.name = p.filename().string();
                file_node.file_path = rel_path_str;
                file_node.id = rel_path_str;
                file_node.content = content;
                file_node.type = "file";
                file_node.weights["structural"] = 1.0;
                raw_nodes.push_back(file_node);
            }

            for (auto& n : raw_nodes) {
                auto ptr = std::make_shared<CodeNode>(n);
                result.nodes.push_back(ptr);
                nodes_to_embed.push_back(ptr);
            }
            result.updated_count++;
            
        } else {
            for (const auto& [id, node] : existing_nodes_map) {
                if (node->file_path == rel_path_str) result.nodes.push_back(node);
            }
        }
    }

    full_context_file.close();

    // 🚀 NEW: Preload all file contexts for fast completions
    spdlog::info("📦 Preloading {} file contexts for ghost text...", files_to_preload.size());
    auto preload_start = std::chrono::high_resolution_clock::now();
    
    for (const auto& [file_path, content] : files_to_preload) {
        code_assistance::preload_file_context(file_path, content);
    }
    
    auto preload_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - preload_start
    ).count();
    
    spdlog::info("✅ Context preload complete in {}ms", preload_elapsed);

    if (!nodes_to_embed.empty()) {
        generate_embeddings_batch(nodes_to_embed, 200);
    }
    
    generate_tree_file(source_dir, files_to_process, storage_dir / "tree.txt");
    save_manifest(project_id, new_manifest);

    spdlog::info("✅ [SYNC COMPLETE] Generated Nodes: {}", result.nodes.size());
    return result;
}

void SyncService::update_file_context(const std::string& file_path, const std::string& content) {
    // Invalidate old context
    code_assistance::invalidate_file_context(file_path);
    
    // Preload new context
    code_assistance::preload_file_context(file_path, content);
    
    spdlog::debug("🔄 Updated context for {}", file_path);
}

std::vector<std::shared_ptr<CodeNode>> SyncService::sync_single_file(
    const std::string& project_id,
    const std::string& local_root,
    const std::string& storage_path,
    const std::string& relative_path
) {
    fs::path full_path = fs::path(local_root) / relative_path;
    if (!fs::exists(full_path)) throw std::runtime_error("File not found locally");

    std::ifstream file(full_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto raw_nodes = CodeParser::extract_nodes_from_file(relative_path, content);
    
    std::vector<std::shared_ptr<CodeNode>> nodes;
    // 🚀 FIX: Declare the variable here!
    std::vector<std::string> texts_to_embed; 

    for (auto& n : raw_nodes) {
        auto ptr = std::make_shared<CodeNode>(n);
        nodes.push_back(ptr);

        // 🚀 IDENTITY INJECTION
        std::string identity_text = 
            "[FILE: " + n.file_path + "] " +
            "[SYMBOL: " + n.name + "] " +
            "Content: " + n.content;

        texts_to_embed.push_back(identity_text);
    }

    // Call AI Service
    auto embs = embedding_service_->generate_embeddings_batch(texts_to_embed);
    for (size_t i = 0; i < embs.size(); ++i) {
        if (i < nodes.size()) nodes[i]->embedding = embs[i];
    }

    // Save to converted_files
    fs::path target_txt = fs::path(storage_path) / "converted_files" / (relative_path + ".txt");
    fs::create_directories(target_txt.parent_path());
    std::ofstream out(target_txt);
    out << content;

    return nodes;
}

} // namespace code_assistance

