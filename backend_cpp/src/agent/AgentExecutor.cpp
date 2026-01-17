#define NOMINMAX
#include <cpr/cpr.h>
#include "agent/AgentExecutor.hpp"
#include "LogManager.hpp"
#include <regex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stack>
#include <unordered_set>
#include "parser_elite.hpp"

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
}

// --- HELPERS (Kept same as your code) ---
nlohmann::json extract_json(const std::string& raw) {
    std::string clean = raw;
    size_t code_start = clean.find("```json");
    if (code_start != std::string::npos) {
        size_t code_end = clean.find("```", code_start + 7);
        if (code_end != std::string::npos) clean = clean.substr(code_start + 7, code_end - (code_start + 7));
    }
    try {
        size_t start = clean.find("{");
        size_t end = clean.rfind("}");
        if (start != std::string::npos && end != std::string::npos) return nlohmann::json::parse(clean.substr(start, end - start + 1));
    } catch (...) {}
    try {
        if (raw.find("read_file") != std::string::npos) {
            std::regex path_re(R"(path=['"]([^'"]+)['"])");
            std::smatch match;
            if (std::regex_search(raw, match, path_re)) return {{"thought", "Auto-correcting python output..."}, {"tool", "read_file"}, {"parameters", {{"path", match[1].str()}}}};
        }
    } catch(...) {}
    return nlohmann::json::object();
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
    code_assistance::GenerationResult last_gen; 

    // 3. Record User Prompt & Embed
    std::vector<float> prompt_vec = ai_service_->generate_embedding(req.prompt());
    std::string root_node_id = graph->add_node(
        req.prompt(), 
        NodeType::PROMPT, 
        parent_node_id, 
        prompt_vec, 
        {{"session_id", session_id}}
    );
    std::string last_graph_node = root_node_id;

    // 4. Memory Recall (Hybrid: Short-term Graph + Long-term Vault)
    
    // A. Long Term (Experience Vault)
    MemoryRecallResult long_term = memory_vault_->recall(prompt_vec);

    // B. Short Term (Graph Context)
    auto related_nodes = graph->semantic_search(prompt_vec, 5);
    std::string memories = "";
    std::string warnings = ""; 
    std::string episodic_memories = "";

    if (!related_nodes.empty()) {
        for(const auto& node : related_nodes) {
            bool is_failure = (node.metadata.count("status") && node.metadata.at("status") == "failed");
            
            // Code Context
            if (node.type == NodeType::CONTEXT_CODE) {
                std::string fpath = node.metadata.count("file_path") ? node.metadata.at("file_path") : "unknown";
                memories += "- [CODE] " + fpath + ":\n" + node.content.substr(0, 300) + "...\n";
            } 
            // Recent Chat History
            else if (node.type == NodeType::RESPONSE) {
                if (!is_failure) episodic_memories += "- [RECENT CHAT] " + node.content + "\n";
            } 
            // Past Actions
            else if (node.type == NodeType::TOOL_CALL) {
                if (is_failure) warnings += "⚠️ AVOID: " + node.content + " (This failed previously)\n";
                else memories += "- [SUCCESSFUL ACTION] " + node.content + "\n";
            }
        }
    }

    // 5. Execution Loop
    int max_steps = 16;
    
    for (int step = 0; step < max_steps; ++step) {
        
        std::string prompt_template = 
            "### SYSTEM ROLE\n"
            "You are 'Synapse', an Autonomous Coding Agent.\n\n"
            "### TOOL MANIFEST\n" + tool_manifest + "\n\n"
            "### USER REQUEST\n" + req.prompt() + "\n\n";

        // 🚀 INJECT EXPERIENCE VAULT (Long Term)
        if (long_term.has_memories) {
            if(!long_term.positive_hints.empty()) 
                prompt_template += "### 🧠 SUCCESSFUL STRATEGIES (Verified Patterns)\n" + long_term.positive_hints + "\n";
            if(!long_term.negative_warnings.empty()) 
                prompt_template += "### ⛔ KNOWN PITFALLS (Avoid These)\n" + long_term.negative_warnings + "\n";
        }

        // 🚀 INJECT EPISODIC MEMORY (Short Term)
        if (!episodic_memories.empty()) prompt_template += "### EPISODIC CONTEXT (Recent)\n" + episodic_memories + "\n";
        if (!memories.empty()) prompt_template += "### RELEVANT CODE/ACTIONS\n" + memories + "\n";
        
        // 🚀 INJECT EXECUTION STATE
        if (!internal_monologue.empty()) prompt_template += "### EXECUTION HISTORY\n" + internal_monologue + "\n";
        if (!warnings.empty()) prompt_template += "### ⛔ ACTIVE WARNINGS\n" + warnings + "\n";
        if (!last_error.empty()) prompt_template += "\n### ⚠️ PREVIOUS ERROR\n" + last_error + "\nREQUIRED: Fix this error using a different strategy.\n";

        prompt_template += "\n### INSTRUCTIONS\n"
                           "1. Analyze the Context and Memory.\n"
                           "2. Choose the best Tool or Final Answer.\n"
                           "3. Respond in STRICT JSON.\n"
                           "\nNEXT JSON ACTION:";

        spdlog::debug("📝 FULL PROMPT SENT TO AI:\n{}", prompt_template);
        this->notify(writer, "THINKING", "Processing logic...");
        
        last_gen = ai_service_->generate_text_elite(prompt_template);
        
        if (!last_gen.success) {
            final_output = "ERROR: AI Service Failure";
            goto mission_complete;
        }

        std::string raw_thought = last_gen.text;
        nlohmann::json action = extract_json(raw_thought);
        
        if (action.contains("tool")) {
            std::string tool_name = action["tool"];
            std::string reasoning = action.value("thought", "");
            
            // Record Thought
            last_graph_node = graph->add_node(reasoning, NodeType::SYSTEM_THOUGHT, last_graph_node);
            this->notify(writer, "PLANNING", reasoning);

            // Handle Final Answer
            if (tool_name == "FINAL_ANSWER") {
                final_output = action["parameters"].value("answer", "");
                last_graph_node = graph->add_node(final_output, NodeType::RESPONSE, last_graph_node, {}, {{"status", "success"}});
                
                // 🚀 LEARNING: If we reached success without crashing, record it as a positive pattern
                if (last_error.empty()) {
                    memory_vault_->add_success(req.prompt(), "Solved via: " + internal_monologue.substr(0, 500), prompt_vec);
                }

                this->notify(writer, "FINAL", final_output);
                goto mission_complete; 
            }

            // Record Intent
            std::string sig = tool_name + action["parameters"].dump();
            last_graph_node = graph->add_node(sig, NodeType::TOOL_CALL, last_graph_node, {}, {{"tool", tool_name}});

            // Execution
            nlohmann::json params = action.value("parameters", nlohmann::json::object());
            params["project_id"] = req.project_id();

            this->notify(writer, "TOOL_EXEC", "Running " + tool_name);
            
            // 🚀 Call Sentinel
            std::string observation = safe_execute_tool(tool_name, params, session_id);
            
            // Record Result
            last_graph_node = graph->add_node(observation, NodeType::CONTEXT_CODE, last_graph_node);

            // Self-Check & Learning
            if (observation.find("ERROR:") == 0 || observation.find("SYSTEM_ERROR") == 0) {
                // 🚀 LEARNING: Record Negative Pattern
                memory_vault_->add_failure(req.prompt(), "Tool Failed: " + tool_name + " Params: " + params.dump(), prompt_vec);
                
                last_error = observation;
                graph->update_metadata(last_graph_node, "status", "failed");
                internal_monologue += "\n[FAILED] " + tool_name + " -> " + observation;
                this->notify(writer, "ERROR_CATCH", "Detected failure. Self-correcting...");
            } else {
                last_error = "";
                graph->update_metadata(last_graph_node, "status", "success");
                internal_monologue += "\n[OK] " + tool_name + " -> " + observation.substr(0, 200) + "...";
                this->notify(writer, "SUCCESS", "Action confirmed.");
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
    // Update cursor for continuity
    {
        std::lock_guard<std::mutex> lock(cursor_mutex_);
        session_cursors_[session_id] = last_graph_node;
    }
    
    // Telemetry log
    auto mission_end_time = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(mission_end_time - mission_start_time).count();
    code_assistance::InteractionLog log;
    log.request_type = "AGENT";
    log.project_id = req.project_id();
    log.user_query = req.prompt();
    log.ai_response = final_output;
    log.duration_ms = total_ms;
    log.full_prompt = "### HISTORY:\n" + internal_monologue;
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