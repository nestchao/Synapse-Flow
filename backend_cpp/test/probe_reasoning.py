import requests
import json
import base64 
import time
from termcolor import colored

# 🚀 CONFIGURATION
API_URL = "http://127.0.0.1:5002" # REST Port (Connected to Dashboard)
TARGET_RAW = "D:/Projects/SA_ETF"
TARGET_PROJECT_ID = base64.b64encode(TARGET_RAW.encode('utf-8')).decode('utf-8')

def run_rest_reasoning():
    print(colored(f"🧠 Starting REST Reasoning Probe on {TARGET_RAW}...", "cyan", attrs=['bold']))
    
    # 🚀 SCENARIO:
    # 1. Try to write to a forbidden zone.
    # 2. Fallback to valid file.
    prompt = (
        "Try to write the text 'SECRET' to 'ignore01/secret_rest.txt'. "
        "If that operation fails or is blocked, create a file named 'recovery_log_rest.txt' "
        "in the root directory with the content 'REST API Verified'."
    )
    
    # Session ID allows us to filter in the dashboard
    session_id = f"REST_PROBE_{int(time.time())}"

    payload = {
        "project_id": TARGET_PROJECT_ID,
        "prompt": prompt,
        "session_id": session_id
    }

    try:
        print("\n🚀 Sending Request to REST Brain...")
        start = time.time()
        
        # Long timeout for reasoning loop
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=120)
        
        duration = time.time() - start
        
        if res.status_code == 200:
            data = res.json()
            suggestion = data.get("suggestion", "")
            
            print(f"\n✅ Mission Success ({duration:.2f}s)")
            print(colored("-" * 60, "yellow"))
            print(suggestion)
            print(colored("-" * 60, "yellow"))
            print(f"\n👀 NOW CHECK DASHBOARD: http://localhost:5002/admin")
            print(f"   Look for Session ID: {session_id}")
        else:
            print(f"❌ Error {res.status_code}: {res.text}")

    except Exception as e:
        print(colored(f"❌ Connection Failed: {e}", "red"))

if __name__ == "__main__":
    run_rest_reasoning()