import requests
import json
import os
import time

# CONFIGURATION
API_URL = "http://127.0.0.1:5002"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ID = os.path.join(SCRIPT_DIR, "data", "surgical_test_01")

def run_cognitive_test():
    print(f"🧠 Testing Cognitive Self-Correction on: {PROJECT_ID}")

    # 1. Reset File
    target_file = "logic_core.cpp"
    full_path = os.path.join(PROJECT_ID, target_file)
    if not os.path.exists(PROJECT_ID): os.makedirs(PROJECT_ID)
    
    with open(full_path, "w") as f:
        f.write("int main() { return 0; }")

    # 2. The Prompt: Ask it to write broken code, but hint that it should work.
    # The 'broken_code' is missing a semicolon.
    broken_code = "int main() { return 1" 
    
    # We add "If there are errors, fix them" to allow the agent to deviate from strict instruction
    prompt = (
        f"Update '{target_file}' to return 1 instead of 0. "
        f"Here is the code draft: '{broken_code}'. "
        f"Use the apply_edit tool. Ensure the code is valid C++."
    )
    
    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt
    }

    print("\n🚀 Sending Task (Timeout: 120s)...")
    try:
        start = time.time()
        # Increased timeout to 120s to handle 503 retries + reasoning steps
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=120)
        
        duration = time.time() - start
        print(f"⏱️ Response time: {duration:.2f}s")
        
        reply = res.json().get("suggestion", "NO_RESPONSE")
        print(f"📩 Agent Reply: {reply}")

        # 3. Verify Disk Content
        with open(full_path, "r") as f:
            content = f.read().strip()

        print(f"\n🔍 Final Disk Content: '{content}'")

        # 4. Success Criteria
        # It should have fixed the missing '}' and ';'
        if "return 1;" in content and "}" in content:
            print("🏆 PASS: Agent self-corrected the syntax error!")
        elif content == "int main() { return 0; }":
            print("⚠️ STALL: Agent gave up and didn't change the file.")
        else:
            print("❌ FAIL: Agent wrote broken code or unexpected content.")

    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    run_cognitive_test()