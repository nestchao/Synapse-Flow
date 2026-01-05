#include <httplib.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <filesystem>
#include <thread>
#include <chrono>
#include <mutex>
#include <unordered_map>

#include "KeyManager.hpp"
#include "LogManager.hpp"
#include "ThreadPool.hpp"
#include "embedding_service.hpp"
#include "sync_service.hpp"
#include "SystemMonitor.hpp"
#include "agent/AgentExecutor.hpp"
#include "agent/SubAgent.hpp"
#include "tools/ToolRegistry.hpp"
#include "tools/FileSurgicalTool.hpp"
#include "tools/FileSystemTools.hpp"
#include "faiss_vector_store.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace code_assistance {
    std::string web_search(const std::string& args_json, const std::string& api_key);
}

class CodeAssistanceServer {
public:
    CodeAssistanceServer(int port = 5002) : port_(port), thread_pool_(4) {
        key_manager_ = std::make_shared<code_assistance::KeyManager>();
        ai_service_ = std::make_shared<code_assistance::EmbeddingService>(key_manager_);
        sub_agent_ = std::make_shared<code_assistance::SubAgent>();
        tool_registry_ = std::make_shared<code_assistance::ToolRegistry>();
        
        tool_registry_->register_tool(std::make_unique<code_assistance::ReadFileTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::ListDirTool>());
        tool_registry_->register_tool(std::make_unique<code_assistance::FileSurgicalTool>());
        
        executor_ = std::make_shared<code_assistance::AgentExecutor>(
            nullptr, ai_service_, sub_agent_, tool_registry_
        );

        setup_routes();
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

        fs::path vector_path = fs::path("data") / project_id / "vector_store";
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
                ss << f.rdbuf(); 
                
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

    void handle_register_project(const httplib::Request& req, httplib::Response& res) {
        try {
            auto project_id = req.path_params.at("project_id");
            auto body = json::parse(req.body);
            fs::path path = fs::path("data") / project_id / "config.json";
            fs::create_directories(path.parent_path());
            std::ofstream f(path); f << body.dump(2);
            res.set_content(json{{"success", true}}.dump(), "application/json");
        } catch(...) { res.status = 500; }
    }

    void handle_sync_project(const httplib::Request& req, httplib::Response& res) {
        try {
            auto project_id = req.path_params.at("project_id");
            json config = load_project_config(project_id);
            std::string store_path = config.value("storage_path", "");
            if(store_path.empty()) store_path = (fs::path("data") / project_id).string();
            
            thread_pool_.enqueue([this, project_id, config, store_path]() {
                code_assistance::SyncService sync(ai_service_);
                // Pass empty vectors for filters for now, or load from config
                auto res = sync.perform_sync(project_id, config.value("local_path",""), store_path, {}, {}, {});
                if (!res.nodes.empty()) {
                    auto store = std::make_shared<code_assistance::FaissVectorStore>(768);
                    store->add_nodes(res.nodes);
                    fs::path p = fs::path(store_path) / "vector_store";
                    fs::create_directories(p);
                    store->save(p.string());
                    std::lock_guard<std::mutex> lock(store_mutex);
                    project_stores_[project_id] = store;
                }
            });
            res.set_content(json{{"success", true}}.dump(), "application/json");
        } catch(...) { res.status = 500; }
    }

    void handle_generate_suggestion(const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string result = executor_->run_autonomous_loop_internal(body);
            res.set_content(json{{"suggestion", result}}.dump(), "application/json");
        } catch(...) { res.status = 500; }
    }

    void handle_retrieve_candidates(const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string project_id = body["project_id"];
            std::string prompt = body["prompt"];
            auto store = load_vector_store(project_id);
            if (!store) { res.status = 404; return; }
            
            auto query_emb = ai_service_->generate_embedding(prompt);
            code_assistance::RetrievalEngine engine(store);
            auto results = engine.retrieve(prompt, query_emb, 10, true);
            
            json candidates = json::array();
            for (const auto& r : results) {
                candidates.push_back({{"file_path", r.node->file_path}, {"content", r.node->content}});
            }
            res.set_content(json{{"candidates", candidates}}.dump(), "application/json");
        } catch(...) { res.status = 500; }
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
                auto body = json::parse(req.body);
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
                json body = json::parse(req.body);
                std::string store_path = body.value("storage_path", "");
                if(store_path.empty()) store_path = (fs::path("data") / project_id).string();
                
                // Async Task (Flattened - No Nested Enqueue)
                thread_pool_.enqueue([this, project_id, store_path]() {
                    auto t_start = std::chrono::high_resolution_clock::now();

                    // A. Run Sync
                    json config = load_project_config(project_id);
                    code_assistance::SyncService sync(ai_service_);
                    auto sync_res = sync.perform_sync(project_id, config.value("local_path",""), store_path, {}, {}, {});
                    
                    // B. Vector Store Update (If needed)
                    if (!sync_res.nodes.empty()) {
                        std::lock_guard<std::mutex> lock(store_mutex);
                        
                        std::shared_ptr<code_assistance::FaissVectorStore> store;
                        if (project_stores_.count(project_id)) {
                            store = project_stores_[project_id];
                        } else {
                            store = std::make_shared<code_assistance::FaissVectorStore>(768);
                            fs::path p = fs::path(store_path) / "vector_store";
                            if (fs::exists(p / "faiss.index")) try { store->load(p.string()); } catch(...) {}
                        }

                        store->add_nodes(sync_res.nodes);
                        
                        fs::path p = fs::path(store_path) / "vector_store";
                        fs::create_directories(p);
                        store->save(p.string());

                        project_stores_[project_id] = store;
                        spdlog::info("💾 Vector Store Updated. Total Nodes: {}", store->get_all_nodes().size());
                    }

                    // C. Hot-Load RAM Context
                    this->refresh_context_cache(project_id, store_path);

                    // D. Update Metrics
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
                auto body = json::parse(req.body);
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
            
            json payload;
            // Map C++ struct fields to JS expected keys
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

            res.set_content(payload.dump(), "application/json");
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
    pre_flight_check();
    CodeAssistanceServer server;
    server.run();
    return 0;
}