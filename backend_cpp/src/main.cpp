#include <httplib.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <filesystem>
#include <thread>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <signal.h>

#include "utils/Scrubber.hpp" 

#include "KeyManager.hpp"
#include "LogManager.hpp"
#include "ThreadPool.hpp"
#include "sync_service.hpp"
#include "SystemMonitor.hpp"
#include "embedding_service.hpp"
#include "faiss_vector_store.hpp"

#include "agent/SubAgent.hpp"
#include "agent/AgentExecutor.hpp"

#include "tools/ToolRegistry.hpp"
#include "tools/FileSystemTools.hpp"
#include "tools/FileSurgicalTool.hpp"
#include "tools/PatternSearchTool.hpp"
#include "tools/CodeExecutionTool.hpp"
#include "tools/ShellExecutionTool.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace code_assistance {
    std::string web_search(const std::string& args_json, const std::string& api_key);
}

std::unique_ptr<httplib::Server> global_server_ptr;

void signal_handler(int signum) {
    spdlog::info("🛑 Interrupt signal ({}) received. Shutting down...", signum);
    if (global_server_ptr) {
        global_server_ptr->stop();
    }
}


class CodeAssistanceServer {
public:
    CodeAssistanceServer(int port = 5002) : port_(port), thread_pool_(4) {
        key_manager_ = std::make_shared<code_assistance::KeyManager>();
        ai_service_ = std::make_shared<code_assistance::EmbeddingService>(key_manager_);
        sub_agent_ = std::make_shared<code_assistance::SubAgent>();
        tool_registry_ = std::make_shared<code_assistance::ToolRegistry>();
        
        auto memory_vault = std::make_shared<code_assistance::MemoryVault>("data/memory_vault");
        
        tool_registry_->register_tool(std::make_unique<code_assistance::ReadFileTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::ListDirTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::FileSurgicalTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::PatternSearchTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::CodeExecutionTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::ShellExecutionTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::GenericTool>(
            "FINAL_ANSWER",
            "Mission Completion Signal",
            "{}",
            [](const std::string&) { return "Mission Completed. Terminating loop."; }
        ));
        tool_registry_->register_tool(std::make_unique<code_assistance::GenericTool>(
            "debug_memory",
            "Shows the current long-term memory stats. Input: {}",
            "{}",
            [memory_vault](const std::string&) { 
                return "Memory Vault Stats: (Check server logs for details)"; 
            }
        ));

        tool_registry_->register_tool(std::make_unique<code_assistance::GenericTool>(
            "clear_memory",
            "Wipes all long-term memories. Input: {}",
            "{}",
            [memory_vault](const std::string&) { 
                return "Memory Vault Cleared."; 
            }
        ));
        
        executor_ = std::make_shared<code_assistance::AgentExecutor>(
            nullptr, ai_service_, sub_agent_, tool_registry_, memory_vault // 🚀 Pass Vault
        );

        setup_routes();

        std::thread([this]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                
                auto stats = system_monitor_.get_latest_snapshot();
                
                // If RAM > 2GB (adjust as needed), clear caches
                if (stats.ram_usage_mb > 2048) {
                    spdlog::warn("⚠️ High Memory Usage ({} MB). Purging Caches...", stats.ram_usage_mb);
                    
                    {
                        std::lock_guard<std::mutex> lock(cache_mutex_);
                        project_context_cache_.clear();
                    }
                    
                    // Assuming EmbeddingService exposes cache clearing or we add it
                    // ai_service_->clear_cache(); 
                    
                    // Force OS to reclaim memory (Linux specific, but useful concept)
                    #ifdef __linux__
                    malloc_trim(0);
                    #endif
                }
            }
        }).detach();
    }

    void run() {
        spdlog::info("🚀 REST Server (Ghost Text & Sync) listening on port {}", port_);
        server_.listen("0.0.0.0", port_);
    }

private:
    int port_;
    httplib::Server server_;
    ThreadPool thread_pool_;
    std::mutex store_mutex;
    
    std::shared_ptr<code_assistance::KeyManager> key_manager_;
    std::shared_ptr<code_assistance::EmbeddingService> ai_service_;
    std::shared_ptr<code_assistance::SubAgent> sub_agent_;
    std::shared_ptr<code_assistance::ToolRegistry> tool_registry_;
    std::shared_ptr<code_assistance::AgentExecutor> executor_;
    std::unordered_map<std::string, std::shared_ptr<code_assistance::FaissVectorStore>> project_stores_;
    code_assistance::SystemMonitor system_monitor_;
    std::unordered_map<std::string, std::string> project_context_cache_;
    std::mutex cache_mutex_;

    // --- HELPER METHODS (Must be inside class) ---

    std::shared_ptr<code_assistance::FaissVectorStore> load_vector_store(const std::string& project_id) {
        std::lock_guard<std::mutex> lock(store_mutex);
        if (project_stores_.count(project_id)) return project_stores_[project_id];

        fs::path vector_path = fs::path("data") / "graphs" / project_id;
        if (!fs::exists(vector_path)) return nullptr;

        try {
            auto store = std::make_shared<code_assistance::FaissVectorStore>(768);
            store->load(vector_path.string());
            project_stores_[project_id] = store;
            return store;
        } catch (...) { return nullptr; }
    }

    json load_project_config(const std::string& project_id) {
        fs::path default_path = fs::path("data") / project_id / "config.json";
        if(!fs::exists(default_path)) return json({});
        try { std::ifstream f(default_path); json c; f >> c; return c; } catch (...) { return json({}); }
    }

    void refresh_context_cache(const std::string& project_id, const fs::path& storage_path) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        std::stringstream ss;
        
        // 1. Inject Topology (Tree)
        fs::path tree_path = storage_path / "tree.txt";
        if (fs::exists(tree_path)) { 
            ss << "### PROJECT TOPOLOGY\n";
            std::ifstream f(tree_path); 
            ss << f.rdbuf() << "\n\n"; 
        }

        // 2. Inject Aggregated Source Code
        fs::path full_path = storage_path / "_full_context.txt";
        if (fs::exists(full_path)) {
            std::ifstream f(full_path);
            if (f.is_open()) {
                ss << "### FULL PROJECT CONTEXT\n";
                
                // 🚀 UNLIMITED READ: Reads the entire file buffer directly into the stream
                // This will consume as much RAM as the file size requires.
                std::string raw_content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                ss << code_assistance::scrub_json_string(raw_content); 
                
                ss << "\n";
            }
        }
        
        project_context_cache_[project_id] = ss.str();
        spdlog::info("🧠 RAM Cache Hot-Loaded for '{}'. Size: {:.2f} MB", 
                     project_id, project_context_cache_[project_id].size() / 1024.0 / 1024.0);

        double size_mb = project_context_cache_[project_id].size() / (1024.0 * 1024.0);
        code_assistance::SystemMonitor::global_cache_size_mb.store(size_mb);
    }

    // --- ROUTE HANDLERS (Moved INSIDE class) ---

    // --- 1. Registration Handler ---
    void handle_register_project(const httplib::Request& req, httplib::Response& res) {
        try {
            std::string project_id = req.path_params.at("project_id");
            
            // 🔍 LOG 1: Raw body inspection
            spdlog::info("📦 Received body length: {} bytes", req.body.length());
            
            // 🔍 LOG 2: Dump first 100 bytes in hex
            std::stringstream hex_dump;
            for (size_t i = 0; i < std::min(size_t(200), req.body.length()); ++i) {
                hex_dump << std::hex << std::setw(2) << std::setfill('0') 
                        << (int)(unsigned char)req.body[i] << " ";
                if ((i + 1) % 16 == 0) hex_dump << "\n";
            }
            spdlog::debug("🔍 First 200 bytes (hex):\n{}", hex_dump.str());
            
            // 🔍 LOG 3: Check for problem area around index 5115
            if (req.body.length() > 5115) {
                std::stringstream problem_area;
                size_t start = std::max(size_t(0), size_t(5100));
                size_t end = std::min(req.body.length(), size_t(5130));
                
                spdlog::warn("🔍 Inspecting bytes 5100-5130 (where error occurred):");
                for (size_t i = start; i < end; ++i) {
                    unsigned char byte = req.body[i];
                    problem_area << std::hex << std::setw(2) << std::setfill('0') 
                                << (int)byte << " ";
                    
                    // Flag suspicious bytes
                    if (byte < 0x20 && byte != 0x09 && byte != 0x0A && byte != 0x0D) {
                        spdlog::error("⚠️ Control char at index {}: 0x{:02X}", i, byte);
                    }
                    if (byte >= 0x80 && byte < 0xC0) {
                        spdlog::error("⚠️ Invalid UTF-8 continuation byte at {}: 0x{:02X}", i, byte);
                    }
                }
                spdlog::warn("Hex dump around error: {}", problem_area.str());
            }
            
            // 🔍 LOG 4: Save raw body to file BEFORE scrubbing
            std::ofstream raw_dump("DEBUG_RAW_BODY.bin", std::ios::binary);
            raw_dump.write(req.body.data(), req.body.size());
            raw_dump.close();
            spdlog::info("💾 Raw body saved to DEBUG_RAW_BODY.bin");
            
            // 🔍 LOG 5: Try scrubbing
            spdlog::info("🧹 Starting scrubbing process...");
            std::string safe_body = code_assistance::scrub_json_string(req.body);
            spdlog::info("✅ Scrubbing complete. New length: {} bytes", safe_body.length());
            
            // 🔍 LOG 6: Save scrubbed body
            std::ofstream scrubbed_dump("DEBUG_SCRUBBED_BODY.txt");
            scrubbed_dump << safe_body;
            scrubbed_dump.close();
            spdlog::info("💾 Scrubbed body saved to DEBUG_SCRUBBED_BODY.txt");
            
            // 🔍 LOG 7: Try parsing with detailed error catching
            nlohmann::json body;
            try {
                spdlog::info("🔍 Attempting JSON parse (lenient mode)...");
                body = nlohmann::json::parse(safe_body, nullptr, false);
                
                if (body.is_discarded()) {
                    spdlog::error("❌ Parse returned 'discarded' - JSON is malformed");
                    
                    // Try to find where it breaks
                    for (size_t test_len = 100; test_len < safe_body.length(); test_len += 100) {
                        try {
                            auto test = nlohmann::json::parse(safe_body.substr(0, test_len), nullptr, false);
                            if (test.is_discarded()) {
                                spdlog::error("🔍 JSON breaks somewhere between {} and {} bytes", test_len - 100, test_len);
                                
                                // Narrow it down
                                for (size_t i = test_len - 100; i < test_len; ++i) {
                                    spdlog::error("  Byte {}: 0x{:02X} ('{}')", 
                                        i, 
                                        (unsigned char)safe_body[i],
                                        std::isprint(safe_body[i]) ? safe_body[i] : '?');
                                }
                                break;
                            }
                        } catch (...) {}
                    }
                    
                    throw std::runtime_error("JSON parsing returned discarded object");
                }
                
                spdlog::info("✅ JSON parsed successfully");
                
            } catch (const nlohmann::json::parse_error& e) {
                spdlog::error("❌ JSON Parse Error: {}", e.what());
                spdlog::error("   Error at byte: {}", e.byte);
                
                // Show context around error
                if (e.byte < safe_body.length()) {
                    size_t ctx_start = std::max(size_t(0), e.byte - 50);
                    size_t ctx_end = std::min(safe_body.length(), e.byte + 50);
                    spdlog::error("   Context: '{}'", safe_body.substr(ctx_start, ctx_end - ctx_start));
                }
                throw;
            }
            
            if (!body.is_object()) {
                spdlog::error("❌ JSON is not an object, it's a: {}", body.type_name());
                res.status = 400;
                res.set_content("{\"error\":\"JSON must be an object\"}", "application/json");
                return;
            }
            
            // Rest of your original code...
            fs::path project_dir = fs::path("data") / project_id;
            fs::create_directories(project_dir);
            
            std::ofstream f(project_dir / "config.json");
            f << body.dump(2);
            
            spdlog::info("🛰️ Project Registered: {}", project_id);
            res.set_content(json{{"success", true}}.dump(), "application/json");
            
        } catch (const std::exception& e) {
            std::ofstream crash_file("DEBUG_CRASH_DUMP.txt");
            crash_file << "ERROR: " << e.what() << "\n\n";
            crash_file << "BODY LENGTH: " << req.body.length() << "\n";
            crash_file << "BODY PREVIEW (first 1000 chars):\n" 
                    << req.body.substr(0, std::min(size_t(1000), req.body.length()));
            crash_file.close();
            
            spdlog::error("🔥 CRASH SAVED TO DEBUG_CRASH_DUMP.txt: {}", e.what());
            spdlog::error("❌ Registration Failed: {}", e.what());
            
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    }

    void handle_sync_project(const httplib::Request& req, httplib::Response& res) {
        try {
            auto project_id = req.path_params.at("project_id");
            json config = load_project_config(project_id);
            std::string store_path = config.value("storage_path", "");
            if(store_path.empty()) store_path = (fs::path("data") / project_id).string();
            
            thread_pool_.enqueue([this, project_id, store_path]() {
                auto t_start = std::chrono::high_resolution_clock::now();

                // A. Run Sync
                json config = load_project_config(project_id);
                code_assistance::SyncService sync(ai_service_);
                
                // Perform Sync
                auto sync_res = sync.perform_sync(project_id, config.value("local_path",""), store_path, {}, {}, {});
                
                // B. 🚀 UNIFIED INGESTION: Feed results to AgentExecutor's Graph
                if (!sync_res.nodes.empty()) {
                    executor_->ingest_sync_results(project_id, sync_res.nodes);
                }

                // C. Hot-Load RAM Context (Optional now, as Graph handles retrieval)
                this->refresh_context_cache(project_id, store_path);

                auto t_end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
                code_assistance::SystemMonitor::global_sync_latency_ms.store(ms);
                spdlog::info("⏱️ Sync Complete in {:.2f} ms", ms);
            });

            res.set_content(json{{"success", true}}.dump(), "application/json");
        } catch(...) { res.status = 500; }
    }

    void handle_generate_suggestion(const httplib::Request& req, httplib::Response& res) {
        try {
            spdlog::info("🎯 AGENT REQUEST - Body size: {} bytes", req.body.length());
            
            std::string safe_body = code_assistance::scrub_json_string(req.body);
            nlohmann::json body = nlohmann::json::parse(safe_body, nullptr, false);
            
            if (body.is_discarded()) {
                spdlog::warn("⚠️ Scrubbed parse failed, trying raw fallback...");
                body = nlohmann::json::parse(req.body, nullptr, false);
            }

            if (body.is_discarded() || !body.is_object()) {
                spdlog::error("❌ JSON Error. Body length: {}", req.body.length());
                res.status = 400;
                res.set_content("{\"error\":\"Invalid JSON encoding\"}", "application/json");
                return;
            }

            spdlog::info("✅ Request JSON parsed successfully");
            spdlog::info("🚀 Calling executor->run_autonomous_loop_internal...");
            
            std::string result = executor_->run_autonomous_loop_internal(body);
            
            spdlog::info("✅ Agent execution completed. Result size: {} bytes", result.length());
            
            // 🔥 FIX: Scrub BEFORE creating JSON object
            std::string ultra_safe_result = code_assistance::scrub_json_string(result);
            
            // 🔥 FIX: Manually construct JSON string instead of using nlohmann
            std::stringstream json_response;
            json_response << "{\"suggestion\":\"";
            
            // Escape special characters for JSON manually
            for (char c : ultra_safe_result) {
                switch (c) {
                    case '"': json_response << "\\\""; break;
                    case '\\': json_response << "\\\\"; break;
                    case '\b': json_response << "\\b"; break;
                    case '\f': json_response << "\\f"; break;
                    case '\n': json_response << "\\n"; break;
                    case '\r': json_response << "\\r"; break;
                    case '\t': json_response << "\\t"; break;
                    default:
                        if (c >= 0x20 && c <= 0x7E) {
                            json_response << c;
                        } else {
                            // Skip non-printable chars
                        }
                        break;
                }
            }
            
            json_response << "\"}";
            
            res.set_content(json_response.str(), "application/json");
            
        } catch (const std::exception& e) {
            std::ofstream crash_file("DEBUG_CRASH_DUMP.txt");
            crash_file << "ERROR: " << e.what() << "\n\n";
            crash_file << "LOCATION: handle_generate_suggestion\n";
            crash_file << "REQUEST BODY LENGTH: " << req.body.length() << "\n";
            crash_file << "BODY:\n" << req.body;
            crash_file.close();
            
            spdlog::error("🔥 CRASH SAVED TO DEBUG_CRASH_DUMP.txt: {}", e.what());
            spdlog::error("🔥 REST HANDLER ERROR: {}", e.what());
            res.status = 500;
            res.set_content("{\"error\":\"Internal Server Error\"}", "application/json");
        }
    }

    void handle_retrieve_candidates(const httplib::Request& req, httplib::Response& res) {
        try {
            
            std::string safe_body = code_assistance::scrub_json_string(req.body);
            // VALID 3-ARGUMENT PARSE: (input, callback_fn, allow_exceptions)
            nlohmann::json body = nlohmann::json::parse(safe_body, nullptr, false);
            
            if (body.is_discarded()) {
                spdlog::warn("⚠️ Scrubbed parse failed, trying raw fallback...");
                body = nlohmann::json::parse(req.body, nullptr, false);
            }

            if (body.is_discarded() || !body.is_object()) {
                spdlog::error("❌ JSON Error. Body length: {}", req.body.length());
                res.status = 400;
                res.set_content("{\"error\":\"Invalid JSON encoding\"}", "application/json");
                return;
            }


            std::string project_id = body.value("project_id", "");
            std::string prompt = body.value("prompt", "");

            // 🛡️ CRITICAL FIX: Get the Graph ALREADY in memory from the executor
            // This prevents the "Connection Reset" crash caused by file-lock conflicts
            auto graph = executor_->get_or_create_graph(project_id);
            
            // Generate embedding for the query
            auto query_emb = ai_service_->generate_embedding(prompt);
            if (query_emb.empty()) {
                throw std::runtime_error("Failed to generate query embedding");
            }

            // Use the PointerGraph's semantic search directly
            // This returns nodes that are guaranteed to exist in RAM
            auto results = graph->semantic_search(query_emb, 10);
            
            json candidates = json::array();
            for (const auto& node : results) {
                json item;
                item["file_path"] = node.metadata.count("file_path") ? node.metadata.at("file_path") : "unknown";
                item["name"] = node.metadata.count("node_name") ? node.metadata.at("node_name") : "anonymous";
                item["content"] = node.content; // The code snippet
                item["type"] = node_type_to_string(node.type);
                candidates.push_back(item);
            }

            spdlog::info("🔎 RAG Audit: Found {} candidates for project {}", candidates.size(), project_id);
            res.set_content(json{{"candidates", candidates}}.dump(), "application/json");

        } catch (const std::exception& e) {
        std::ofstream crash_file("DEBUG_CRASH_DUMP.txt");
        crash_file << "ERROR: " << e.what() << "\n\n";
        crash_file << "BODY RECEIVED:\n" << req.body;
        crash_file.close();
        spdlog::error("🔥 CRASH SAVED TO DEBUG_CRASH_DUMP.txt: {}", e.what());
            spdlog::error("❌ Retrieval API Error: {}", e.what());
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    }

    void setup_routes() {
        // --- CORS HEADERS ---
        server_.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server_.Options("/(.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });

        // 1. GHOST TEXT (Inline Logic)
        server_.Post("/complete", [this](const httplib::Request& req, httplib::Response& res) {
            auto start = std::chrono::high_resolution_clock::now();
            try {
                
            std::string safe_body = code_assistance::scrub_json_string(req.body);
            // VALID 3-ARGUMENT PARSE: (input, callback_fn, allow_exceptions)
            nlohmann::json body = nlohmann::json::parse(safe_body, nullptr, false);
            
            if (body.is_discarded()) {
                spdlog::warn("⚠️ Scrubbed parse failed, trying raw fallback...");
                body = nlohmann::json::parse(req.body, nullptr, false);
            }

            if (body.is_discarded() || !body.is_object()) {
                spdlog::error("❌ JSON Error. Body length: {}", req.body.length());
                res.status = 400;
                res.set_content("{\"error\":\"Invalid JSON encoding\"}", "application/json");
                return;
            }


                std::string prefix = body.value("prefix", "");
                std::string suffix = body.value("suffix", "");
                std::string project_id = body.value("project_id", "");
                std::string current_file = body.value("file_path", "");

                if (prefix.empty()) { res.status = 400; return; }

                // 1. Fetch from RAM (No Disk I/O)
                std::string long_context = "";
                {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    if (project_context_cache_.count(project_id)) {
                        long_context = project_context_cache_[project_id];
                    }
                }

                // 2. Generate
                std::string completion = this->ai_service_->generate_autocomplete(prefix, suffix, long_context, current_file);

                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                code_assistance::SystemMonitor::global_llm_generation_ms.store(ms);
                
                // 3. LOGGING (Crucial for Dashboard)
                if (!completion.empty()) {
                    code_assistance::InteractionLog log;
                    log.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    log.project_id = project_id;
                    log.request_type = "GHOST";
                    
                    // User Query Preview
                    std::string pre_short = prefix.length() > 50 ? prefix.substr(prefix.length() - 50) : prefix;
                    std::string suf_short = suffix.length() > 50 ? suffix.substr(0, 50) : suffix;
                    log.user_query = pre_short + " [CURSOR] " + suf_short;
                    
                    // Full Context for Inspector
                    log.full_prompt = "### SYSTEM CONTEXT SIZE: " + std::to_string(long_context.size()) + " chars\n" +
                                      "### ACTIVE FILE: " + current_file + "\n\n" + 
                                      prefix + "[CURSOR]" + suffix; 
                    
                    log.ai_response = completion;
                    log.duration_ms = ms;
                    
                    // Save to Singleton
                    code_assistance::LogManager::instance().add_log(log);
                }

                res.set_content(json{{"completion", completion}}.dump(), "application/json");
            } catch (...) { res.status = 500; }
        });

        // 2. Standard Handlers
        server_.Post("/sync/register/:project_id", [this](const httplib::Request& req, httplib::Response& res) { this->handle_register_project(req, res); });
        server_.Post("/generate-code-suggestion", [this](const httplib::Request& req, httplib::Response& res) { this->handle_generate_suggestion(req, res); });
        server_.Post("/retrieve-context-candidates", [this](const httplib::Request& req, httplib::Response& res) { this->handle_retrieve_candidates(req, res); });

        // 3. SYNC RUN (Fixed Logic)
        server_.Post("/sync/run/:project_id", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto project_id = req.path_params.at("project_id");

                
            std::string safe_body = code_assistance::scrub_json_string(req.body);
            // VALID 3-ARGUMENT PARSE: (input, callback_fn, allow_exceptions)
            nlohmann::json body = nlohmann::json::parse(safe_body, nullptr, false);
            
            if (body.is_discarded()) {
                spdlog::warn("⚠️ Scrubbed parse failed, trying raw fallback...");
                body = nlohmann::json::parse(req.body, nullptr, false);
            }

            if (body.is_discarded() || !body.is_object()) {
                spdlog::error("❌ JSON Error. Body length: {}", req.body.length());
                res.status = 400;
                res.set_content("{\"error\":\"Invalid JSON encoding\"}", "application/json");
                return;
            }


                std::string store_path = body.value("storage_path", "");
                if(store_path.empty()) store_path = (fs::path("data") / project_id).string();
                
                thread_pool_.enqueue([this, project_id, store_path]() {
                    auto t_start = std::chrono::high_resolution_clock::now();

                    // A. Load Config
                    json config = load_project_config(project_id);
                    
                    // 🚀 EXTRACT LISTS (FIXED PART)
                    std::vector<std::string> ext = config.value("allowed_extensions", std::vector<std::string>{});
                    std::vector<std::string> ign = config.value("ignored_paths", std::vector<std::string>{});
                    std::vector<std::string> inc = config.value("included_paths", std::vector<std::string>{});

                    // Ensure defaults if empty (Backend Safety)
                    if (ext.empty()) ext = {"java", "json", "py", "cpp", "h", "ts", "js", "txt", "md"};
                    
                    code_assistance::SyncService sync(ai_service_);
                    
                    // Pass the extracted lists to the sync engine
                    auto sync_res = sync.perform_sync(
                        project_id, 
                        config.value("local_path", ""), 
                        store_path, 
                        ext, 
                        ign, 
                        inc
                    );
                    
                    // B. Unified Ingestion
                    if (!sync_res.nodes.empty()) {
                        executor_->ingest_sync_results(project_id, sync_res.nodes);
                    }

                    // C. Hot-Load RAM Context
                    this->refresh_context_cache(project_id, store_path);

                    auto t_end = std::chrono::high_resolution_clock::now();
                    double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
                    code_assistance::SystemMonitor::global_sync_latency_ms.store(ms);
                    spdlog::info("⏱️ Sync Complete in {:.2f} ms", ms);
                });

                res.set_content(json{{"success", true}}.dump(), "application/json");
            } catch(...) { res.status = 500; }
        });
        
        // 4. File Specific Sync
        server_.Post("/sync/file/:project_id", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                std::string project_id = req.path_params.at("project_id");

                
            std::string safe_body = code_assistance::scrub_json_string(req.body);
            // VALID 3-ARGUMENT PARSE: (input, callback_fn, allow_exceptions)
            nlohmann::json body = nlohmann::json::parse(safe_body, nullptr, false);
            
            if (body.is_discarded()) {
                spdlog::warn("⚠️ Scrubbed parse failed, trying raw fallback...");
                body = nlohmann::json::parse(req.body, nullptr, false);
            }

            if (body.is_discarded() || !body.is_object()) {
                spdlog::error("❌ JSON Error. Body length: {}", req.body.length());
                res.status = 400;
                res.set_content("{\"error\":\"Invalid JSON encoding\"}", "application/json");
                return;
            }


                std::string rel_path = body.value("file_path", "");
                
                if (rel_path.find(".study_assistant") == std::string::npos) {
                    fs::path default_store = fs::path("data") / project_id;
                    thread_pool_.enqueue([this, project_id, default_store]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Debounce
                        this->refresh_context_cache(project_id, default_store);
                    });
                }
                res.set_content(json{{"status", "queued"}}.dump(), "application/json");
            } catch (...) { res.status = 400; }
        });

        // 5. TELEMETRY (FIXED: Send ALL data needed by JS)
        server_.Get("/api/admin/telemetry", [this](const httplib::Request&, httplib::Response& res) {
            auto m = system_monitor_.get_latest_snapshot();
            auto logs = code_assistance::LogManager::instance().get_logs_json();
            auto traces = code_assistance::LogManager::instance().get_traces_json(); 
            
            json payload;
            payload["metrics"] = {
                {"cpu", m.cpu_usage},
                {"ram_mb", m.ram_usage_mb},
                {"last_sync_duration_ms", m.last_sync_duration_ms}, 
                {"cache_size_mb", m.cache_size_mb},
                {"llm_latency", m.llm_generation_ms},
                {"tps", m.tokens_per_second},
                {"vector_latency", m.vector_latency_ms}
            };
            payload["logs"] = logs;
            payload["agent_traces"] = traces;

            res.set_content(payload.dump(), "application/json");
        });

        // 6. 🚀 GRAPH DATA ENDPOINT
        server_.Get("/api/admin/graph/:project_id", [this](const httplib::Request& req, httplib::Response& res) {
            std::string project_id = req.path_params.at("project_id");
            
            // Sanitize ID (same logic as AgentExecutor)
            std::string safe_id = project_id;
            std::replace(safe_id.begin(), safe_id.end(), ':', '_');
            std::replace(safe_id.begin(), safe_id.end(), '/', '_');
            std::replace(safe_id.begin(), safe_id.end(), '\\', '_');
            
            fs::path graph_path = fs::path("data/graphs") / safe_id / "graph.json";
            
            if (fs::exists(graph_path)) {
                std::ifstream f(graph_path);
                std::stringstream buffer;
                buffer << f.rdbuf();
                res.set_content(buffer.str(), "application/json");
            } else {
                res.set_content("[]", "application/json"); // Return empty graph if new
            }
        });
        
        server_.Get("/api/hello", [](const httplib::Request&, httplib::Response& res) { res.set_content(R"({"status": "nominal"})", "application/json"); });
        server_.set_mount_point("/", "./www");
        server_.Get("/admin", [](const httplib::Request&, httplib::Response& res) { res.set_redirect("/index.html"); });
    }
};

void pre_flight_check() {
    if (!fs::exists("keys.json")) spdlog::warn("⚠️ keys.json not found!");
}

int main() {
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    pre_flight_check();
    CodeAssistanceServer app;
    
    // We can't easily extract the server object from the wrapper class without modifying it
    // But ensuring ThreadPool destructors run (via app going out of scope) is usually enough 
    // if the server.listen() loop breaks.
    
    app.run(); // This blocks
    
    return 0;
}