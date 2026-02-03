#pragma once
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <memory>

namespace code_assistance {

// Renamed to avoid Windows Macro collisions (IGNORE/INCLUDE)
enum PathFlag : uint8_t {
    PF_NONE = 0,
    PF_IGNORE = 1 << 0,  // 0x01
    PF_INCLUDE = 1 << 1  // 0x02 (Overrides IGNORE)
};

class PrefixTrie {
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children;
        uint8_t flags = PathFlag::PF_NONE;
    };

    std::unique_ptr<Node> root;

public:
    PrefixTrie() : root(std::make_unique<Node>()) {}

    // O(L) Insertion
    void insert(const std::string& path, PathFlag flag) {
        Node* current = root.get();
        std::filesystem::path p(path);
        
        for (const auto& part : p) {
            std::string segment = part.string();
            if (segment == "." || segment.empty()) continue;
            
            // Auto-create path if missing
            if (current->children.find(segment) == current->children.end()) {
                current->children[segment] = std::make_unique<Node>();
            }
            current = current->children[segment].get();
        }
        // Mark the leaf node
        current->flags |= flag;
    }

    // O(L) Lookup - Returns the most specific rule found
    uint8_t check(const std::filesystem::path& path) const {
        const Node* current = root.get();
        uint8_t accumulated_flags = PathFlag::PF_NONE;

        // Traverse the path segments
        for (const auto& part : path) {
            std::string segment = part.string();
            
            auto it = current->children.find(segment);
            if (it == current->children.end()) {
                // If we fall off the trie, the last specific rule applies
                break; 
            }
            current = it->second.get();
            
            // Logic: If we hit a node with flags, update our state.
            if (current->flags != PathFlag::PF_NONE) {
                accumulated_flags = current->flags;
            }
        }
        
        return accumulated_flags;
    }
    
    void clear() {
        root = std::make_unique<Node>();
    }
};

}