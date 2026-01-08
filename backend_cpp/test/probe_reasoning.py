import grpc
import agent_pb2
import agent_pb2_grpc
import time
from termcolor import colored

# Connect to local gRPC
channel = grpc.insecure_channel('127.0.0.1:50051')
stub = agent_pb2_grpc.AgentServiceStub(channel)

# 🚀 TARGET CONFIG
TARGET_PROJECT = "D:/Projects/SA_ETF"

def run_reasoning_test():
    print(colored(f"🧠 PH3: Starting Reasoning Probe on {TARGET_PROJECT}...", "cyan", attrs=['bold']))
    
    # 1. THE TRAP
    # Ask for a file that doesn't exist to force error handling + directory listing
    prompt = "Read 'config/missing_settings.json'. If it fails, list the root directory to find the real file."
    
    query = agent_pb2.UserQuery(
        project_id=TARGET_PROJECT, 
        prompt=prompt,
        session_id=f"PHOENIX_{int(time.time())}"
    )

    try:
        # Increased timeout for debugging
        responses = stub.ExecuteTask(query, timeout=120)
        
        print("\n📡 MONITORING AGENT STREAM:")
        print("-" * 60)
        
        for res in responses:
            if res.phase == "ERROR_CATCH":
                print(colored(f"   🛡️  [SELF-CORRECTION] {res.payload}", "green"))
            elif res.phase == "TOOL_EXEC":
                print(colored(f"   🛠️  [TOOL] {res.payload}", "yellow"))
            elif res.phase == "THINKING":
                print(colored(f"   💭 [AI] {res.payload}", "blue"))
            elif res.phase == "FATAL":
                print(colored(f"   ❌ [FATAL] {res.payload}", "red", attrs=['bold']))
            elif res.phase == "FINAL":
                print(colored(f"   🏁 [FINAL] {res.payload}", "white", attrs=['bold']))
            else:
                print(f"   [{res.phase}] {res.payload}")
                
        print("-" * 60)

    except Exception as e:
        print(colored(f"❌ Connection Failed: {e}", "red"))

if __name__ == "__main__":
    run_reasoning_test()