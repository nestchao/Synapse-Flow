import requests
import json
import time
from termcolor import colored

# 🚀 CONFIGURATION
API_URL = "http://127.0.0.1:5002"
PROJECT_ID = "D:/Projects/SA_ETF"

def run_rest_probe():
    print(colored("🛰️  PHASE 5: Connecting to REST API (VS Code Simulator)...", "cyan", attrs=['bold']))
    
    # 1. Heartbeat Check
    try:
        r = requests.get(f"{API_URL}/api/hello", timeout=2)
        if r.status_code == 200:
            print("✅ Server Heartbeat: NOMINAL")
        else:
            print(f"❌ Server Heartbeat: FAILED ({r.status_code})")
            return
    except:
        print("❌ CRITICAL: Server offline. Run 'code_assistance_server.exe'")
        return

    # 2. Define the Mission (Complex Task to force Planning)
    # We ask for a missing file to force the agent to PLAN -> FAIL -> LIST -> FIND -> READ.
    prompt = "Read 'config/missing_settings.json'. If it fails, search the root directory for the real config file and read that."
    
    print(f"\n🚀 Sending Task via HTTP: \"{prompt}\"")
    
    start = time.time()
    try:
        # VS Code sends this exact payload structure
        payload = {
            "project_id": PROJECT_ID,
            "prompt": prompt,
            "active_file_path": "test01.py", 
            "active_file_content": "print('hello world')" # Context injection
        }
        
        # This request waits for the C++ Agent to finish thinking
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=120)
        
        duration = time.time() - start
        
        if res.status_code == 200:
            data = res.json()
            suggestion = data.get("suggestion", "")
            
            print(f"\n✅ Mission Success ({duration:.2f}s)")
            print(colored("-" * 60, "yellow"))
            print(suggestion)
            print(colored("-" * 60, "yellow"))
            print("\n👀 CHECK DASHBOARD NOW: http://localhost:5002/admin")
        else:
            print(f"❌ HTTP Error {res.status_code}: {res.text}")

    except Exception as e:
        print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    run_rest_probe()