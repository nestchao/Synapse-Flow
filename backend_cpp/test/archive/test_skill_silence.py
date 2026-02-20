import requests
import json
import base64
import time
from termcolor import colored

API_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest" 
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def run():
    print(colored(f"🧪 TEST 1: Ensuring NO skills load for irrelevant topics...", "cyan"))
    
    # 1. Register
    requests.post(f"{API_URL}/sync/register/{PROJECT_ID}", json={
        "local_path": TARGET_PATH, "allowed_extensions": ["py"], "ignored_paths": [], "included_paths": []
    })

    # 2. Ask irrelevant question
    prompt = "What is the capital of France? Just answer the name."
    
    print(f"📤 Sending: '{prompt}'")
    
    # We use a unique session ID so we can track it easily in logs if needed
    res = requests.post(f"{API_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": "TEST_SILENCE"
    })
    
    print("📥 Response:", res.json().get("suggestion"))
    print(colored("\n🔎 CHECK SERVER LOGS NOW.", "yellow"))
    print("You should see: ❌ [SKILL REJECT] ... (Too irrelevant)")
    print("You should NOT see: ✅ [SKILL MATCH]")

if __name__ == "__main__":
    run()