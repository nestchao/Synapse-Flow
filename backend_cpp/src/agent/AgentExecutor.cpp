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
    try {
        std::stack<char> braces;
        size_t start = std::string::npos;
        size_t end = std::string::npos;

        for (size_t i = 0; i < raw.length(); ++i) {
            if (raw[i] == '{') {
                if (braces.empty()) start = i;
                braces.push('{');
            } else if (raw[i] == '}') {
                if (!braces.empty()) {
                    braces.pop();
                    if (braces.empty()) {
                        end = i;
                        break; 
                    }
                }
            }
        }

        if (start != std::string::npos && end != std::string::npos) {
            std::string clean_json = raw.substr(start, end - start + 1);
            return nlohmann::json::parse(clean_json);
        }
    } catch (...) {}
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
    // Also push to trace log
    code_assistance::LogManager::instance().add_trace({"AGENT", "", phase, msg, duration_ms});
}

// --- CORE ENGINE ---
std::string AgentExecutor::run_autonomous_loop(const ::code_assistance::UserQuery& req, ::grpc::ServerWriter<::code_assistance::AgentResponse>* writer) {
    auto mission_start_time = std::chrono::steady_clock::now();
    
    ContextSnapshot ctx; 
    ctx.history = ""; 
    ctx.architectural_map = ""; 
    ctx.focal_code = "";

    std::string tool_manifest = tool_registry_->get_manifest();
    std::string internal_monologue = "";
    std::unordered_set<size_t> action_history;
    std::hash<std::string> hasher;

    code_assistance::GenerationResult last_gen; 
    std::string final_output = "Mission Timed Out.";
    std::string last_error = ""; // 🧠 SHORT-TERM FAILURE MEMORY

    std::vector<float> prompt_embedding = ai_service_->generate_embedding(req.prompt());
    std::string memories = memory_vault_->recall(prompt_embedding);

    std::vector<float> prompt_vec = ai_service_->generate_embedding(req.prompt());
    std::string relevant_memories = memory_vault_->recall(prompt_vec);
    
    if (!memories.empty()) {
        ctx.history += "\n[SYSTEM: I have handled similar tasks before. Consult MEMORY section.]\n";
    }
    
    int max_steps = 8; // Reduce steps to force efficiency
    
    for (int step = 0; step < max_steps; ++step) {
        // 🚀 DYNAMIC PROMPT CONSTRUCTION
        std::string prompt = 
            "### ROLE: Synapse Autonomous Pilot\n"
            "### TOOLS\n" + tool_manifest + "\n\n"
            "### MISSION\n" + req.prompt() + "\n\n"
            "### PROTOCOL\n"
            "1. Format calls as JSON: {\"tool\": \"name\", \"parameters\": {...}}\n"
            "2. If answer found, use FINAL_ANSWER.\n"
            "3. If a tool fails, ANALYZE the error and FIX your parameters.\n";

        if (!memories.empty()) {
            prompt += memories + "\n"; // Inject Past Successes
        }

        if (!relevant_memories.empty()) {
            prompt += relevant_memories; // The AI now sees its past victories
        }

        if (!internal_monologue.empty()) {
            prompt += "### FLIGHT LOG (HISTORY)\n" + internal_monologue + "\n";
        }

        // 🧠 CRITICAL: INJECT ERROR FEEDBACK
        if (!last_error.empty()) {
            prompt += "\n\n### ⚠️ CRITICAL SYSTEM ALERT\n"
                      "The previous action FAILED. Error Trace:\n" + last_error + "\n"
                      "INSTRUCTION: You MUST change your approach. Do not repeat the same invalid input.\n";
        }

        prompt += "\nNEXT ACTION (JSON):";

        last_gen = ai_service_->generate_text_elite(prompt);
        
        if (!last_gen.success) {
            this->notify(writer, "ERROR", "AI Service Unreachable");
            return "ERROR: AI Service Failure";
        }

        std::string thought = last_gen.text;
        this->notify(writer, "THOUGHT", "Step " + std::to_string(step + 1));

        nlohmann::json action = extract_json(thought);
        
        if (action.contains("tool")) {
            std::string tool_name = action["tool"];
            
            // Loop Detection
            std::string action_sig = tool_name + action["parameters"].dump();
            size_t action_hash = hasher(action_sig);
            
            if (action_history.count(action_hash) && last_error.empty()) {
                 internal_monologue += "\n[SYSTEM: Duplicate action detected. Try something else.]";
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

            // 🛠️ EXECUTE TOOL
            std::string observation = tool_registry_->dispatch(tool_name, params);
            
            // 🧠 ANALYZE RESULT
            if (observation.find("ERROR:") == 0) {
                last_error = observation; // Store for next prompt injection
                internal_monologue += "\n[SYSTEM ERROR: " + observation + "]";
                
                // Telemetry for user visibility
                this->notify(writer, "ERROR_CATCH", "Tool failed. Re-planning...");
            } else {
                last_error = ""; // Clear error on success
                
                if (tool_name == "read_file") {
                    ctx.focal_code += "\nFile: " + params.value("path", "") + "\n" + observation;
                }
                internal_monologue += "\n[RESULT: " + tool_name + "]\n" + observation;
                this->notify(writer, "TOOL_EXEC", "Used " + tool_name);
            }
            
        } else {
            if (thought.find("FINAL_ANSWER") != std::string::npos) {
                final_output = thought;
                goto mission_complete;
            }
            internal_monologue += "\n[SYSTEM: Invalid JSON format. Retry.]";
        }
    }

mission_complete:
    // 🚀 LOGGING (Existing logic)
    auto mission_end_time = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(mission_end_time - mission_start_time).count();

    code_assistance::InteractionLog log;
    log.request_type = "AGENT";
    log.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log.project_id = req.project_id();
    log.user_query = req.prompt();
    log.ai_response = final_output;
    log.duration_ms = total_ms;
    log.prompt_tokens = last_gen.prompt_tokens;
    log.completion_tokens = last_gen.completion_tokens;
    log.total_tokens = last_gen.total_tokens;
    log.full_prompt = "### HISTORY:\n" + internal_monologue + "\n### FOCAL CODE:\n" + ctx.focal_code;

    code_assistance::LogManager::instance().add_log(log);

    if (last_gen.success && final_output != "Mission Timed Out.") {
        // We calculate a score: 1.0 if no errors occurred, 0.8 if we had to self-correct
        double score = (internal_monologue.find("SYSTEM ERROR") == std::string::npos) ? 1.0 : 0.8;
        
        // We memorize the User Prompt -> Final Answer mapping
        // This helps the agent "shortcut" directly to the answer next time
        memory_vault_->add_experience(req.prompt(), final_output, prompt_embedding, score);
    }

    if (final_output != "Mission Timed Out.") {
        // Calculate Score:
        // 1.0 = Flawless run (No "SYSTEM ERROR" in logs)
        // 0.8 = Self-Corrected run (Had errors but fixed them)
        double score = (internal_monologue.find("SYSTEM ERROR") == std::string::npos) ? 1.0 : 0.8;
        
        // We store the Prompt -> Result mapping
        // Next time, the AI will see this Result immediately
        memory_vault_->add_experience(req.prompt(), final_output, prompt_vec, score);
    }

    return final_output;
}

std::string AgentExecutor::run_autonomous_loop_internal(const nlohmann::json& body) {
    ::code_assistance::UserQuery fake_req;
    fake_req.set_prompt(body.value("prompt", ""));
    fake_req.set_project_id(body.value("project_id", "default"));
    return this->run_autonomous_loop(fake_req, nullptr); 
}

void AgentExecutor::determineContextStrategy(const std::string& query, ContextSnapshot& ctx, const std::string& project_id) {
    // Implementation placeholder
}

bool AgentExecutor::check_reflection(const std::string& query, const std::string& topo, std::string& reason) {
    reason = "Pass"; return true; 
}

} // namespace code_assistance