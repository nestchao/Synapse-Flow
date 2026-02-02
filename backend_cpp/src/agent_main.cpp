#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <string>

// Generated Proto Headers
#include "agent.pb.h"
#include "agent.grpc.pb.h"

// Core Engine Headers
#include "agent/AgentExecutor.hpp"
#include "tools/ToolRegistry.hpp"
#include "KeyManager.hpp"
#include "embedding_service.hpp"
#include "tools/FileSystemTools.hpp"
#include "tools/FileSurgicalTool.hpp"
#include "tools/WebSearchTool.hpp"

namespace code_assistance {
    // Forward declare if needed, or include the header if you made one
    std::string web_search(const std::string& args_json, const std::string& api_key);
}

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;

// 🚀 SERVICE IMPLEMENTATION
class AgentServiceImpl final : public code_assistance::AgentService::Service {
    // ⬇️ THIS WAS MISSING IN YOUR ERROR LOG
    std::shared_ptr<code_assistance::AgentExecutor> executor_; 

public:
    explicit AgentServiceImpl(std::shared_ptr<code_assistance::AgentExecutor> executor) 
        : executor_(executor) {}

    Status ExecuteTask(ServerContext* context, 
                      const code_assistance::UserQuery* request, 
                      ServerWriter<code_assistance::AgentResponse>* writer) override {
        
        spdlog::info("🛰️ Mission Received: [{}] {}", request->session_id(), request->prompt());

        // 1. Acknowledge
        code_assistance::AgentResponse init_res;
        init_res.set_phase("STARTUP");
        init_res.set_payload("Ignition sequence started...");
        writer->Write(init_res);

        try {
            // 2. Execute Autonomous Loop
            // This calls the method we fixed in AgentExecutor.cpp
            std::string final_answer = executor_->run_autonomous_loop(*request, writer);

            // 3. Final Payload
            code_assistance::AgentResponse final_res;
            final_res.set_phase("FINAL");
            final_res.set_payload(final_answer);
            writer->Write(final_res);

            spdlog::info("✅ Mission Complete");

        } catch (const std::exception& e) {
            spdlog::error("💥 Mission Crash: {}", e.what());
            code_assistance::AgentResponse err;
            err.set_phase("ERROR");
            err.set_payload(std::string("Internal Engine Failure: ") + e.what());
            writer->Write(err);
            return Status::CANCELLED;
        }

        return Status::OK;
    }
};

int main() {
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    std::string server_address("0.0.0.0:50051");

    spdlog::info("🔧 Initializing Avionics...");

    // 1. Initialize Core Subsystems
    auto key_manager = std::make_shared<code_assistance::KeyManager>();
    auto ai_service = std::make_shared<code_assistance::EmbeddingService>(key_manager);
    auto sub_agent = std::make_shared<code_assistance::SubAgent>();
    auto tools = std::make_shared<code_assistance::ToolRegistry>();

    auto memory_vault = std::make_shared<code_assistance::MemoryVault>("data/memory_vault");

    // 2. Wire Tools
    tools->register_tool(std::make_unique<code_assistance::ReadFileTool>());
    tools->register_tool(std::make_unique<code_assistance::ListDirTool>());
    tools->register_tool(std::make_unique<code_assistance::FileSurgicalTool>());
    tools->register_tool(std::make_unique<code_assistance::GenericTool>(
        "FINAL_ANSWER",
        "Mission Completion Signal",
        "{}",
        [](const std::string&) { return "Mission Completed. Terminating loop."; }
    ));
    
    // Wire Web Search (Lambda to inject key)
    // tools->register_tool(std::make_unique<code_assistance::GenericTool>(
    //     "web_search",
    //     "Search Google/Serper. Input: {'query': 'string'}",
    //     "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}}}",
    //     [key_manager](const std::string& args) { 
    //         // Ensure this function exists in WebSearchTool.cpp or remove this block if not ready
    //         return code_assistance::web_search(args, key_manager->get_serper_key()); 
    //     }
    // ));

    // 3. Initialize Executor
    auto executor = std::make_shared<code_assistance::AgentExecutor>(
        nullptr, 
        ai_service,
        sub_agent,
        tools,
        memory_vault 
    );

    // 4. Ignite Server
    AgentServiceImpl service(executor);
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    spdlog::info("🚀 Agent gRPC Service ignited on {}", server_address);
    
    server->Wait();
    return 0;
}