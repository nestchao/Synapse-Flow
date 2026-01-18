#include "proto/agent.grpc.pb.h" 
#include "agent/AgentExecutor.hpp"

namespace code_assistance {

class AgentServiceImpl final : public AgentService::Service {
    
    std::shared_ptr<AgentExecutor> executor;

public:
    // Constructor injection
    explicit AgentServiceImpl(std::shared_ptr<AgentExecutor> exec) : executor(exec) {}

    grpc::Status ExecuteTask(
        grpc::ServerContext* context, 
        const UserQuery* request, 
        grpc::ServerWriter<AgentResponse>* writer
    ) override {
        
        AgentResponse res;
        res.set_phase("STARTUP");
        res.set_payload("Agent Service Connected.");
        writer->Write(res);

        executor->run_autonomous_loop(*request, writer);

        return grpc::Status::OK;
    }
};

} // namespace code_assistance