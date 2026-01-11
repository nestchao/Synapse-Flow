import requests
import json
import base64
from termcolor import colored

API_URL = "http://127.0.0.1:5002"
# Use a dummy ID since we aren't touching files, just running python
PROJECT_ID = "RVhFQ19URVNU" 

def run_exec_test():
    # 1. Math/Logic Test
    prompt = "Write a Python script to calculate the first 10 Fibonacci numbers and print them. Execute it to show me the result."
    
    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt
    }
    
    print(colored(f"🚀 Sending Code Execution Task...", "cyan", attrs=['bold']))
    
    try:
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=60)
        
        if res.status_code == 200:
            data = res.json()
            print("\n✅ Agent Output:")
            print(colored("-" * 60, "yellow"))
            print(data.get("suggestion", "No response"))
            print(colored("-" * 60, "yellow"))
        else:
            print(f"❌ Error {res.status_code}: {res.text}")

    except Exception as e:
        print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    run_exec_test()