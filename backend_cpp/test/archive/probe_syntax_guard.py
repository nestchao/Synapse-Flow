import requests
import json
import os
import time

API_URL = "http://127.0.0.1:5002"
# Use absolute path to ensure C++ server finds it
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ID = os.path.join(SCRIPT_DIR, "data", "surgical_test_01")

def test_guard():
    print(f"🛡️ Testing Semantic Guardrails on: {PROJECT_ID}")

    # 1. Create a C++ file (Valid initial state)
    target_file = "logic_core.cpp"
    full_path = os.path.join(PROJECT_ID, target_file)
    
    # Ensure directory exists
    if not os.path.exists(PROJECT_ID):
        os.makedirs(PROJECT_ID)

    with open(full_path, "w") as f:
        f.write("int main() { return 0; }")

    # 2. Attempt to inject BROKEN code
    # Missing closing brace '}' and semicolon ';'
    broken_code = "int main() { return 1" 
    
    payload = {
        "project_id": PROJECT_ID,
        # We explicitly ask it to use apply_edit
        "prompt": f"Overwrite '{target_file}' with exactly this content: '{broken_code}'. Use the apply_edit tool."
    }

    print("🚀 Sending Malicious Payload (Timeout set to 60s)...")
    try:
        start = time.time()
        # Increased timeout to handle Agent Retry Loop
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=60)
        
        print(f"⏱️ Response time: {time.time() - start:.2f}s")
        reply = res.json().get("suggestion", "")
        
        print(f"\n📩 Agent Reply:\n{reply}")

        # 3. Check if file was protected
        with open(full_path, "r") as f:
            content = f.read()

        print(f"\n🔍 Disk Content: '{content}'")

        if content == "int main() { return 0; }":
            print("✅ PASS: File was NOT modified. Syntax Guard active.")
        else:
            print(f"❌ FAIL: File was modified to broken code.")

    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    test_guard()