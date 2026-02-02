import grpc
import agent_pb2
import agent_pb2_grpc
import time
from termcolor import colored

def run_ast_test():
    print(colored("🛰️  UPLINK ESTABLISHED. Listening for Engine Heartbeat...", "cyan"))
    
    channel = grpc.insecure_channel('127.0.0.1:50051')
    stub = agent_pb2_grpc.AgentServiceStub(channel)

    WORKSPACE_TARGET = "D:/Projects/SA_ETF" # 🚀 FULL PATH

    # In the query construction:
    query = agent_pb2.UserQuery(
        project_id=WORKSPACE_TARGET,
        prompt="Recursively list the directory. Look for 'calibration_target.cpp' inside any 'exception' folder and report its symbols.",
        session_id=f"DIAG_{int(time.time())}"
    )

    try:
        # Use a longer timeout for the thinking process
        responses = stub.ExecuteTask(query, timeout=300) 
        
        print(colored("📡 RECEIVING STREAM:", "yellow"))
        print("-" * 50)
        
        for res in responses:
            # 🚀 Print EVERY message from the C++ Engine
            color = "white"
            if res.phase == "AST_SCAN": color = "blue"
            if res.phase == "TOOL_EXEC": color = "magenta"
            if res.phase == "FINAL": color = "green"
            if res.phase == "THOUGHT": color = "cyan"
            
            header = f"[{res.phase}]"
            print(f"{colored(header, color):<12} | {res.payload}")

        print("-" * 50)
        print(colored("🏁 STREAM CLOSED.", "yellow"))

    except grpc.RpcError as e:
        print(colored(f"❌ gRPC CONNECTION LOST: {e.code()} - {e.details()}", "red"))
    except Exception as e:
        print(colored(f"❌ PROBE CRASHED: {e}", "red"))

if __name__ == "__main__":
    run_ast_test()