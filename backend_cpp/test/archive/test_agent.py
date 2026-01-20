import grpc
import agent_pb2
import agent_pb2_grpc
import time

def run_flight_test():
    # Use explicit IPv4
    channel = grpc.insecure_channel('127.0.0.1:50051')
    stub = agent_pb2_grpc.AgentServiceStub(channel)

    query = agent_pb2.UserQuery(
        project_id="test_launch",
        prompt="Check if our EmbeddingService uses a singleton pattern. If not, find the Google recommended C++ singleton pattern for high-concurrency and suggest a refactor.",
        session_id="emergence_test_001"
    )

    print("🚀 PROBE: Launching Task Request...")
    start_time = time.time()
    
    try:
        # Use a timeout to prevent infinite hanging
        responses = stub.ExecuteTask(query, timeout=1200)
        
        count = 0
        for res in responses:
            count += 1
            print(f"[{res.phase.upper()}] {res.payload}")
        
        if count == 0:
            print("⚠️ WARNING: Connection succeeded but server sent 0 messages.")
            
    except grpc.RpcError as e:
        print(f"❌ gRPC ERROR: {e.code()} - {e.details()}")
    
    print(f"🏁 PROBE: Mission ended in {time.time() - start_time:.2f}s")

if __name__ == '__main__':
    run_flight_test()