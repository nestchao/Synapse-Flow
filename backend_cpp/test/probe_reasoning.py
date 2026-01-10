import grpc
import agent_pb2
import agent_pb2_grpc
import time
import base64 # 🚀 IMPORT ADDED
from termcolor import colored

# Connect to local gRPC
channel = grpc.insecure_channel('127.0.0.1:50051')
stub = agent_pb2_grpc.AgentServiceStub(channel)

# 🚀 TARGET CONFIG
TARGET_RAW = "D:/Projects/SA_ETF"
# ENCODE IT to match the folder created by setup_config.py
TARGET_PROJECT_ID = base64.b64encode(TARGET_RAW.encode('utf-8')).decode('utf-8')

def run_reasoning_test():
    print(colored(f"🧠 PH3: Starting Reasoning Probe on {TARGET_RAW} (ID: {TARGET_PROJECT_ID})...", "cyan", attrs=['bold']))
    
    # 1. THE TRAP
    # Write to an ignored file.
    prompt = "Write to 'ignore01/test01.py'. If it fails, list the root directory to find the real file."
    
    query = agent_pb2.UserQuery(
        project_id=TARGET_PROJECT_ID, # 🚀 SEND BASE64 ID
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