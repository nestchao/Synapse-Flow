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
    std::shared_ptr<ToolRegistry> tool_registry
) : engine_(engine), ai_service_(ai), sub_agent_(sub_agent), tool_registry_(tool_registry) {

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

// --- CORE ENGINE ---
std::string AgentExecutor::run_autonomous_loop(const ::code_assistance::UserQuery& req, ::grpc::ServerWriter<::code_assistance::AgentResponse>* writer) {
    auto mission_start_time = std::chrono::steady_clock::now();
    
    // 1. Get Project Graph (This is the specific brain for this project)
    auto graph = get_or_create_graph(req.project_id());

    std::string tool_manifest = tool_registry_->get_manifest();
    std::string internal_monologue = "";
    std::unordered_set<size_t> action_history;
    std::hash<std::string> hasher;

    code_assistance::GenerationResult last_gen; 
    std::string final_output = "Mission Timed Out.";
    std::string last_error = ""; 

    // 2. Memory Recall (Hybrid: Code + Episodic)
    std::vector<float> prompt_vec = ai_service_->generate_embedding(req.prompt());
    
    std::string root_node_id = graph->add_node(
        req.prompt(), 
        NodeType::PROMPT, 
        "", 
        prompt_vec, 
        {{"session_id", req.session_id()}}
    );
    std::string last_graph_node = root_node_id;

    // Search Graph for Context
    auto related_nodes = graph->semantic_search(prompt_vec, 3);
    std::string memories = "";
    if (!related_nodes.empty()) {
        memories = "### RELEVANT CONTEXT (From Knowledge Graph):\n";
        for(const auto& node : related_nodes) {
            if (node.type == NodeType::CONTEXT_CODE) {
                // 🚀 FIXED: Use .at() or check existence, also ensure file_path exists
                std::string fpath = node.metadata.count("file_path") ? node.metadata.at("file_path") : "unknown";
                memories += "- [CODE] " + fpath + ":\n" + node.content.substr(0, 300) + "...\n";
            } else if (node.type == NodeType::RESPONSE) {
                memories += "- [PAST SOLUTION] " + node.content + "\n";
            }
        }
    }
    
    int max_steps = 8;
    
    for (int step = 0; step < max_steps; ++step) {
        
        std::string prompt = 
            "### SYSTEM ROLE\n"
            "You are a C++ Autonomous Agent. You MUST plan before you act.\n\n"
            "### TOOL MANIFEST\n" + tool_manifest + "\n\n"
            "### OBJECTIVE\n" + req.prompt() + "\n\n"
            "### RESPONSE FORMAT (STRICT JSON)\n"
            "{\n"
            "  \"thought\": \"Briefly explain your reasoning here...\",\n"
            "  \"tool\": \"tool_name\",\n"
            "  \"parameters\": { ... }\n"
            "}\n\n";

        if (!memories.empty()) prompt += memories + "\n"; 
        if (!internal_monologue.empty()) prompt += "### EXECUTION HISTORY\n" + internal_monologue + "\n";

        if (!last_error.empty()) {
            prompt += "\n### ⚠️ PREVIOUS ERROR\nTrace: " + last_error + "\nREQUIRED: Analyze the error in your 'thought' field and pivot strategy.\n";
        }

        prompt += "\nNEXT JSON ACTION:";

        this->notify(writer, "THINKING", "Planning next move...");
        last_gen = ai_service_->generate_text_elite(prompt);
        
        if (!last_gen.success) {
            this->notify(writer, "FATAL", "Neural Link Severed");
            return "ERROR: AI Service Failure";
        }

        std::string raw_thought = last_gen.text;
        nlohmann::json action = extract_json(raw_thought);
        
        if (action.contains("tool")) {
            std::string tool_name = action["tool"];
            std::string reasoning = action.value("thought", "No reasoning provided.");
            
            // Record Thought in Graph
            last_graph_node = graph->add_node(
                reasoning, 
                NodeType::SYSTEM_THOUGHT, 
                last_graph_node
            );

            spdlog::info("🧠 STEP {}: {}", step+1, reasoning);
            this->notify(writer, "PLANNING", reasoning);

            std::string action_sig = tool_name + action["parameters"].dump();
            size_t action_hash = hasher(action_sig);
            if (action_history.count(action_hash) && last_error.empty()) {
                 internal_monologue += "\n[SYSTEM: Loop detected. Skipping.]";
                 last_error = "Loop detected. Change strategy.";
                 continue; 
            }
            action_history.insert(action_hash);

            if (tool_name == "FINAL_ANSWER") {
                final_output = action["parameters"].value("answer", "Done.");
                
                // Record Success
                graph->add_node(
                    final_output,
                    NodeType::RESPONSE,
                    last_graph_node,
                    {}, 
                    {{"status", "success"}}
                );
                
                this->notify(writer, "FINAL", final_output);
                goto mission_complete; 
            }

            // Record Tool Call
            last_graph_node = graph->add_node(
                action_sig,
                NodeType::TOOL_CALL,
                last_graph_node,
                {},
                {{"tool", tool_name}}
            );

            nlohmann::json params = action.value("parameters", nlohmann::json::object());
            params["project_id"] = req.project_id();

            this->notify(writer, "TOOL_EXEC", "Engaging: " + tool_name);
            std::string observation = tool_registry_->dispatch(tool_name, params);
            
            // Record Result
            last_graph_node = graph->add_node(
                observation,
                NodeType::CONTEXT_CODE, 
                last_graph_node
            );

            if (observation.find("ERROR:") != std::string::npos || observation.find("Error:") != std::string::npos) {
                last_error = observation; 
                internal_monologue += "\n[STEP " + std::to_string(step+1) + " FAILED]\nThought: " + reasoning + "\nError: " + observation;
                this->notify(writer, "ERROR_CATCH", "Action failed. Re-planning...");
                code_assistance::LogManager::instance().add_trace({req.session_id(), "", "ERROR_CATCH", observation, 0.0});
            } else {
                last_error = ""; 
                internal_monologue += "\n[STEP " + std::to_string(step+1) + " SUCCESS]\nThought: " + reasoning + "\nResult: " + observation;
                this->notify(writer, "SUCCESS", "Action confirmed.");
            }
            
        } else {
            if (raw_thought.find("FINAL_ANSWER") != std::string::npos) {
                final_output = raw_thought;
                graph->add_node(final_output, NodeType::RESPONSE, last_graph_node);
                goto mission_complete;
            }
            last_error = "Response was not valid JSON.";
        }
    }

mission_complete:
    auto mission_end_time = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(mission_end_time - mission_start_time).count();

    code_assistance::InteractionLog log;
    log.request_type = "AGENT";
    log.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log.project_id = req.project_id();
    log.user_query = req.prompt();
    log.ai_response = final_output;
    log.duration_ms = total_ms;
    log.full_prompt = "### HISTORY:\n" + internal_monologue;

    code_assistance::LogManager::instance().add_log(log);

    // Save the specific project graph
    graph->save();

    return final_output;
}

std::string AgentExecutor::run_autonomous_loop_internal(const nlohmann::json& body) {
    ::code_assistance::UserQuery fake_req;
    fake_req.set_prompt(body.value("prompt", ""));
    fake_req.set_project_id(body.value("project_id", "default"));
    fake_req.set_session_id("internal_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    return this->run_autonomous_loop(fake_req, nullptr); 
}

void AgentExecutor::determineContextStrategy(const std::string& query, ContextSnapshot& ctx, const std::string& project_id) {}
bool AgentExecutor::check_reflection(const std::string& query, const std::string& topo, std::string& reason) { return true; }
std::string AgentExecutor::construct_reasoning_prompt(const std::string& task, const std::string& history, const std::string& last_error) { return ""; }

}