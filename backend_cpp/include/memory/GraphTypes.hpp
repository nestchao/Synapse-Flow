#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <ctime>

namespace code_assistance {

enum class NodeType {
    PROMPT,         // User input
    TOOL_CALL,      // Action taken by Agent
    CONTEXT_CODE,   // Code snippet retrieved/read
    RESPONSE,       // Final answer or intermediate thought
    SYSTEM_THOUGHT, // Internal Monologue (Step-back reasoning)
    UNKNOWN
};

// Serialization Helper for Enum
inline std::string node_type_to_string(NodeType t) {
    switch(t) {
        case NodeType::PROMPT: return "PROMPT";
        case NodeType::TOOL_CALL: return "TOOL_CALL";
        case NodeType::CONTEXT_CODE: return "CONTEXT_CODE";
        case NodeType::RESPONSE: return "RESPONSE";
        case NodeType::SYSTEM_THOUGHT: return "SYSTEM_THOUGHT";
        default: return "UNKNOWN";
    }
}

inline NodeType string_to_node_type(const std::string& s) {
    if(s == "PROMPT") return NodeType::PROMPT;
    if(s == "TOOL_CALL") return NodeType::TOOL_CALL;
    if(s == "CONTEXT_CODE") return NodeType::CONTEXT_CODE;
    if(s == "RESPONSE") return NodeType::RESPONSE;
    if(s == "SYSTEM_THOUGHT") return NodeType::SYSTEM_THOUGHT;
    return NodeType::UNKNOWN;
}

struct PointerNode {
    std::string id;             // UUID (Time-sortable)
    NodeType type;
    long long timestamp;
    
    // --- The Graph ---
    std::string parent_id;      // The cause (e.g., Prompt -> Thought)
    std::vector<std::string> children_ids; // The effects (e.g., Thought -> Tool Call)
    
    // --- The Vector ---
    long faiss_id = -1;         // Link to HNSW Index (-1 if not indexed)
    
    // --- The Data ---
    std::string content;        // The actual text/code
    
    // --- The "Tags" (For Filtering) ---
    // e.g., {"status": "failed", "tool_name": "read_file"}
    std::unordered_map<std::string, std::string> metadata;

    // Serialization
    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"type", node_type_to_string(type)},
            {"timestamp", timestamp},
            {"parent_id", parent_id},
            {"children_ids", children_ids},
            {"faiss_id", faiss_id},
            {"content", content},
            {"metadata", metadata}
        };
    }

    static PointerNode from_json(const nlohmann::json& j) {
        PointerNode n;
        n.id = j.value("id", "");
        n.type = string_to_node_type(j.value("type", "UNKNOWN"));
        n.timestamp = j.value("timestamp", 0LL);
        n.parent_id = j.value("parent_id", "");
        n.children_ids = j.value("children_ids", std::vector<std::string>{});
        n.faiss_id = j.value("faiss_id", -1L);
        n.content = j.value("content", "");
        if(j.contains("metadata")) {
            n.metadata = j["metadata"].get<std::unordered_map<std::string, std::string>>();
        }
        return n;
    }
};

}