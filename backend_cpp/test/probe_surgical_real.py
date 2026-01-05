import requests
import json
import time
import os

# CONFIGURATION
API_URL = "http://127.0.0.1:5002"
PROJECT_ID = "surgical_test_01" 
# Use absolute path to where you want the test file created
# Ensure this folder exists!
TARGET_DIR = os.path.abspath("data/surgical_test_01") 

if not os.path.exists(TARGET_DIR):
    os.makedirs(TARGET_DIR)

def run_probe():
    print(f"📡 Connecting to Neural Engine at {API_URL}...")

    # 1. Create a dummy file first so we can edit it
    target_file = os.path.join(TARGET_DIR, "live_test.txt")
    with open(target_file, "w") as f:
        f.write("OLD_CONTENT_V1")
    print(f"✅ Created dummy file: {target_file}")

    # 2. Send Instruction to Agent
    # We explicitly tell it to use the 'apply_edit' tool logic
    prompt = f"Overwrite the file 'live_test.txt' with the content 'SURGERY_SUCCESSFUL_V2'. Use the apply_edit tool."
    
    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt
    }

    print("\n🚀 Sending Command to Agent...")
    try:
        # Note: We use the generate-code-suggestion endpoint which runs the Agent Loop
        start = time.time()
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=10)
        
        print(f"⏱️ Response time: {time.time() - start:.2f}s")
        print(f"📩 Agent Reply: {json.dumps(res.json(), indent=2)}")

        # 3. Verify the file changed on disk
        with open(target_file, "r") as f:
            new_content = f.read()
        
        print("\n🔍 DISK VERIFICATION:")
        if new_content == "SURGERY_SUCCESSFUL_V2":
            print("✅ SUCCESS: File was updated by C++ Engine.")
        else:
            print(f"❌ FAIL: Content is '{new_content}'")

    except Exception as e:
        print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    run_probe()