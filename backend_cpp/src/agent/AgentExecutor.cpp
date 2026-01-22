#define NOMINMAX
#include <cpr/cpr.h>
#include "agent/AgentExecutor.hpp"
#include "LogManager.hpp"
#include "SystemMonitor.hpp" 
#include <regex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stack>
#include <unordered_set>
#include "parser_elite.hpp"
#include "tools/FileSystemTools.hpp"
#include "planning/ExecutionGuard.hpp"

namespace code_assistance {

namespace fs = std::filesystem;

// --- CONSTRUCTOR ---
AgentExecutor::AgentExecutor(
    std::shared_ptr<RetrievalEngine> engine,
    std::shared_ptr<EmbeddingService> ai,
    std::shared_ptr<SubAgent> sub_agent,
    std::shared_ptr<ToolRegistry> tool_registry,
    std::shared_ptr<MemoryVault> memory_vault

) : engine_(engine), ai_service_(ai), sub_agent_(sub_agent), tool_registry_(tool_registry), memory_vault_(memory_vault) {

    context_mgr_ = std::make_unique<ContextManager>();
    planning_engine_ = std::make_unique<PlanningEngine>();
}

// --- HELPERS (Kept same as your code) ---
// Enhanced extract_json with f-string repair
// Enhanced extract_json with f-string repair
nlohmann::json extract_json(const std::string& raw) {
    std::string clean = raw;
    
    // 1. Strip UI artifacts (code/JSON/download buttons) and Markdown blocks
    // Remove common UI text that appears before JSON in scraped content
    std::vector<std::string> ui_prefixes = {"code", "JSON", "json", "download", "Copy code"};
    for (const auto& prefix : ui_prefixes) {
        size_t pos = 0;
        while ((pos = clean.find(prefix, pos)) != std::string::npos) {
            // Only remove if it's at start of line or after newline
            if (pos == 0 || clean[pos - 1] == '\n') {
                clean.erase(pos, prefix.length());
                // Also remove trailing newline if present
                if (pos < clean.length() && clean[pos] == '\n') {
                    clean.erase(pos, 1);
                }
            } else {
                pos += prefix.length();
            }
        }
    }
    
    // Strip Markdown Code Blocks
    size_t code_start = clean.find("```json");
    if (code_start != std::string::npos) {
        size_t code_end = clean.find("```", code_start + 7);
        if (code_end != std::string::npos) {
            clean = clean.substr(code_start + 7, code_end - (code_start + 7));
        }
    } else {
        code_start = clean.find("```");
        if (code_start != std::string::npos) {
            size_t code_end = clean.find("```", code_start + 3);
            if (code_end != std::string::npos) {
                clean = clean.substr(code_start + 3, code_end - (code_start + 3));
            }
        }
    }

    // 2. Trim Whitespace
    clean.erase(0, clean.find_first_not_of(" \n\r\t"));
    clean.erase(clean.find_last_not_of(" \n\r\t") + 1);

    // 3. PRE-EXTRACTION: Get tool name BEFORE attempting parse
    //    This prevents corruption from affecting tool detection
    std::string extracted_tool = "";
    std::regex tool_re_safe(R"("tool"\s*:\s*"([a-zA-Z_]+)\")");
    std::smatch tool_match;
    if (std::regex_search(clean, tool_match, tool_re_safe)) {
        extracted_tool = tool_match[1].str();
        spdlog::debug("🔍 Pre-extracted tool name: {}", extracted_tool);
    }

    // 4. Try Strict Parse First
    try {
        return nlohmann::json::parse(clean);
    } catch (const std::exception& e) {
        spdlog::warn("⚠️ Strict JSON Parse Failed: {}", e.what());
    }

    // 4. AGGRESSIVE REPAIR: Fix Python code inside JSON strings
    std::string repaired = "";
    bool in_json_string = false;
    bool escaped = false;
    int brace_depth = 0;
    
    // Track if we're inside a "content" or "code" field value
    bool inside_code_field = false;
    std::string last_key = "";
    
    for (size_t i = 0; i < clean.length(); i++) {
        char c = clean[i];
        
        // Handle escape sequences
        if (escaped) {
            repaired += c;
            escaped = false;
            continue;
        }
        
        if (c == '\\') {
            repaired += c;
            escaped = true;
            continue;
        }
        
        // Track JSON structure depth
        if (!in_json_string) {
            if (c == '{') brace_depth++;
            if (c == '}') brace_depth--;
            
            // Detect "content" or "code" keys
            if (c == ':' && last_key.find("content") != std::string::npos) {
                inside_code_field = true;
            }
            if (c == ':' && last_key.find("code") != std::string::npos) {
                inside_code_field = true;
            }
            
            // Track last key
            if (c == '"') {
                size_t key_start = i + 1;
                size_t key_end = clean.find('"', key_start);
                if (key_end != std::string::npos && clean[key_end - 1] != '\\') {
                    last_key = clean.substr(key_start, key_end - key_start);
                }
            }
        }
        
        // Toggle string state
        if (c == '"') {
            // Check if we're ending a code field
            if (in_json_string && inside_code_field && brace_depth == 2) {
                // Peek ahead - if next char is comma or }, we're closing the field
                if (i + 1 < clean.length()) {
                    char next = clean[i + 1];
                    // Skip whitespace
                    size_t peek = i + 1;
                    while (peek < clean.length() && std::isspace(clean[peek])) peek++;
                    if (peek < clean.length()) {
                        char next_significant = clean[peek];
                        if (next_significant == ',' || next_significant == '}') {
                            inside_code_field = false;
                        }
                    }
                }
            }
            
            in_json_string = !in_json_string;
            repaired += c;
            continue;
        }
        
        // CRITICAL: If we're inside a code field's string value, escape unescaped quotes
        if (in_json_string && inside_code_field) {
            // Only escape DOUBLE quotes - single quotes are fine in JSON strings
            if (c == '"' && !escaped) {
                // This is an unescaped double quote inside Python code
                repaired += '\\';
                repaired += c;
                continue;
            }
            // Single quotes are valid inside JSON strings - don't escape them!
        }
        
        // Handle newlines
        if (in_json_string) {
            if (c == '\n') {
                repaired += "\\n";
                continue;
            } else if (c == '\r') {
                continue; // Skip
            } else if (c == '\t') {
                repaired += "\\t";
                continue;
            }
        }
        
        repaired += c;
    }

    // 5. Try parsing repaired JSON
    try {
        auto parsed = nlohmann::json::parse(repaired);
        spdlog::info("✅ JSON Repair Successful (Aggressive Mode)");
        return parsed;
    } catch (const std::exception& e) {
        spdlog::warn("⚠️ Repaired JSON Still Invalid: {}", e.what());
        // Log a sample of the repaired JSON for debugging
        std::string sample = repaired.length() > 300 ? repaired.substr(0, 300) + "..." : repaired;
        spdlog::debug("Repaired JSON sample: {}", sample);
    }

    // 6. NUCLEAR OPTION: Regex-based field extraction with Python-aware quote handling
    try {
        nlohmann::json fallback = nlohmann::json::object();
        
        // Use pre-extracted tool name if available
        if (!extracted_tool.empty()) {
            fallback["tool"] = extracted_tool;
            spdlog::info("🔧 Using pre-extracted tool name: {}", extracted_tool);
        } else {
            // Extract tool/name (handle both formats)
            std::regex tool_re(R"((?:"tool"|"name")\s*:\s*"([a-zA-Z_]+)\")");
            std::smatch match;
            if (std::regex_search(clean, match, tool_re)) {
                std::string tool_name = match[1].str();
                fallback["tool"] = tool_name;
                spdlog::info("🔧 Extracted tool name: {}", tool_name);
            }
        }

        // Extract path parameter (critical for file operations)
        std::regex path_re(R"("path"\s*:\s*"([^"]+)\")");
        std::smatch path_match;
        if (std::regex_search(clean, path_match, path_re)) {
            if (!fallback.contains("parameters")) {
                fallback["parameters"] = nlohmann::json::object();
            }
            fallback["parameters"]["path"] = path_match[1].str();
            spdlog::info("📁 Extracted path: {}", path_match[1].str());
        }

        // Extract "steps" array for propose_plan (CRITICAL FIX)
        if (extracted_tool == "propose_plan" || fallback.value("tool", "") == "propose_plan") {
            std::regex steps_re(R"("steps"\s*:\s*\[)");
            std::smatch steps_match;
            if (std::regex_search(clean, steps_match, steps_re)) {
                size_t start_pos = steps_match.position(0) + steps_match.length(0);
                
                // Find matching closing bracket
                int depth = 1;
                size_t end_pos = start_pos;
                bool in_str = false;
                bool esc = false;
                
                while (end_pos < clean.length() && depth > 0) {
                    char c = clean[end_pos];
                    
                    if (esc) {
                        esc = false;
                        end_pos++;
                        continue;
                    }
                    
                    if (c == '\\') {
                        esc = true;
                        end_pos++;
                        continue;
                    }
                    
                    if (c == '"') in_str = !in_str;
                    
                    if (!in_str) {
                        if (c == '[') depth++;
                        if (c == ']') depth--;
                    }
                    
                    end_pos++;
                }
                
                if (depth == 0) {
                    std::string steps_str = "[" + clean.substr(start_pos, end_pos - start_pos - 1) + "]";
                    try {
                        auto steps_json = nlohmann::json::parse(steps_str);
                        if (!fallback.contains("parameters")) {
                            fallback["parameters"] = nlohmann::json::object();
                        }
                        fallback["parameters"]["steps"] = steps_json;
                        spdlog::info("📋 Extracted steps array: {} items", steps_json.size());
                    } catch (const std::exception& e) {
                        spdlog::error("❌ Could not parse steps array: {}", e.what());
                    }
                }
            }
        }

        // Extract content field (most complex - contains Python code)
        std::regex content_start_re(R"("content"\s*:\s*")");
        std::smatch content_match;
        if (std::regex_search(clean, content_match, content_start_re)) {
            size_t start_pos = content_match.position(0) + content_match.length(0);
            
            // Find the closing quote by counting escape sequences
            size_t end_pos = start_pos;
            bool in_escape = false;
            int depth = 0; // For nested structures
            
            while (end_pos < clean.length()) {
                char c = clean[end_pos];
                
                if (in_escape) {
                    in_escape = false;
                    end_pos++;
                    continue;
                }
                
                if (c == '\\') {
                    in_escape = true;
                    end_pos++;
                    continue;
                }
                
                // Found unescaped quote - check if it's the field terminator
                if (c == '"') {
                    // Peek ahead to see if this closes the content field
                    size_t peek = end_pos + 1;
                    while (peek < clean.length() && std::isspace(clean[peek])) peek++;
                    
                    if (peek < clean.length()) {
                        char next = clean[peek];
                        // If followed by comma or }, this closes the field
                        if (next == ',' || next == '}') {
                            break;
                        }
                    }
                }
                
                end_pos++;
            }
            
            if (end_pos > start_pos && end_pos < clean.length()) {
                std::string content_raw = clean.substr(start_pos, end_pos - start_pos);
                
                // Unescape the content (reverse JSON escaping)
                std::string content_clean = "";
                bool esc = false;
                for (char c : content_raw) {
                    if (esc) {
                        if (c == 'n') content_clean += '\n';
                        else if (c == 't') content_clean += '\t';
                        else if (c == 'r') continue;
                        else content_clean += c;
                        esc = false;
                    } else if (c == '\\') {
                        esc = true;
                    } else {
                        content_clean += c;
                    }
                }
                
                if (!fallback.contains("parameters")) {
                    fallback["parameters"] = nlohmann::json::object();
                }
                fallback["parameters"]["content"] = content_clean;
                spdlog::info("📝 Extracted content: {} chars", content_clean.length());
            }
        }

        // Validate minimum viable structure
        if (fallback.contains("tool") && fallback.contains("parameters")) {
            spdlog::info("✅ JSON Repair via Nuclear Regex Extraction");
            return fallback;
        }

    } catch (const std::exception& e) {
        spdlog::error("❌ Regex fallback failed: {}", e.what());
    }

    // 7. CATASTROPHIC FAILURE
    spdlog::error("❌ JSON Parse FATAL - All methods exhausted");
    spdlog::error("Input (first 500 chars): {}", clean.substr(0, 500));
    
    // Log full input to file for debugging
    std::ofstream debug_file("debug_json_fail_" + std::to_string(std::time(nullptr)) + ".txt");
    debug_file << "ORIGINAL:\n" << raw << "\n\nCLEANED:\n" << clean;
    debug_file.close();
    
    return nlohmann::json::object();
}

std::string sanitize_for_prompt(const std::string& text) {
    std::string safe = text;
    // Replace f"..." with f'...'
    size_t pos = 0;
    while ((pos = safe.find("f\"", pos)) != std::string::npos) {
        size_t end = safe.find("\"", pos + 2);
        if (end != std::string::npos) {
            // Found an f-string with double quotes
            safe[pos + 1] = '\'';
            safe[end] = '\'';
            pos = end + 1;
        } else {
            break;
        }
    }
    return safe;
}

std::string load_full_context_file(const std::string& project_id) {
    std::string root = FileSystemTools::resolve_project_root(project_id);
    if (root.empty()) return "";
    
    // Check .study_assistant folder first
    fs::path full_ctx_path = fs::path(root) / ".study_assistant" / "converted_files" / "_full_context.txt";
    
    // Fallback to data folder
    if (!fs::exists(full_ctx_path)) {
        full_ctx_path = fs::path("data") / project_id / "_full_context.txt";
    }

    if (fs::exists(full_ctx_path)) {
        std::ifstream f(full_ctx_path);
        std::stringstream buffer;
        buffer << f.rdbuf();
        return buffer.str();
    }
    return "";
}

std::string AgentExecutor::find_project_root() {
    fs::path p = fs::current_path();
    while (p.has_parent_path()) {
        if (fs::exists(p / "src") || fs::exists(p / ".git")) return p.string();
        p = p.parent_path();
    }
    return fs::current_path().string();
}

void AgentExecutor::notify(::grpc::ServerWriter<::code_assistance::AgentResponse>* w, 
                            const std::string& phase, 
                            const std::string& msg, 
                            double duration_ms) {
    if (w) {
        ::code_assistance::AgentResponse res;
        res.set_phase(phase);
        res.set_payload(msg);
        w->Write(res);
    }
    code_assistance::LogManager::instance().add_trace({"AGENT", "", phase, msg, duration_ms});
}

std::shared_ptr<SkillLibrary> AgentExecutor::get_skill_library(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(skill_mutex_);
    
    if (skill_libraries_.find(project_id) == skill_libraries_.end()) {
        std::string root_str = FileSystemTools::resolve_project_root(project_id);
        
        // Default to data folder if resolution fails, otherwise use project-specific folder
        fs::path skill_path;
        if (root_str.empty()) {
            skill_path = fs::path("data") / "business_metadata";
        } else {
            skill_path = fs::path(root_str) / ".study_assistant" / "business_metadata";
        }

        spdlog::info("🧠 Loading Skills for {} from {}", project_id, skill_path.string());
        skill_libraries_[project_id] = std::make_shared<SkillLibrary>(skill_path.string(), ai_service_);
    }
    return skill_libraries_[project_id];
}


// 🚀 GRAPH MANAGEMENT IMPLEMENTATION
std::shared_ptr<PointerGraph> AgentExecutor::get_or_create_graph(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    
    if (graphs_.find(project_id) == graphs_.end()) {
        // Sanitize ID for filesystem safety
        std::string safe_id = project_id;
        std::replace(safe_id.begin(), safe_id.end(), ':', '_');
        std::replace(safe_id.begin(), safe_id.end(), '/', '_');
        std::replace(safe_id.begin(), safe_id.end(), '\\', '_');
        
        // Store in data/graphs/<project_id>
        std::string path = "data/graphs/" + safe_id;
        if (!fs::exists(path)) fs::create_directories(path);
        
        spdlog::info("📂 Loading Graph for Project: {} at {}", project_id, path);
        graphs_[project_id] = std::make_shared<PointerGraph>(path);
    }
    return graphs_[project_id];
}

void AgentExecutor::ingest_sync_results(const std::string& project_id, const std::vector<std::shared_ptr<CodeNode>>& nodes) {
    auto graph = get_or_create_graph(project_id);
    
    spdlog::info("🧠 Ingesting {} code nodes into Graph Memory...", nodes.size());
    
    for (const auto& node : nodes) {
        std::unordered_map<std::string, std::string> meta;
        meta["file_path"] = node->file_path;
        meta["node_name"] = node->name;
        meta["node_type"] = node->type;
        
        std::string deps = "";
        for(const auto& d : node->dependencies) deps += d + ",";
        meta["dependencies"] = deps;
        
        // Add Context Node (Knowledge Base)
        graph->add_node(
            node->content, 
            NodeType::CONTEXT_CODE, 
            "", // Root knowledge
            node->embedding,
            meta
        );
    }
    graph->save();
    spdlog::info("✅ Graph Knowledge Updated.");
}

std::string AgentExecutor::restore_session_cursor(std::shared_ptr<PointerGraph> graph, const std::string& session_id) {
    // Query graph for nodes with this session_id metadata
    // In a real DB, this is fast. With flat JSON, it's O(N), but acceptable for startup.
    auto nodes = graph->query_by_metadata("session_id", session_id);
    
    if (nodes.empty()) return "";

    // Find the most recent node (highest timestamp)
    PointerNode latest = nodes[0];
    for(const auto& n : nodes) {
        if (n.timestamp > latest.timestamp) latest = n;
    }
    
    spdlog::info("🔄 Restored Session '{}' cursor to node: {}", session_id, latest.id);
    return latest.id;
}

// --- CORE ENGINE ---
std::string AgentExecutor::run_autonomous_loop(const ::code_assistance::UserQuery& req, ::grpc::ServerWriter<::code_assistance::AgentResponse>* writer) {
    auto mission_start_time = std::chrono::steady_clock::now();
    
    // 1. Setup Graph & Session
    auto graph = get_or_create_graph(req.project_id());
    std::string session_id = req.session_id();
    std::string parent_node_id = "";
    
    {
        std::lock_guard<std::mutex> lock(cursor_mutex_);
        if (session_cursors_.find(session_id) == session_cursors_.end()) {
            session_cursors_[session_id] = restore_session_cursor(graph, session_id);
        }
        parent_node_id = session_cursors_[session_id];
    }

    // 2. Variables
    std::string tool_manifest = tool_registry_->get_manifest();
    std::string internal_monologue = "";
    std::string final_output = "Mission Timed Out.";
    std::string last_error = ""; 
    std::string last_effective_prompt = ""; 

    code_assistance::GenerationResult last_gen; 

    // 3. Record User Prompt & Embed
    std::vector<float> prompt_vec = ai_service_->generate_embedding(req.prompt());
    std::string root_node_id = graph->add_node(req.prompt(), NodeType::PROMPT, parent_node_id, prompt_vec, {{"session_id", session_id}});
    std::string last_graph_node = root_node_id;

    // --- SKILL RETRIEVAL ---
    static std::unordered_map<std::string, std::string> session_skill_cache;
    static std::mutex skill_cache_mutex;

    std::string business_context = "";
    
    // 1. Try to find new skills based on current prompt
    auto skill_lib = get_skill_library(req.project_id());
    std::string new_skills = skill_lib->retrieve_skills(req.prompt(), prompt_vec);

    {
        std::lock_guard<std::mutex> lock(skill_cache_mutex);
        
        // 2. If new skills found, update cache (append unique)
        // For simplicity, we just overwrite or append if it's significant.
        // A better heuristic: If prompt is short ("Approved"), KEEP old skills.
        // If prompt is long ("Change it to use SQL"), FETCH new skills.
        
        if (req.prompt().length() > 50 && !new_skills.empty()) {
            session_skill_cache[session_id] = new_skills;
        }
        
        // 3. Always use cached skills if available
        if (session_skill_cache.count(session_id)) {
            business_context = session_skill_cache[session_id];
        } else if (!new_skills.empty()) {
             // First turn
             session_skill_cache[session_id] = new_skills;
             business_context = new_skills;
        }
    }
    
    if (!business_context.empty()) {
        std::string header = "\n### 🛑 MANDATORY BUSINESS RULES (YOU MUST FOLLOW THESE)\n" 
                             "Failure to follow these rules will result in rejection.\n";
        // Check if header already exists to avoid duplication
        if (business_context.find("### 🛑") == std::string::npos) {
            business_context = header + business_context;
        }
    }

    if (!parent_node_id.empty()) {
        auto trace = graph->get_trace(parent_node_id);
        // Limit to last 15 steps to save context window
        int start_idx = std::max(0, (int)trace.size() - 15);
        
        for (size_t i = start_idx; i < trace.size(); ++i) {
            const auto& node = trace[i];
            // Skip nodes from other sessions if mixed (though get_trace follows parent links)
            
            if (node.type == NodeType::PROMPT) {
                internal_monologue += "\n[USER] " + node.content;
            } else if (node.type == NodeType::SYSTEM_THOUGHT) {
                internal_monologue += "\n[THOUGHT] " + node.content;
            } else if (node.type == NodeType::TOOL_CALL) {
                // Try to get tool name from metadata, or parsing content
                std::string tname = node.metadata.count("tool") ? node.metadata.at("tool") : "tool";
                internal_monologue += "\n[TOOL CALL] " + tname; // Content might be huge JSON, skip for brevity
            } else if (node.type == NodeType::CONTEXT_CODE) {
                // Tool output
                std::string preview = node.content.length() > 100 ? node.content.substr(0, 100) + "..." : node.content;
                internal_monologue += "\n[TOOL RESULT] " + preview;
            } else if (node.type == NodeType::RESPONSE) {
                internal_monologue += "\n[AI] " + node.content;
            }
        }
        spdlog::info("🧠 Reconstructed History: {} entries.", trace.size());
    }

    // Check Plan Approval via Chat
    if (planning_engine_->has_active_plan() && !planning_engine_->is_plan_approved()) {
        std::string p_lower = req.prompt();
        std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), ::tolower);
        
        bool user_approved = (
            p_lower.find("approve") != std::string::npos || 
            p_lower.find("yes") != std::string::npos || 
            p_lower.find("proceed") != std::string::npos ||
            p_lower.find("go ahead") != std::string::npos ||
            p_lower.find("looks good") != std::string::npos
        );

        if (user_approved) {
            planning_engine_->approve_plan();
            this->notify(writer, "PLANNING", "Plan approved. Commencing execution.");
            spdlog::info("✅ AgentExecutor: Detected user approval in prompt: '{}'", req.prompt());
        } else {
            spdlog::info("⏳ AgentExecutor: Plan is pending. User prompt '{}' did not trigger approval.", req.prompt());
        }
    }

    // 4. Memory Recall
    MemoryRecallResult long_term = memory_vault_->recall(prompt_vec);
    auto related_nodes = graph->semantic_search(prompt_vec, 5);
    std::string memories = "";
    std::string warnings = ""; 
    std::string episodic_memories = "";

    if (!related_nodes.empty()) {
        for(const auto& node : related_nodes) {
            bool is_failure = (node.metadata.count("status") && node.metadata.at("status") == "failed");
            
            if (node.type == NodeType::CONTEXT_CODE) {
                std::string fpath = node.metadata.count("file_path") ? node.metadata.at("file_path") : "unknown";
                memories += "- [CODE] " + fpath + ":\n" + node.content.substr(0, 300) + "...\n";
            } 
            else if (node.type == NodeType::RESPONSE) {
                if (!is_failure) episodic_memories += "- [RECENT CHAT] " + node.content + "\n";
            } 
            else if (node.type == NodeType::TOOL_CALL) {
                if (is_failure) warnings += "⚠️ AVOID: " + node.content + " (This failed previously)\n";
                else memories += "- [SUCCESSFUL ACTION] " + node.content + "\n";
            }
        }
    }

    // 🚀 NEW: Load Massive Context (Tree + Full Code)
    std::string massive_context = "";
    std::string full_codebase = load_full_context_file(req.project_id());
    
    if (!full_codebase.empty()) {
        // 🛑 TOKEN LIMIT CALCULATION:
        // 1 Token ~= 4 Characters.
        // Limit: 1,000,000 Tokens ~= 4,000,000 Bytes (4MB).
        // Safety Buffer: We use 3.8MB to leave room for system prompts, history, and output.
        const size_t SAFE_TOKEN_LIMIT_BYTES = 3800000; 

        if (full_codebase.size() > SAFE_TOKEN_LIMIT_BYTES) {
             massive_context += "\n### 📚 FULL CODEBASE (Truncated to fit 1M Context)\n" + full_codebase.substr(0, SAFE_TOKEN_LIMIT_BYTES) + "\n...[CONTENT TRUNCATED]...\n";
        } else {
             massive_context += "\n### 📚 FULL CODEBASE\n" + full_codebase + "\n";
        }
    }

    // 5. Execution Loop
    int max_steps = 16;
    
    for (int step = 0; step < max_steps; ++step) {
        
        std::string prompt_template = 
            "### SYSTEM ROLE\n"
            "You are 'Synapse', an Autonomous Coding Agent.\n\n"
            "### TOOL MANIFEST\n" + tool_manifest + "\n"
            "Added Tool: `propose_plan` -> Input: { \"steps\": [ {\"description\": \"...\", \"tool\": \"apply_edit\", \"parameters\": {\"path\": \"REQUIRED_FILE_PATH.py\", \"content\": \"...\"}} ] }\n"
            "CRITICAL: You MUST include the 'path' parameter in the plan step if the tool is 'apply_edit'.\n\n"
            "IMPORTANT: When writing code in JSON, you MUST escape newlines as \\n. Do not use literal newlines inside strings."
            "### USER REQUEST\n" + req.prompt() + "\n\n";

        if (!business_context.empty()) prompt_template += business_context + "\n";
        if (!massive_context.empty()) prompt_template += massive_context + "\n";

        std::string plan_ctx = planning_engine_->get_plan_context_for_ai();
        if (!plan_ctx.empty()) prompt_template += plan_ctx + "\n";

        if (long_term.has_memories) {
            if(!long_term.positive_hints.empty()) 
                prompt_template += "### 🧠 SUCCESSFUL STRATEGIES\n" + long_term.positive_hints + "\n";
            if(!long_term.negative_warnings.empty()) 
                prompt_template += "### ⛔ KNOWN PITFALLS\n" + long_term.negative_warnings + "\n";
        }

        if (!episodic_memories.empty()) 
            prompt_template += "### EPISODIC CONTEXT\n" + sanitize_for_prompt(episodic_memories) + "\n";
        if (!memories.empty()) 
            prompt_template += "### RELEVANT CODE\n" + sanitize_for_prompt(memories) + "\n";
        if (!internal_monologue.empty()) prompt_template += "### EXECUTION HISTORY\n" + internal_monologue + "\n";
        if (!warnings.empty()) prompt_template += "### ⛔ ACTIVE WARNINGS\n" + warnings + "\n";
        if (!last_error.empty()) prompt_template += "\n### ⚠️ PREVIOUS ERROR\n" + last_error + "\nREQUIRED: Fix this error.\n";

        prompt_template += 
            "\n### 🚨 CRITICAL JSON FORMATTING RULES 🚨\n"
            "When generating Python code inside JSON parameters:\n"
            "1. **ALWAYS use SINGLE QUOTES for Python strings**: print('hello') ✅  print(\"hello\") ❌\n"
            "2. **F-strings must use single quotes**: f'Result: {x}' ✅  f\"Result: {x}\" ❌\n"
            "3. **Docstrings use single quotes**: 'This is a docstring' ✅\n"
            "4. **Multi-line code: Use \\n**: \"def foo():\\n    return 1\" ✅\n"
            "5. **NEVER use triple quotes**: \"\"\"docstring\"\"\" ❌\n\n"
            
            "CORRECT EXAMPLE:\n"
            "{\n"
            "  \"tool\": \"apply_edit\",\n"
            "  \"parameters\": {\n"
            "    \"path\": \"test.py\",\n"
            "    \"content\": \"import logging\\n\\nlogging.basicConfig(level=logging.INFO)\\n\\ndef calc(n):\\n    'Calculate something'\\n    logging.info(f'Processing n={n}')\\n    return n * 2\"\n"
            "  }\n"
            "}\n\n"
            
            "WRONG EXAMPLE (WILL FAIL):\n"
            "{\n"
            "  \"parameters\": {\n"
            "    \"content\": \"logging.info(f\\\"This breaks JSON\\\")\"  ❌ DOUBLE QUOTES IN F-STRING\n"
            "  }\n"
            "}\n\n"
            
            // [NEW] FORCE JSON MODE INSTRUCTION
            "### 🛑 FINAL OUTPUT INSTRUCTION 🛑\n"
            "You must output ONLY valid JSON. Do not write any conversational text, explanations, or markdown outside the JSON object.\n"
            "Your entire response must start with `{` and end with `}`.\n";
            
        // Capture the full prompt context for the dashboard
        last_effective_prompt = prompt_template;
        spdlog::debug("📝 PROMPT TO AI (Truncated):\n{}", prompt_template.substr(0, 1000));
        
        if (prompt_template.find("f\"") != std::string::npos) {
            spdlog::warn("⚠️ Prompt contains f-string with double quotes - AI will likely fail");
        }

        this->notify(writer, "THINKING", "Processing logic...");
        last_gen = ai_service_->generate_text_elite(prompt_template);
        
        if (!last_gen.success) {
            final_output = "ERROR: AI Service Failure";
            goto mission_complete;
        }

        std::string raw_thought = last_gen.text;

        // --- 🔍 DEBUG LOG: RAW AI OUTPUT ---
        spdlog::info("\n==================================================");
        spdlog::info("🤖 RAW SCRAPER/AI OUTPUT (START):");
        // We use std::cout for raw output to ensure no formatting/truncation issues with spdlog
        std::cout << raw_thought << std::endl; 
        spdlog::info("🤖 RAW SCRAPER/AI OUTPUT (END)");
        spdlog::info("==================================================\n");
        // ------------------------------------

        nlohmann::json action = extract_json(raw_thought);

        spdlog::info("🧩 PARSED JSON RESULT:\n{}", action.dump(2)); 

        std::string tool_name = "";

        if (action.contains("tool_code")) {
            internal_monologue += "\n[SYSTEM ERROR] Invalid JSON Format (tool_code). Use {\"tool\": ..., \"parameters\": ...}.";
            this->notify(writer, "ERROR_CATCH", "Invalid Protocol (tool_code). Retrying...");
            continue; 
        }

        if (action.contains("tool")) {
            tool_name = action["tool"];
        } 
        else if (action.contains("name")) {
            tool_name = action["name"];
        }
        else if (action.contains("function")) {
            tool_name = action["function"];
        }

        if (!tool_name.empty()) {
            
            // Log what we found for debugging
            spdlog::info("🔍 Detected Tool Call: {}", tool_name);

            // Handle optional "thought" or "reasoning" fields
            std::string reasoning = action.value("thought", action.value("reasoning", "Executing logic..."));
            
            // Handle parameters (support "parameters", "arguments", or "args")
            nlohmann::json params;
            if (action.contains("parameters")) params = action["parameters"];
            else if (action.contains("arguments")) params = action["arguments"];
            else if (action.contains("args")) params = action["args"];
            else params = nlohmann::json::object(); // No params

            params["project_id"] = req.project_id();

            // --- 🛡️ EXECUTION GUARD CHECK START ---
            GuardResult guard = ExecutionGuard::validate_tool_call(tool_name, params, planning_engine_.get());
            
            if (!guard.allowed) {
                spdlog::warn("🛑 Guard Blocked Action: {}", guard.reason);
                this->notify(writer, "BLOCKED", guard.reason);
                internal_monologue += "\n[SYSTEM BLOCK] " + guard.reason;
                last_error = guard.reason; 
                
                // IMPORTANT: Semicolons added strictly here
                final_output = "I cannot execute that action. " + guard.reason + " I must propose a plan first.";
                continue; 
            }
            // --- 🛡️ EXECUTION GUARD CHECK END ---

            // 1. Propose Plan
            if (tool_name == "propose_plan") {
                // If plan is already approved, BLOCK proposal and force execution
                if (planning_engine_->is_plan_approved()) {
                    this->notify(writer, "ERROR_CATCH", "Plan already approved. Forcing execution...");
                    
                    // Inject a stern correction into the monologue
                    internal_monologue += "\n[SYSTEM ERROR] You tried to 'propose_plan' but the plan is ALREADY APPROVED. You MUST use 'apply_edit' or 'run_command' to execute the steps now.";
                    
                    // Don't exit loop, let it retry generation with this new context
                    continue; 
                }

                if (params.contains("steps")) {
                    planning_engine_->propose_plan(req.prompt(), params["steps"]);
                    
                    ::code_assistance::AgentResponse plan_res;
                    plan_res.set_phase("PROPOSAL");
                    plan_res.set_payload(planning_engine_->get_snapshot().to_json().dump()); 
                    if (writer) writer->Write(plan_res);
                    
                    final_output = "I have proposed a plan based on the business rules. Please review and approve.";
                    goto mission_complete; 
                }
            }

            // 2. Execute Tool
            this->notify(writer, "TOOL_EXEC", "Running " + tool_name);
            std::string observation = safe_execute_tool(tool_name, params, session_id);

            // 3. Update Plan Step
            if (planning_engine_->is_plan_approved()) {
                auto plan = planning_engine_->get_snapshot();
                if (plan.current_step_idx < plan.steps.size()) {
                    auto next_tool = plan.steps[plan.current_step_idx].tool_name;
                    prompt_template += "5. **URGENT ACTION**: The plan is APPROVED. \n"
                                    "   **FORBIDDEN TOOL**: `propose_plan` (Do NOT use this).\n"  // <--- ADDED THIS
                                    "   **REQUIRED TOOL**: `" + next_tool + "`.\n"
                                    "   Execute Step " + std::to_string(plan.current_step_idx + 1) + " immediately.\n";
                }
            }
            
            // 4. Record Thought
            last_graph_node = graph->add_node(reasoning, NodeType::SYSTEM_THOUGHT, last_graph_node);
            this->notify(writer, "PLANNING", reasoning);

            // 5. Handle Final Answer
            if (tool_name == "FINAL_ANSWER") {
                final_output = params.value("answer", "");
                last_graph_node = graph->add_node(final_output, NodeType::RESPONSE, last_graph_node, {}, {{"status", "success"}});
                
                if (last_error.empty()) {
                    memory_vault_->add_success(req.prompt(), "Solved via: " + internal_monologue.substr(0, 500), prompt_vec);
                }

                this->notify(writer, "FINAL", final_output);
                goto mission_complete; 
            }

            // 6. Record Intent & Result
            std::string sig = tool_name + params.dump();
            last_graph_node = graph->add_node(sig, NodeType::TOOL_CALL, last_graph_node, {}, {{"tool", tool_name}});
            
            last_graph_node = graph->add_node(observation, NodeType::CONTEXT_CODE, last_graph_node);

            // 7. Self-Check & Learning
            if (observation.find("ERROR:") == 0 || observation.find("SYSTEM_ERROR") == 0) {
                memory_vault_->add_failure(req.prompt(), "Tool Failed: " + tool_name + " Params: " + params.dump(), prompt_vec);
                last_error = observation;
                graph->update_metadata(last_graph_node, "status", "failed");
                internal_monologue += "\n[FAILED] " + tool_name + " " + params.dump() + " -> " + observation;
                this->notify(writer, "ERROR_CATCH", "Detected failure. Self-correcting...");

                if (planning_engine_->is_plan_approved()) {
                    auto plan = planning_engine_->get_snapshot();
                    planning_engine_->mark_step_status(plan.current_step_idx, StepStatus::FAILED, observation);
                }

            } else {
                last_error = "";
                graph->update_metadata(last_graph_node, "status", "success");
                internal_monologue += "\n[OK] " + tool_name + " " + params.dump() + " -> " + observation.substr(0, 200) + "...";
                this->notify(writer, "SUCCESS", "Action confirmed.");

                // 🚀 CRITICAL FIX: Update Planning Engine state
                if (planning_engine_->is_plan_approved()) {
                    auto plan = planning_engine_->get_snapshot();
                    // Double check if the executed tool matches the current step to be safe
                    if (plan.current_step_idx < plan.steps.size()) {
                         // We assume the Guard already validated it matches, so we mark it success
                         planning_engine_->mark_step_status(plan.current_step_idx, StepStatus::SUCCESS, observation);
                         spdlog::info("✅ Plan Step {} Completed. Advancing...", plan.current_step_idx + 1);
                    }
                }
            }
            
        } else {
            // No tool = Chat response
            final_output = last_gen.text;
            last_graph_node = graph->add_node(final_output, NodeType::RESPONSE, last_graph_node);
            this->notify(writer, "FINAL", final_output);
            goto mission_complete;
        }
    }

mission_complete:
    {
        std::lock_guard<std::mutex> lock(cursor_mutex_);
        session_cursors_[session_id] = last_graph_node;
    }
    
    auto mission_end_time = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(mission_end_time - mission_start_time).count();
    
    SystemMonitor::global_llm_generation_ms.store(total_ms);
    
    code_assistance::InteractionLog log;
    log.request_type = "AGENT";
    log.project_id = req.project_id();
    log.user_query = req.prompt();
    log.ai_response = final_output;
    
    log.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    log.duration_ms = total_ms;
    log.full_prompt = last_effective_prompt + "\n\n### EXECUTION HISTORY\n" + internal_monologue;
    code_assistance::LogManager::instance().add_log(log);

    graph->save();
    return final_output;
}

std::string AgentExecutor::run_autonomous_loop_internal(const nlohmann::json& body) {
    ::code_assistance::UserQuery fake_req;
    fake_req.set_prompt(body.value("prompt", ""));
    fake_req.set_project_id(body.value("project_id", "default"));
    
    // Use "REST_SESSION" as default ID to maintain continuity for VS Code unless specified
    std::string sid = body.value("session_id", "REST_SESSION");
    fake_req.set_session_id(sid);
    
    return this->run_autonomous_loop(fake_req, nullptr); 
}

std::string AgentExecutor::safe_execute_tool(
    const std::string& tool_name, 
    const nlohmann::json& params, 
    const std::string& session_id
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    std::string result;
    bool failed = false;

    // 1. Deep Logging: Intent
    spdlog::info("🛠️ [TOOL START] {} | Params: {}", tool_name, params.dump());
    
    try {
        // 2. Execution Guard
        result = tool_registry_->dispatch(tool_name, params);
        
        // 3. Application-Level Error Check (Tools return strings starting with "ERROR:")
        if (result.find("ERROR:") == 0) {
            failed = true;
            spdlog::warn("⚠️ [TOOL FAIL] {} | Reason: {}", tool_name, result);
        } else {
            spdlog::info("✅ [TOOL OK] {} | Output Size: {} chars", tool_name, result.size());
        }

    } catch (const std::exception& e) {
        // 4. C++ Exception Trap (Segfaults/Stdlib errors)
        failed = true;
        result = "SYSTEM EXCEPTION: " + std::string(e.what());
        spdlog::error("💥 [TOOL CRASH] {} | Exception: {}", tool_name, e.what());
    } catch (...) {
        // 5. Catch-All (Unknown errors)
        failed = true;
        result = "SYSTEM EXCEPTION: Unknown Critical Failure";
        spdlog::critical("💥 [TOOL CRASH] {} | Unknown Signal", tool_name);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    // 6. Telemetry Injection
    // We log the trace regardless of success/fail for the Dashboard
    std::string state = failed ? "ERROR_CATCH" : "TOOL_EXEC";
    code_assistance::LogManager::instance().add_trace({
        session_id, 
        "", // Timestamp auto-filled
        state, 
        (failed ? "FAILED: " : "SUCCESS: ") + tool_name + " -> " + result.substr(0, 100), 
        duration
    });

    return result;
}


void AgentExecutor::determineContextStrategy(const std::string& query, ContextSnapshot& ctx, const std::string& project_id) {}
bool AgentExecutor::check_reflection(const std::string& query, const std::string& topo, std::string& reason) { return true; }
std::string AgentExecutor::construct_reasoning_prompt(const std::string& task, const std::string& history, const std::string& last_error) { return ""; }

}