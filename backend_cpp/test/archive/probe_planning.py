import requests
import json
import base64
import time
from termcolor import colored

# 🚀 CONFIGURATION
API_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest" # Must match Step 1
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

# A complex prompt ensures the AI triggers "Planning Mode"
PROMPT = "Create a python script named 'payment_processor.py' that processes a list of transactions. It should handle errors gracefully."

def run_planning_test():
    print(colored(f"🧠 TESTING SKILLS & PLANNING on {TARGET_PATH}...", "cyan", attrs=['bold']))

    # 1. Register the project (Essential for Skill Loading)
    print("1️⃣  Registering Project...")
    reg_payload = {
        "local_path": TARGET_PATH,
        "allowed_extensions": ["py", "yaml"],
        "ignored_paths": [],
        "included_paths": []
    }
    requests.post(f"{API_URL}/sync/register/{PROJECT_ID}", json=reg_payload)

    # 2. Send Complex Request
    session_id = f"PLAN_TEST_{int(time.time())}"
    print(f"2️⃣  Sending Prompt: '{PROMPT}'")
    
    # --- TURN 1: Expecting a Plan Proposal ---
    payload = {
        "project_id": PROJECT_ID,
        "prompt": PROMPT,
        "session_id": session_id
    }
    
    try:
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=60)
        data = res.json()
        
        # Check if we got a suggestion (Plan Proposal usually comes as text or suggestion)
        suggestion = data.get("suggestion", "")
        
        if "proposed a plan" in suggestion.lower() or "steps" in suggestion.lower():
            print(colored("\n✅ PHASE 1 COMPLETE: Plan Proposed!", "green"))
            print(colored("-" * 40, "yellow"))
            print(suggestion)
            print(colored("-" * 40, "yellow"))
        else:
            print(colored("\n⚠️  WARNING: AI did not propose a plan. It might have tried to execute immediately.", "yellow"))
            print("Response:", suggestion)
            return

        # --- TURN 2: Approving the Plan ---
        print("\n3️⃣  Simulating User Approval...")
        time.sleep(2) # Simulate reading time
        
        approval_payload = {
            "project_id": PROJECT_ID,
            "prompt": "The plan looks good. Approved. Proceed.",
            "session_id": session_id # MUST match to keep context
        }
        
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=approval_payload, timeout=120)
        final_data = res.json()
        
        print(colored("\n✅ PHASE 2 COMPLETE: Execution Finished!", "green"))
        print(colored("=" * 60, "magenta"))
        print(final_data.get("suggestion", ""))
        print(colored("=" * 60, "magenta"))

    except Exception as e:
        print(colored(f"❌ Error: {e}", "red"))

if __name__ == "__main__":
    run_planning_test()