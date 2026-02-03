#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "agent/AgentTypes.hpp" // 🚀 Full definition now available

namespace code_assistance {

class ContextManager {
    const size_t TOKEN_LIMIT = 100000;
    const size_t SAFE_CHAR_LIMIT = 250000;

public:
    std::string rank_and_prune(const ContextSnapshot& ctx) {
        std::string payload = "";

        // 1. Focal Code
        if (!ctx.raw_nodes.empty()) {
            payload += std::string("### FOCAL POINT\n") + ctx.raw_nodes[0].node->content + "\n";
        }

        // 2. Topology
        if (!ctx.architectural_map.empty()) {
            payload += std::string("### PROJECT TOPOLOGY\n") + ctx.architectural_map + "\n";
        }

        // 3. Experience Vault
        for (const auto& exp : ctx.experiences) {
            payload += std::string("### PREVIOUS FIX\n") + exp + "\n";
        }

        // 4. History (Surgical Truncation)
        size_t history_len = ctx.history.length();
        size_t start_pos = (history_len > 3000) ? (history_len - 3000) : 0;
        
        payload += std::string("### CHAT HISTORY\n") + ctx.history.substr(start_pos);

        if (payload.length() > SAFE_CHAR_LIMIT) {
            payload = payload.substr(0, SAFE_CHAR_LIMIT);
            payload += "\n\n[SYSTEM WARNING: Context Truncated due to Browser Bridge Limits]";
        }
        return payload;
    }
};

}