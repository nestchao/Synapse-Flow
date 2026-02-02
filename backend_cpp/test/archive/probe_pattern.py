import requests
import json
import base64
from termcolor import colored

API_URL = "http://127.0.0.1:5002"
TARGET_RAW = "D:/Projects/OOPAssignment"
PROJECT_ID = base64.b64encode(TARGET_RAW.encode('utf-8')).decode('utf-8')

def run_pattern_test():
    # 🚀 THE MISSION: Find 'reflect' library usage
    # We hint at 'pattern matching' so the AI prefers pattern_search over reading 100 java files.
    prompt = "Find where the 'reflect' library (e.g., 'import java.lang.reflect') is used in this project. Return the file path and line number using pattern matching."
    
    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt
    }
    
    print(colored(f"🚀 Sending Pattern Search Task to {TARGET_RAW}...", "cyan", attrs=['bold']))
    
    try:
        # 60s timeout for multi-step reasoning
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=60000)
        
        if res.status_code == 200:
            data = res.json()
            print("\n✅ Mission Success:")
            print(colored("-" * 60, "yellow"))
            print(data.get("suggestion", "No response"))
            print(colored("-" * 60, "yellow"))
        else:
            print(f"❌ Error {res.status_code}: {res.text}")

    except Exception as e:
        print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    run_pattern_test()