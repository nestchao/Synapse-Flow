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
    memory_vault_ = std::make_unique<MemoryVault>("data/memory_vault");
}

// --- HELPERS ---
nlohmann::json extract_json(const std::string& raw) {
    std::string clean = raw;
    
    // 1. Strip Markdown
    size_t code_start = clean.find("```json");
    if (code_start != std::string::npos) {
        size_t code_end = clean.find("```", code_start + 7);
        if (code_end != std::string::npos) clean = clean.substr(code_start + 7, code_end - (code_start + 7));
    }

    try {
        size_t start = clean.find("{");
        size_t end = clean.rfind("}");
        if (start != std::string::npos && end != std::string::npos) {
            return nlohmann::json::parse(clean.substr(start, end - start + 1));
        }
    } catch (...) {}

    // 2. Python-Style Fallback
    try {
        if (raw.find("read_file") != std::string::npos) {
            std::regex path_re(R"(path=['"]([^'"]+)['"])");
            std::smatch match;
            if (std::regex_search(raw, match, path_re)) {
                return {{"thought", "Correcting python-style output..."}, {"tool", "read_file"}, {"parameters", {{"path", match[1].str()}}}};
            }
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

// --- CORE ENGINE ---
std::string AgentExecutor::run_autonomous_loop(const ::code_assistance::UserQuery& req, ::grpc::ServerWriter<::code_assistance::AgentResponse>* writer) {
    auto mission_start_time = std::chrono::steady_clock::now();
    
    std::string tool_manifest = tool_registry_->get_manifest();
    std::string internal_monologue = "";
    std::unordered_set<size_t> action_history;
    std::hash<std::string> hasher;

    code_assistance::GenerationResult last_gen; 
    std::string final_output = "Mission Timed Out.";
    std::string last_error = ""; 

    std::vector<float> prompt_vec = ai_service_->generate_embedding(req.prompt());
    std::string memories = memory_vault_->recall(prompt_vec);
    
    int max_steps = 8;
    
    for (int step = 0; step < max_steps; ++step) {
        
        // 🚀 PHASE 4: COGNITIVE PROMPT (Chain of Thought)
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

        if (!memories.empty()) prompt += "### MEMORY\n" + memories + "\n"; 
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
            
            // 🚀 LOG REASONING
            spdlog::info("🧠 STEP {}: {}", step+1, reasoning);
            this->notify(writer, "PLANNING", reasoning); // Send thought to Dashboard

            // Loop Detection
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
                this->notify(writer, "FINAL", final_output);
                goto mission_complete; 
            }

            nlohmann::json params = action.value("parameters", nlohmann::json::object());
            params["project_id"] = req.project_id();

            this->notify(writer, "TOOL_EXEC", "Engaging: " + tool_name);
            std::string observation = tool_registry_->dispatch(tool_name, params);
            
            // 🧠 ERROR HANDLING
            std::string prefix = observation.substr(0, 50);
            if (prefix.find("ERROR") != std::string::npos) {
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
    log.total_tokens = last_gen.total_tokens;
    log.full_prompt = "### HISTORY:\n" + internal_monologue;

    code_assistance::LogManager::instance().add_log(log);

    if (final_output != "Mission Timed Out." && last_error.empty()) {
        memory_vault_->add_experience(req.prompt(), final_output, prompt_vec, 1.0);
    }

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