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

// --- HELPER: CLEAN RESPONSE ---
std::string clean_response_text(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    text.erase(std::remove(text.begin(), text.end(), '\f'), text.end());
    size_t pos = 0;
    while ((pos = text.find("\\frac", pos)) != std::string::npos) {
        text.replace(pos, 5, "frac");
        pos += 4;
    }
    return text;
}

// --- HELPER: ROBUST JSON EXTRACTION ---
nlohmann::json extract_json(const std::string& raw) {
    std::string clean = raw;
    
    // 1. Prioritize Explicit Markdown JSON Blocks
    size_t md_start = clean.find("```json");
    if (md_start != std::string::npos) {
        size_t start = clean.find('\n', md_start);
        if (start != std::string::npos) {
            size_t end = clean.find("```", start + 1);
            if (end != std::string::npos) {
                try {
                    return nlohmann::json::parse(clean.substr(start + 1, end - start - 1));
                } catch(...) { /* Fallback if block is malformed */ }
            }
        }
    }

    // 2. Scan for VALID JSON start (Lookahead Check)
    size_t json_start = std::string::npos;
    char start_char = '\0';
    char end_char = '\0';

    for (size_t i = 0; i < clean.length(); ++i) {
        char c = clean[i];
        if (c == '{' || c == '[') {
            for (size_t j = i + 1; j < clean.length(); ++j) {
                char next = clean[j];
                if (std::isspace(next)) continue;
                if (c == '{' && (next == '"' || next == '}')) {
                    json_start = i; start_char = '{'; end_char = '}'; goto found;
                }
                if (c == '[' && (next == '{' || next == '"' || next == ']' || std::isdigit(next))) {
                    json_start = i; start_char = '['; end_char = ']'; goto found;
                }
                break; 
            }
        }
    }
    found:;

    if (json_start == std::string::npos) return nlohmann::json::object();

    // 3. Bracket Counting
    int balance = 0;
    bool in_string = false;
    bool escape = false;
    size_t json_end = std::string::npos;

    for (size_t i = json_start; i < clean.length(); ++i) {
        char c = clean[i];
        if (escape) { escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        
        if (!in_string) {
            if (c == start_char) balance++;
            else if (c == end_char) {
                balance--;
                if (balance == 0) {
                    json_end = i;
                    break;
                }
            }
        }
    }

    std::string json_str = (json_end != std::string::npos) 
        ? clean.substr(json_start, json_end - json_start + 1) 
        : clean.substr(json_start);

    try {
        return nlohmann::json::parse(json_str);
    } catch (...) {
        if (raw.find("def ") != std::string::npos) {
            nlohmann::json f; f["tool"] = "FINAL_ANSWER"; f["parameters"] = {{"answer", raw}}; return f;
        }
    }
    return nlohmann::json::object();
}

std::string sanitize_for_prompt(const std::string& text) {
    std::string safe = text;
    size_t pos = 0;
    while ((pos = safe.find("f\"", pos)) != std::string::npos) {
        size_t end = safe.find("\"", pos + 2);
        if (end != std::string::npos) {
            safe[pos + 1] = '\'';
            safe[end] = '\'';
            pos = end + 1;
        } else break;
    }
    return safe;
}

std::string load_full_context_file(const std::string& project_id) {
    std::string root = FileSystemTools::resolve_project_root(project_id);
    if (root.empty()) return "";
    fs::path full_ctx_path = fs::path(root) / ".study_assistant" / "converted_files" / "_full_context.txt";
    if (!fs::exists(full_ctx_path)) full_ctx_path = fs::path("data") / project_id / "_full_context.txt";
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
        fs::path skill_path;
        if (root_str.empty()) skill_path = fs::path("data") / "business_metadata";
        else skill_path = fs::path(root_str) / ".study_assistant" / "business_metadata";
        spdlog::info("🧠 Loading Skills for {} from {}", project_id, skill_path.string());
        skill_libraries_[project_id] = std::make_shared<SkillLibrary>(skill_path.string(), ai_service_);
    }
    return skill_libraries_[project_id];
}

std::shared_ptr<PointerGraph> AgentExecutor::get_or_create_graph(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    if (graphs_.find(project_id) == graphs_.end()) {
        std::string safe_id = project_id;
        std::replace(safe_id.begin(), safe_id.end(), ':', '_');
        std::replace(safe_id.begin(), safe_id.end(), '/', '_');
        std::replace(safe_id.begin(), safe_id.end(), '\\', '_');
        std::string path = "data/graphs/" + safe_id;
        if (!fs::exists(path)) fs::create_directories(path);
        spdlog::info("📂 Loading Graph for Project: {} at {}", project_id, path);
        graphs_[project_id] = std::make_shared<PointerGraph>(path);
    }
    return graphs_[project_id];
}

void AgentExecutor::ingest_sync_results(const std::string& project_id, const std::vector<std::shared_ptr<CodeNode>>& nodes) {
    auto graph = get_or_create_graph(project_id);

    size_t before_count = graph->get_node_count(); // You might need to add this getter to PointerGraph
    spdlog::info("🧠 [GRAPH INGESTION] Starting injection of {} nodes...", nodes.size());

    for (const auto& node : nodes) {
        std::unordered_map<std::string, std::string> meta;
        meta["file_path"] = node->file_path;
        meta["node_name"] = node->name;
        meta["node_type"] = node->type;
        std::string deps = "";
        for(const auto& d : node->dependencies) deps += d + ",";
        meta["dependencies"] = deps;
        graph->add_node(node->content, NodeType::CONTEXT_CODE, "", node->embedding, meta);
    }
    graph->save();
    spdlog::info("✅ [GRAPH INGESTION] Success. Total Memory Nodes: {}", graph->get_node_count());
}

std::string AgentExecutor::restore_session_cursor(std::shared_ptr<PointerGraph> graph, const std::string& session_id) {
    auto nodes = graph->query_by_metadata("session_id", session_id);
    if (nodes.empty()) return "";
    PointerNode latest = nodes[0];
    for(const auto& n : nodes) {
        if (n.timestamp > latest.timestamp) latest = n;
    }
    spdlog::info("🔄 Restored Session '{}' cursor to node: {}", session_id, latest.id);
    return latest.id;
}

std::string AgentExecutor::run_autonomous_loop(const ::code_assistance::UserQuery& req, ::grpc::ServerWriter<::code_assistance::AgentResponse>* writer) {
    auto mission_start_time = std::chrono::steady_clock::now();
    
    // 1. Setup Graph & Session
    auto graph = get_or_create_graph(req.project_id());
    std::string session_id = req.session_id();
    
    // 2. GENERATE EMBEDDING FIRST (Needed for search)
    std::vector<float> prompt_vec = ai_service_->generate_embedding(req.prompt());

    // 3. PERFORM SIGMA-2 RETRIEVAL (Now that we have prompt_vec)
    auto top_nodes = graph->semantic_search(prompt_vec, 5);
    std::string relational_context = "### RELATED CODE RELATIONSHIPS (Sigma-2)\n";
    std::string massive_context = ""; // Declare this here so we can add to it

    for (const auto& node : top_nodes) {
        try {
            auto related = graph->get_children(node.id);
            for (const auto& r_node : related) {
                // 🛡️ CRITICAL: Never use .at() or [] in a threaded agent. Use .find()
                auto it_name = r_node.metadata.find("node_name");
                std::string r_name = (it_name != r_node.metadata.end()) ? it_name->second : "anonymous_symbol";
                
                relational_context += "- " + node.id + " -> links to -> " + r_name + "\n";
                
                if (r_node.type == NodeType::CONTEXT_CODE) {
                    auto it_path = r_node.metadata.find("file_path");
                    std::string r_path = (it_path != r_node.metadata.end()) ? it_path->second : "unknown_file";
                    massive_context += "\n# FILE: " + r_path + "\n" + r_node.content + "\n";
                }
            }
        } catch (...) {
            spdlog::warn("⚠️ Graph traversal safety-tripped for node {}", node.id);
        }
    }

    // 4. Restore Session Cursor
    std::string parent_node_id = "";
    {
        std::lock_guard<std::mutex> lock(cursor_mutex_);
        if (session_cursors_.find(session_id) == session_cursors_.end()) {
            session_cursors_[session_id] = restore_session_cursor(graph, session_id);
        }
        parent_node_id = session_cursors_[session_id];
    }

    // Variables
    std::string tool_manifest = tool_registry_->get_manifest();
    std::string internal_monologue = "";
    std::string final_output = "Mission Timed Out.";
    std::string last_error = ""; 
    std::string last_effective_prompt = ""; 
    code_assistance::GenerationResult last_gen; 

    // Record User Prompt
    prompt_vec = ai_service_->generate_embedding(req.prompt());
    std::string root_node_id = graph->add_node(req.prompt(), NodeType::PROMPT, parent_node_id, prompt_vec, {{"session_id", session_id}});
    std::string last_graph_node = root_node_id;

    // Retrieve Skills
    auto skill_lib = get_skill_library(req.project_id());
    std::string business_context = skill_lib->retrieve_skills(req.prompt(), prompt_vec);

    // 🚀 RETRIEVE & FORMAT HISTORY (DEDUPLICATED)
    if (!parent_node_id.empty()) {
        auto trace = graph->get_trace(parent_node_id);
        
        // Take more history to ensure we see past tool usage
        int start_idx = std::max(0, (int)trace.size() - 25);
        
        std::string last_user_content = ""; 
        std::unordered_set<std::string> content_hashes; // For deduplication

        for (size_t i = start_idx; i < trace.size(); ++i) {
            const auto& node = trace[i];
            
            // Hash content for dedupe
            std::string hash_key = node.content;
            
            if (node.type == NodeType::PROMPT) {
                if (node.content == last_user_content) continue;
                internal_monologue += "\n\n👤 [USER REQUEST]\n" + node.content;
                last_user_content = node.content;
            } 
            else if (node.type == NodeType::SYSTEM_THOUGHT) {
                // Keep thoughts short in history
                internal_monologue += "\n💭 [THOUGHT] " + node.content;
            } 
            else if (node.type == NodeType::TOOL_CALL) {
                std::string tname = node.metadata.count("tool") ? node.metadata.at("tool") : "tool";
                // Extract arguments from content if possible (usually content is tool name + args)
                internal_monologue += "\n▶️ [ACTION] " + node.content;
            } 
            else if (node.type == NodeType::CONTEXT_CODE) {
                // 🚀 SMART DEDUPLICATION:
                // If we've seen this EXACT tool output before in this trace, summarise it.
                // BUT: Always show it if it was the VERY LAST thing (i = size - 1)
                bool is_duplicate = content_hashes.count(hash_key);
                bool is_recent = (i >= trace.size() - 2);

                internal_monologue += "\n### 🛠️ OBSERVATION (Result)\n";
                if (is_duplicate && !is_recent) {
                    internal_monologue += "(...Result same as previous step to save context...)\n";
                } else {
                    // Truncate massive outputs in history, but allow reasonably large ones
                    if (node.content.length() > 2000 && !is_recent) {
                        internal_monologue += "```\n" + node.content.substr(0, 2000) + "\n... (Truncated history)\n```";
                    } else {
                        internal_monologue += "```\n" + node.content + "\n```";
                    }
                    content_hashes.insert(hash_key);
                }
            } 
            else if (node.type == NodeType::RESPONSE) {
                internal_monologue += "\n🤖 [AI REPLY] " + node.content;
            }
        }
    }

    // Memory Recall
    std::string memories = "";
    std::string warnings = ""; 
    if (!prompt_vec.empty()) {
        MemoryRecallResult long_term = memory_vault_->recall(prompt_vec);
        if(long_term.has_memories) {
            if(!long_term.positive_hints.empty()) memories += "\n### 🧠 SUCCESSFUL STRATEGIES\n" + long_term.positive_hints;
            if(!long_term.negative_warnings.empty()) warnings += "\n### ⛔ KNOWN PITFALLS\n" + long_term.negative_warnings;
        }
    }

    // Load Full Context
    massive_context = "";
    std::string full_codebase = load_full_context_file(req.project_id());
    if (!full_codebase.empty()) {
        const size_t SAFE_TOKEN_LIMIT_BYTES = 3800000; 
        if (full_codebase.size() > SAFE_TOKEN_LIMIT_BYTES) massive_context += "\n### 📚 FULL CODEBASE (Truncated)\n" + full_codebase.substr(0, SAFE_TOKEN_LIMIT_BYTES) + "\n";
        else massive_context += "\n### 📚 FULL CODEBASE\n" + full_codebase + "\n";
    }

    // Loop Control
    int max_steps = 16;
    for (int step = 0; step < max_steps; ++step) {
        
        std::string prompt_template = 
            "### SYSTEM ROLE\n"
            "You are 'Synapse', an Autonomous Coding Agent.\n\n"
            "### TOOL MANIFEST\n" + tool_manifest + "\n"
            "🚀 BATCH MODE ENABLED: You are encouraged to return a JSON LIST `[...]` of multiple tool calls to save time.\n"
            "Example: `[ {\"tool\": \"apply_edit\", ...}, {\"tool\": \"execute_code\", ...} ]`\n"
            "If you are confident, perform the edit, execution, and final answer in ONE response.\n\n"
            "### USER REQUEST\n" + req.prompt() + "\n\n";

        prompt_template += 
            "\n### 🚨 CRITICAL JSON FORMATTING RULES 🚨\n"
            "1. **INDENTATION IS VITAL**: When writing Python code in JSON, you MUST include proper indentation.\n"
            "   ❌ WRONG: \"def foo():\\nreturn 1\"\n"
            "   ✅ RIGHT: \"def foo():\\n    return 1\" (Notice the spaces after \\n)\n"
            "2. **SINGLE QUOTES**: Use single quotes for Python strings: print('hello').\n"
            "3. **NO LATEX**: Do NOT use LaTeX formulas (like \\frac) in the output text. It breaks the display. Use plain text like (1/pi).\n"
            "4. **OUTPUT VALID JSON**: Start with `[`.\n"
            "5. **ESCAPE PROPERLY**: All newlines must be \\n, all tabs must be \\t, all quotes inside strings must be escaped.\n";

        prompt_template += 
            "### 🛑 CODE GENERATION RULE 🛑\n"
            "1. Write the full Python code inside a ```python block FIRST.\n"
            "2. Then, inside your JSON, set \"content\": \"__CODE_BLOCK_0__\".\n"
            "3. My system will automatically inject the code block into the file.\n";

        prompt_template += relational_context;

        if (!business_context.empty()) prompt_template += business_context + "\n";
        if (!massive_context.empty()) prompt_template += massive_context + "\n";

        std::string plan_ctx = planning_engine_->get_plan_context_for_ai();
        if (!plan_ctx.empty()) prompt_template += plan_ctx + "\n";

        if (!memories.empty()) prompt_template += memories + "\n";
        if (!internal_monologue.empty()) prompt_template += "### EXECUTION HISTORY (Read-Only)\n" + internal_monologue + "\n";
        if (!warnings.empty()) prompt_template += warnings + "\n";
        if (!last_error.empty()) prompt_template += "\n### ⚠️ PREVIOUS ERROR\n" + last_error + "\nREQUIRED: Fix this error.\n";
            
        last_effective_prompt = prompt_template;
        spdlog::debug("📝 PROMPT TO AI (Truncated):\n{}", prompt_template.substr(0, 1000));

        this->notify(writer, "THINKING", "Processing logic...");
        last_gen = ai_service_->generate_text_elite(prompt_template);
        
        if (!last_gen.success) {
            final_output = "ERROR: AI Service Failure";
            goto mission_complete;
        }

        std::string raw_thought = last_gen.text;

        spdlog::info("\n==================================================");
        spdlog::info("🤖 RAW SCRAPER/AI OUTPUT (START):");
        std::cout << raw_thought << std::endl; 
        spdlog::info("🤖 RAW SCRAPER/AI OUTPUT (END)");
        spdlog::info("==================================================\n");

        // Code Extraction Logic
        std::string extracted_code = "";
        std::vector<std::string> code_blocks;
        size_t code_start = raw_thought.find("```python");
        
        if (code_start == std::string::npos) {
            code_start = raw_thought.find("```");
            if (code_start != std::string::npos && raw_thought.substr(code_start, 7) == "```json") {
                code_start = std::string::npos;
            }
        }

        if (code_start == std::string::npos) {
            size_t json_start = std::string::npos;
            for (size_t i = 0; i < raw_thought.length(); ++i) {
                if (raw_thought[i] == '[') {
                    for (size_t k = i + 1; k < raw_thought.length(); ++k) {
                        char next = raw_thought[k];
                        if (std::isspace(next)) continue;
                        if (next == '{') { json_start = i; goto found_split; }
                        break;
                    }
                }
            }
            found_split:;

            if (json_start != std::string::npos && json_start > 10) {
                std::string pre_json = raw_thought.substr(0, json_start);
                if (pre_json.find("import ") != std::string::npos || pre_json.find("def ") != std::string::npos) {
                    size_t word_py = pre_json.find("Python\n");
                    if (word_py != std::string::npos) pre_json = pre_json.substr(word_py + 7);
                    extracted_code = pre_json;
                    extracted_code.erase(0, extracted_code.find_first_not_of(" \n\r\t"));
                    extracted_code.erase(extracted_code.find_last_not_of(" \n\r\t") + 1);
                    if (!extracted_code.empty()) {
                        code_blocks.push_back(extracted_code);
                        spdlog::info("⚠️ Auto-Recovered code block (Smart Split).");
                    }
                }
            }
        } 
        else {
            size_t start = raw_thought.find('\n', code_start) + 1;
            size_t end = raw_thought.find("```", start);
            if (end != std::string::npos) {
                extracted_code = raw_thought.substr(start, end - start);
                code_blocks.push_back(extracted_code);
            }
        }
        
        nlohmann::json extracted = extract_json(raw_thought);
        spdlog::info("🧩 PARSED JSON RESULT:\n{}", extracted.dump(2)); 
        std::vector<nlohmann::json> actions;

        if (extracted.is_array()) {
            for (const auto& item : extracted) actions.push_back(item);
            spdlog::info("🚀 Batch Mode: Detected {} actions in one response.", actions.size());
        } else {
            actions.push_back(extracted);
        }

        bool batch_aborted = false;

        for (auto& action : actions) {
            if (batch_aborted) break; 

            std::string tool_name = "";
            if (action.contains("tool")) tool_name = action["tool"];
            else if (action.contains("name")) tool_name = action["name"];
            else if (action.contains("function")) tool_name = action["function"];

            if (tool_name.empty()) {
                if (actions.size() == 1) {
                    final_output = last_gen.text;
                    last_graph_node = graph->add_node(final_output, NodeType::RESPONSE, last_graph_node);
                    this->notify(writer, "FINAL", final_output);
                    goto mission_complete;
                }
                continue;
            }

            nlohmann::json params;
            if (action.contains("parameters")) params = action["parameters"];
            else if (action.contains("arguments")) params = action["arguments"];
            else if (action.contains("args")) params = action["args"];
            else {
                params = action;
                if (params.contains("tool")) params.erase("tool");
                if (params.contains("name")) params.erase("name");
                if (params.contains("function")) params.erase("function");
                if (params.contains("thought")) params.erase("thought");
            }

            if (params.contains("content")) {
                std::string content = params["content"].get<std::string>();
                std::regex placeholder_re(R"((?:__|)CODE_BLOCK_(\d+)(?:__|))");
                std::smatch m;
                if (std::regex_search(content, m, placeholder_re)) {
                    int idx = std::stoi(m[1].str());
                    if (idx >= 0 && static_cast<size_t>(idx) < code_blocks.size()) {
                        params["content"] = code_blocks[idx];
                        spdlog::info("💉 Injected Code Block {} ({} chars)", idx, code_blocks[idx].length());
                    }
                } 
                else if (code_blocks.size() == 1 && (content.find("CODE_BLOCK") != std::string::npos || content.length() < 20)) {
                     params["content"] = code_blocks[0];
                     spdlog::info("💉 Auto-Injected Single Code Block (Fallback)");
                }
            }

            params["project_id"] = req.project_id();

            if (action.contains("thought")) {
                std::string reasoning = action["thought"];
                last_graph_node = graph->add_node(reasoning, NodeType::SYSTEM_THOUGHT, last_graph_node);
                internal_monologue += "\n💭 [THOUGHT] " + reasoning; // Update current context immediately
                this->notify(writer, "PLANNING", reasoning);
            }

            params["_batch_mode"] = true; 

            if (tool_name == "propose_plan") {
                MemoryRecallResult past_experiences = memory_vault_->recall(prompt_vec);

                if (past_experiences.has_memories) {
                    // Force the AI to reconsider the plan based on past failures
                    internal_monologue += "\n⚠️ WAIT: Recalling past similar tasks...\n" + 
                                        past_experiences.negative_warnings;
                                        
                    // Re-generate the prompt to include these warnings before the plan is finalized
                    continue; 
                }

                if (params.contains("steps")) {
                    planning_engine_->propose_plan(req.prompt(), params["steps"]);
                    if (actions.size() > 1) {
                        planning_engine_->approve_plan();
                        this->notify(writer, "PLANNING", "Plan proposed and auto-approved for batch execution.");
                    } else {
                        ::code_assistance::AgentResponse plan_res;
                        plan_res.set_phase("PROPOSAL");
                        plan_res.set_payload(planning_engine_->get_snapshot().to_json().dump()); 
                        if (writer) writer->Write(plan_res);
                        final_output = "Plan Proposed.";
                        goto mission_complete; 
                    }
                }
            }

            GuardResult guard = ExecutionGuard::validate_tool_call(tool_name, params, planning_engine_.get());
            if (!guard.allowed) {
                spdlog::warn("🛑 Guard Blocked Action: {}", guard.reason);
                this->notify(writer, "BLOCKED", guard.reason);
                internal_monologue += "\n🛑 [BLOCKED] " + guard.reason;
                last_error = guard.reason;
                batch_aborted = true; 
                continue;
            }

            this->notify(writer, "TOOL_EXEC", "Running " + tool_name);
            std::string observation = safe_execute_tool(tool_name, params, session_id);

            if (tool_name == "apply_edit" && observation.find("SUCCESS") != std::string::npos) {
                this->notify(writer, "VERIFYING", "Running automated build check...");
                
                // Automatically trigger a build/test tool based on project type
                nlohmann::json verify_params;
                verify_params["command"] = "python -m py_compile " + params["path"].get<std::string>(); // For Python
                verify_params["project_id"] = req.project_id();
                
                std::string build_log = safe_execute_tool("run_command", verify_params, session_id);
                
                if (build_log.find("Exit Code: 0") == std::string::npos) {
                    // If build fails, overwrite observation to force AI to see the error immediately
                    observation = "⚠️ EDIT APPLIED BUT BUILD FAILED:\n" + build_log + 
                                "\nACTION REQUIRED: Re-read the file and fix the syntax error.";
                    this->notify(writer, "AUTO_REPAIR", "Build failed. Feeding error back to Brain.");
                }
            }

            if (planning_engine_->is_plan_approved()) {
                auto plan = planning_engine_->get_snapshot();
                if (plan.current_step_idx < plan.steps.size()) {
                    planning_engine_->mark_step_status(plan.current_step_idx, StepStatus::SUCCESS, observation);
                }
            }

            std::string sig = tool_name; 
            if (params.contains("path")) sig += " " + params["path"].get<std::string>();
            
            // Record Graph with metadata
            last_graph_node = graph->add_node(sig, NodeType::TOOL_CALL, last_graph_node, {}, {{"tool", tool_name}});
            last_graph_node = graph->add_node(observation, NodeType::CONTEXT_CODE, last_graph_node);

            // Update local loop monologue immediately for next iterations in batch (or next step)
            internal_monologue += "\n▶️ [ACTION] " + sig;
            internal_monologue += "\n### 🛠️ OBSERVATION (Result)\n```\n" + observation + "\n```";

            if (observation.find("ERROR:") == 0 || observation.find("SYSTEM_ERROR") == 0) {
                memory_vault_->add_failure(req.prompt(), "Tool Failed: " + tool_name, prompt_vec);
                last_error = observation;
                this->notify(writer, "ERROR_CATCH", "Action failed. Halting batch.");
                batch_aborted = true;
            }

            if (tool_name == "FINAL_ANSWER") {
                final_output = params.value("answer", "");
                last_graph_node = graph->add_node(final_output, NodeType::RESPONSE, last_graph_node, {}, {{"status", "success"}});
                if (last_error.empty()) {
                    memory_vault_->add_success(req.prompt(), "Solved via: " + internal_monologue.substr(0, 500), prompt_vec);
                }
                this->notify(writer, "FINAL", final_output);
                goto mission_complete; 
            }
        } 
    } 

mission_complete:
    {
        std::lock_guard<std::mutex> lock(cursor_mutex_);
        session_cursors_[session_id] = last_graph_node;
    }
    
    final_output = clean_response_text(final_output);

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

    spdlog::info("🛠️ [TOOL START] {} | Params: {}", tool_name, params.dump());
    
    try {
        // --- 🛡️ START AST-GUARD INTEGRATION ---
        if (tool_name == "apply_edit") {
            std::string code = params.value("content", "");
            std::string path = params.value("path", "");
            std::string ext = fs::path(path).extension().string();

            // Use the Elite Parser to check syntax before writing to disk
            code_assistance::elite::ASTBooster temp_parser;
            if (!temp_parser.validate_syntax(code, ext)) {
                failed = true;
                result = "ERROR: AST REJECTION. Your proposed code for '" + path + 
                         "' contains syntax or indentation errors. Please fix the structure and try again.";
                spdlog::warn("🚫 [AST GUARD] Blocked broken code injection for {}", path);
            }
        }
        // --- 🛡️ END AST-GUARD INTEGRATION ---

        // Only dispatch if the guard hasn't already flagged a failure
        if (!failed) {
            result = tool_registry_->dispatch(tool_name, params);
            if (result.find("ERROR:") == 0) {
                failed = true;
                spdlog::warn("⚠️ [TOOL FAIL] {} | Reason: {}", tool_name, result);
            } else {
                spdlog::info("✅ [TOOL OK] {} | Output Size: {} chars", tool_name, result.size());
            }
        }
        
    } catch (const std::exception& e) {
        failed = true;
        result = "SYSTEM EXCEPTION: " + std::string(e.what());
        spdlog::error("💥 [TOOL CRASH] {} | Exception: {}", tool_name, e.what());
    } catch (...) {
        failed = true;
        result = "SYSTEM EXCEPTION: Unknown Critical Failure";
        spdlog::critical("💥 [TOOL CRASH] {} | Unknown Signal", tool_name);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    // The trace will now correctly show "FAILED: apply_edit -> ERROR: AST REJECTION..." 
    // if the code was broken.
    std::string state = failed ? "ERROR_CATCH" : "TOOL_EXEC";
    code_assistance::LogManager::instance().add_trace({
        session_id, 
        "", 
        state, 
        (failed ? "FAILED: " : "SUCCESS: ") + tool_name + " -> " + result.substr(0, 100), 
        duration
    });

    return result;
}

void AgentExecutor::determineContextStrategy(const std::string& query, ContextSnapshot& ctx, const std::string& project_id) {}
bool AgentExecutor::check_reflection(const std::string& query, const std::string& topo, std::string& reason) { return true; }
std::string AgentExecutor::construct_reasoning_prompt(const std::string& task, const std::string& history, const std::string& last_error) { return ""; }

} // namespace code_assistance