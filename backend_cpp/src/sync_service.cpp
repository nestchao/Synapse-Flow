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

// Case-insensitive, separator-agnostic path comparison
bool paths_are_equal(const fs::path& p1, const fs::path& p2) {
    std::string s1 = p1.string();
    std::string s2 = p2.string();
    std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
    std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
    
    // Normalize separators
    std::replace(s1.begin(), s1.end(), '\\', '/');
    std::replace(s2.begin(), s2.end(), '\\', '/');
    
    // Strip trailing slashes
    if (!s1.empty() && s1.back() == '/') s1.pop_back();
    if (!s2.empty() && s2.back() == '/') s2.pop_back();

    return s1 == s2;
}

// Check if child is inside parent (or equal)
bool is_inside(const fs::path& child, const fs::path& parent) {
    if (parent.empty()) return false;
    
    // Normalize both paths to remove dots and redundant slashes
    auto c = child.lexically_normal();
    auto p = parent.lexically_normal();

    auto it_c = c.begin();
    auto it_p = p.begin();

    while (it_p != p.end()) {
        // If parent has a trailing slash, lexically_normal might leave a "." segment. 
        // We skip it.
        if (it_p->string() == "." || it_p->string().empty()) {
            ++it_p;
            continue;
        }

        if (it_c == c.end()) return false;
        
        std::string s_c = it_c->string();
        std::string s_p = it_p->string();
        
        // Windows Case-Insensitivity
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

    // 1. Build the Trie structure from flat paths
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

    // 2. Recursive Lambda to print with standard box-drawing characters
    std::ofstream out(output_file);
    out << base_dir.filename().string() << "/\n";

    // Helper function for recursive drawing
    std::function<void(VisualNode&, std::string)> draw_node;
    draw_node = [&](VisualNode& node, std::string prefix) {
        auto it = node.children.begin();
        while (it != node.children.end()) {
            bool is_last = (std::next(it) == node.children.end());
            
            // Standard ASCII Tree Characters
            std::string connector = is_last ? "└── " : "├── ";
            
            // Render folder trailing slash if it has children
            std::string name = it->first;
            if (!it->second.children.empty()) {
                name += "/";
            }

            out << prefix << connector << name << "\n";

            // Prepare prefix for children
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

// backend_cpp/src/sync_service.cpp

void scan_directory_recursive(
    const fs::path& current_dir,
    const fs::path& root_dir,
    const fs::path& storage_dir,
    const std::unordered_set<std::string>& ext_set,
    const std::vector<std::string>& ignored_paths,
    const std::vector<std::string>& included_paths,
    std::vector<fs::path>& results
) {
    try {
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            const auto& path = entry.path();
            if (fs::equivalent(path, storage_dir)) continue;

            fs::path rel_fs = fs::relative(path, root_dir);
            std::string rel_str = rel_fs.generic_string(); // Always uses '/'

            // Check if explicitly ignored
            bool explicitly_ignored = false;
            for (const auto& ign : ignored_paths) {
                if (is_inside(rel_fs, fs::path(ign))) {
                    explicitly_ignored = true;
                    break;
                }
            }

            // Check exceptions
            bool is_explicit_exception = false;
            bool is_bridge_to_exception = false;
            for (const auto& inc : included_paths) {
                fs::path inc_path(inc);
                if (is_inside(rel_fs, inc_path)) { is_explicit_exception = true; break; }
                if (is_inside(inc_path, rel_fs)) { is_bridge_to_exception = true; }
            }

            if (entry.is_directory()) {
                bool enter = !explicitly_ignored || is_bridge_to_exception || is_explicit_exception;
                spdlog::info("DIR  | {} | Ignored: {} | Bridge: {} | Action: {}", 
                    rel_str, explicitly_ignored ? "YES" : "NO ", is_bridge_to_exception ? "YES" : "NO ", enter ? "ENTER" : "SKIP");
                
                if (enter) {
                    scan_directory_recursive(path, root_dir, storage_dir, ext_set, ignored_paths, included_paths, results);
                }
            } else {
                bool collect = !explicitly_ignored || is_explicit_exception;
                
                std::string ext = path.extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                bool ext_match = ext_set.empty() || ext_set.count(ext);

                if (collect && ext_match) {
                    spdlog::info("FILE | {} | Action: COLLECT", rel_str);
                    results.push_back(path);
                } else {
                    spdlog::info("FILE | {} | Action: SKIP (Ignored: {}, ExtMatch: {})", 
                        rel_str, explicitly_ignored ? "YES" : "NO ", ext_match ? "YES" : "NO ");
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Scanner error at {}: {}", current_dir.string(), e.what());
    }
}

void SyncService::generate_embeddings_batch(std::vector<std::shared_ptr<CodeNode>>& nodes, int batch_size) {
    spdlog::info("Generating embeddings for {} nodes...", nodes.size());
    for (size_t i = 0; i < nodes.size(); i += batch_size) {
        std::vector<std::string> texts;
        size_t end = std::min(i + batch_size, nodes.size());
        for (size_t j = i; j < end; ++j) {
            std::string safe = utf8_safe_substr(nodes[j]->content, 800);
            texts.push_back("Name: " + nodes[j]->name + " Code: " + safe);
        }
        try {
            auto embs = embedding_service_->generate_embeddings_batch(texts);
            for (size_t j = 0; j < embs.size(); ++j) nodes[i + j]->embedding = embs[j];
        } catch(...) {}
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
    const FilterConfig& cfg, // You might need to refactor FilterConfig to hold the Trie
    std::vector<fs::path>& results
) {
    // 1. Build Trie locally (In production, build this ONCE in the class constructor/update)
    // For this snippet, we assume 'trie_' is a member of SyncService populated from cfg.
    PrefixTrie trie;
    for(const auto& p : cfg.blacklist) trie.insert(p, PathFlag::IGNORE);
    for(const auto& p : cfg.whitelist) trie.insert(p, PathFlag::INCLUDE);

    try {
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            const auto& path = entry.path();
            if (fs::equivalent(path, storage_dir)) continue;

            fs::path rel_path = fs::relative(path, root_dir);
            
            // 🚀 ELITE O(1) LOOKUP
            uint8_t flag = trie.check(rel_path);
            
            bool is_ignored = (flag & PathFlag::IGNORE);
            bool is_included = (flag & PathFlag::INCLUDE);

            if (entry.is_directory()) {
                // Logic: Enter if NOT ignored, OR if explicit exception exists
                // Note: The Trie check returns the status of the *current* path.
                // We need to know if there's a deep exception.
                // For simplicity in Phase 2.1: We scan if not ignored OR if included.
                
                if (!is_ignored || is_included) {
                    recursive_scan(path, root_dir, storage_dir, cfg, results);
                }
            } 
            else if (entry.is_regular_file()) {
                // File Logic
                if (is_ignored && !is_included) continue;

                // Extension Check
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

    SyncResult result;
    auto manifest = load_manifest(project_id);
    auto existing_nodes_map = load_existing_nodes(storage_path_str);

    // 🚀 PHASE 1: PRE-FLIGHT SANITATION
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

    // 🚀 PHASE 2: PRUNING RECURSIVE SCAN
    std::vector<fs::path> files_to_process;
    // We call the specialized recursive scan that uses should_index internally
    this->recursive_scan(source_dir, source_dir, storage_dir, cfg, files_to_process);

    // 🚀 PHASE 3: DIFFERENTIAL PROCESSING
    std::unordered_map<std::string, std::string> new_manifest;
    std::vector<std::shared_ptr<CodeNode>> nodes_to_embed;
    std::ofstream full_context_file(storage_dir / "_full_context.txt");
    full_context_file << "### AGGREGATED SOURCE CONTEXT\n";

    code_assistance::elite::ASTBooster ast_parser;

    for (const auto& file_path : files_to_process) {
        std::string rel_path_str = fs::relative(file_path, source_dir).generic_string();
        std::string current_hash = calculate_file_hash(file_path);
        std::string old_hash = manifest.count(rel_path_str) ? manifest.at(rel_path_str) : "";
        
        // 1. Read Content
        std::ifstream file_in(file_path);
        std::string content((std::istreambuf_iterator<char>(file_in)), std::istreambuf_iterator<char>());
        
        // 🚀 APPEND TO FULL CONTEXT STREAM
        // This runs for EVERY file in the scan list to ensure the context file is complete
        full_context_file << "\n\n--- FILE: " << rel_path_str << " ---\n" << content << "\n";

        bool is_changed = (current_hash != old_hash);
        new_manifest[rel_path_str] = current_hash;

        // 2. Node Generation
        if (is_changed) {
            spdlog::info("🔼 UPDATE: {}", rel_path_str);
            result.logs.push_back("UPDATE: " + rel_path_str);
            
            std::vector<CodeNode> raw_nodes;
            fs::path p(rel_path_str);
            std::string ext = p.extension().string();

            // 🚀 HYBRID PARSING LOGIC
            // Use Tree-sitter for structured languages
            if (ext == ".cpp" || ext == ".hpp" || ext == ".py" || ext == ".ts" || ext == ".js") {
                raw_nodes = ast_parser.extract_symbols(rel_path_str, content);
                if(raw_nodes.empty()) {
                    // Fallback if AST extraction returns nothing (e.g. empty file)
                    raw_nodes = CodeParser::extract_nodes_from_file(rel_path_str, content);
                }
            } else {
                // Use Regex for others
                raw_nodes = CodeParser::extract_nodes_from_file(rel_path_str, content);
            }

            // Always add the file itself as a node for "Full Context" retrieval
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
            // Recover from existing map to avoid re-embedding
            for (const auto& [id, node] : existing_nodes_map) {
                if (node->file_path == rel_path_str) result.nodes.push_back(node);
            }
        }
    }

    full_context_file.close();

    // 🚀 PHASE 4: VECTOR & METADATA FINALIZATION
    if (!nodes_to_embed.empty()) {
        generate_embeddings_batch(nodes_to_embed, 200);
    }
    
    generate_tree_file(source_dir, files_to_process, storage_dir / "tree.txt");
    save_manifest(project_id, new_manifest);

    spdlog::info("✅ Mission Success: {} nodes indexed.", result.nodes.size());
    return result;
}

std::vector<std::shared_ptr<CodeNode>> SyncService::sync_single_file(
    const std::string& project_id,
    const std::string& local_root,
    const std::string& storage_path,
    const std::string& relative_path
) {
    fs::path full_path = fs::path(local_root) / relative_path;
    if (!fs::exists(full_path)) throw std::runtime_error("File not found locally");

    // 1. Read Content
    std::ifstream file(full_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // 2. Parse into Nodes
    auto raw_nodes = CodeParser::extract_nodes_from_file(relative_path, content);
    std::vector<std::shared_ptr<CodeNode>> nodes;
    
    // 3. Generate Embeddings (Atomic Batch)
    std::vector<std::string> texts_to_embed;
    for (auto& n : raw_nodes) {
        auto ptr = std::make_shared<CodeNode>(n);
        nodes.push_back(ptr);
        texts_to_embed.push_back("Name: " + ptr->name + " Code: " + utf8_safe_substr(ptr->content, 800));
    }

    auto embs = embedding_service_->generate_embeddings_batch(texts_to_embed);
    for (size_t i = 0; i < embs.size(); ++i) nodes[i]->embedding = embs[i];

    // 4. Update the storage .txt (for full context chat)
    fs::path target_txt = fs::path(storage_path) / "converted_files" / (relative_path + ".txt");
    fs::create_directories(target_txt.parent_path());
    std::ofstream out(target_txt);
    out << content;

    return nodes;
}

} // namespace code_assistance