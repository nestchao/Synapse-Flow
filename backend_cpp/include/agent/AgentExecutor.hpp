#pragma once
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "agent.pb.h"
#include "agent.grpc.pb.h"
#include "agent/AgentTypes.hpp"
#include "embedding_service.hpp"
#include "retrieval_engine.hpp"
#include "agent/SubAgent.hpp"
#include "tools/ToolRegistry.hpp"
#include "agent/ContextManager.hpp"
#include "memory/PointerGraph.hpp"

namespace code_assistance {

class AgentExecutor {
public:
    AgentExecutor(
        std::shared_ptr<RetrievalEngine> engine,
        std::shared_ptr<EmbeddingService> ai,
        std::shared_ptr<SubAgent> sub_agent,
        std::shared_ptr<ToolRegistry> tool_registry
    );

    static std::string find_project_root();
    std::string run_autonomous_loop(const ::code_assistance::UserQuery& req, ::grpc::ServerWriter<::code_assistance::AgentResponse>* writer);
    std::string run_autonomous_loop_internal(const nlohmann::json& body);
    void determineContextStrategy(const std::string& query, ContextSnapshot& ctx, const std::string& project_id);
    std::shared_ptr<PointerGraph> get_or_create_graph(const std::string& project_id);
    void ingest_sync_results(const std::string& project_id, const std::vector<std::shared_ptr<CodeNode>>& nodes);

private:
    std::shared_ptr<RetrievalEngine> engine_;
    std::shared_ptr<EmbeddingService> ai_service_;
    std::shared_ptr<SubAgent> sub_agent_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::unique_ptr<ContextManager> context_mgr_;
    std::unordered_map<std::string, std::shared_ptr<PointerGraph>> graphs_;
    std::mutex graph_mutex_;

    void notify(::grpc::ServerWriter<::code_assistance::AgentResponse>* w, const std::string& phase, const std::string& msg, double duration_ms = 0.0);
    bool check_reflection(const std::string& query, const std::string& topo, std::string& reason);
    std::string construct_reasoning_prompt(
        const std::string& task, 
        const std::string& history, 
        const std::string& last_error
    );
};

}