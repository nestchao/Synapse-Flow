import requests
import json
import time
import os

# CONFIGURATION
API_URL = "http://127.0.0.1:5002"

# 1. SETUP ABSOLUTE PATHS
# Get the directory where this script lives
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# Create a data folder next to the script
TARGET_DIR = os.path.join(SCRIPT_DIR, "data", "surgical_test_01")

if not os.path.exists(TARGET_DIR):
    os.makedirs(TARGET_DIR)

# 2. KEY FIX: Project ID must be the ABSOLUTE PATH to the root
PROJECT_ID = TARGET_DIR 

def run_probe():
    print(f"📡 Connecting to Neural Engine at {API_URL}...")
    print(f"📂 Target Workspace: {PROJECT_ID}")

    # 3. Create a dummy file first so we can edit it
    target_file = os.path.join(TARGET_DIR, "live_test.txt")
    with open(target_file, "w") as f:
        f.write("OLD_CONTENT_V1")
    print(f"✅ Created dummy file: {target_file}")

    # 4. Send Instruction to Agent
    # We explicitly tell it to use the 'apply_edit' tool logic
    prompt = f"Overwrite 'live_test.txt' with the text 'SURGERY_SUCCESSFUL_V2'. Use the apply_edit tool."
    
    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt
    }

    print("\n🚀 Sending Command to Agent...")
    try:
        start = time.time()
        # 5. INCREASE TIMEOUT to 60s (Agent might be thinking/retrying)
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=60)
        
        print(f"⏱️ Response time: {time.time() - start:.2f}s")
        
        if res.status_code != 200:
            print(f"❌ Server Error {res.status_code}: {res.text}")
            return

        print(f"📩 Agent Reply: {json.dumps(res.json(), indent=2)}")

        # 6. Verify the file changed on disk
        with open(target_file, "r") as f:
            new_content = f.read()
        
        print("\n🔍 DISK VERIFICATION:")
        if new_content == "SURGERY_SUCCESSFUL_V2":
            print("✅ SUCCESS: File was updated by C++ Engine.")
        else:
            print(f"❌ FAIL: Content is '{new_content}'")

    except requests.exceptions.ReadTimeout:
        print("❌ TIMEOUT: Agent took too long to reply (Backend might be retrying 429s).")
    except Exception as e:
        print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    run_probe()