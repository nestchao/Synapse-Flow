import requests
import json
import os
import time
import sys

# 🛰️ CONFIGURATION
TARGET_PATH = "D:/Projects/SA_ETF" 
PROJECT_ID = "flight_test_001"
API_URL = "http://127.0.0.1:5002"

def run_targeted_test():
    print("--- SCRIPT INITIALIZED ---")
    
    # 1. REGISTER
    reg_payload = {
        "local_path": TARGET_PATH,
        "allowed_extensions": ["txt", "html", "ts", "json", "py"],
        "ignored_paths": ["ignore01", "test02"],
        "included_paths": ["test02/exception01"],
        "storage_path": f"{TARGET_PATH}/.study_assistant"
    }
    
    print(f"📡 Phase 1: Registering {PROJECT_ID}...")
    try:
        res = requests.post(f"{API_URL}/sync/register/{PROJECT_ID}", json=reg_payload, timeout=5)
        print(f"   Status: {res.status_code} | Data: {res.json()}")
    except Exception as e:
        print(f"❌ Abort: Could not connect to REST server at {API_URL}. Error: {e}")
        return

    # 2. SYNC
    print(f"📡 Phase 2: Triggering Sync...")
    requests.post(f"{API_URL}/sync/run/{PROJECT_ID}", json={"storage_path": reg_payload["storage_path"]})
    print("   Sync dispatched. Waiting 3s for HNSW warmup...")
    time.sleep(3)

    # 3. CHAT
    print(f"📡 Phase 3: Querying Agent...")
    query = {
        "project_id": PROJECT_ID,
        "prompt": "Use list_dir to see the root, then explain one file."
    }
    
    try:
        start = time.time()
        chat_res = requests.post(f"{API_URL}/generate-code-suggestion", json=query, timeout=600)
        print(f"✅ Received Response in {time.time()-start:.2f}s:")
        print("-" * 60)
        print(chat_res.json().get("suggestion", "EMPTY RESPONSE"))
        print("-" * 60)
    except Exception as e:
        print(f"❌ Agent Timeout/Error: {e}")

if __name__ == "__main__":
    print("🚀 PROBE ACTIVATED")
    run_targeted_test()
    print("🏁 PROBE DEACTIVATED")