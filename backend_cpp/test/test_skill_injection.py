import requests
import json
import base64
import time
import os
from termcolor import colored

API_URL = "http://127.0.0.1:5002"
TARGET_PATH = r"D:\Projects\SkillTest" 
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def run():
    print(colored(f"🧪 TEST: Full Cycle (Plan -> Approve -> Execute)...", "cyan", attrs=['bold']))
    
    session_id = f"TEST_{int(time.time())}"
    prompt = "Write a python function to calculate fibonacci numbers."
    
    # --- TURN 1: PROPOSAL ---
    print(f"1️⃣  Sending Prompt: '{prompt}'")
    res = requests.post(f"{API_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": session_id
    }, timeout=600000)
    
    data = res.json()
    suggestion = data.get("suggestion", "")

    # Check if we got a Plan
    if "propose_plan" in suggestion or "plan" in suggestion.lower():
        print(colored("   ✅ Agent Proposed a Plan (Planning Engine Active)", "green"))
    else:
        print(colored("   ⚠️ Agent skipped planning (Check logs)", "yellow"))
        print(colored(f"   🔎 RAW RESPONSE: {suggestion}", "white"))

    # --- TURN 2: APPROVAL ---
    print(f"\n2️⃣  Sending Approval: 'Plan approved. Proceed.'")
    
    try:
        res = requests.post(f"{API_URL}/generate-code-suggestion", json={
            "project_id": PROJECT_ID,
            "prompt": "Plan approved. Proceed.",
            "session_id": session_id 
        }, timeout=600)
        
        final_data = res.json()
        suggestion = final_data.get("suggestion", "")
        print(colored("   ✅ Execution Response Received", "green"))
        
        # 🚀 DEBUG: Print preview of what the AI actually said
        print(colored(f"   🔎 AI RESPONSE PREVIEW:\n{suggestion[:300]}...\n", "white"))
        
    except requests.exceptions.ReadTimeout:
        print(colored("   ❌ TIMEOUT: Server is still processing. Check Dashboard logs.", "red"))
        return
    except Exception as e:
        print(colored(f"   ❌ Connection Error: {e}", "red"))
        return

    # --- TURN 3: VERIFICATION ---
    print("\n3️⃣  Checking Disk...")
    
    # Check for either filename (AI might choose fibonacci.py or fibonacci_calculator.py)
    files_to_check = ["fibonacci.py", "fibonacci_calculator.py"]
    found_file = None
    
    for fname in files_to_check:
        full_path = os.path.join(TARGET_PATH, fname)
        if os.path.exists(full_path):
            found_file = full_path
            break
            
    if found_file:
        print(colored(f"   🏆 FILE CREATED: {found_file}", "green", attrs=['bold']))
        with open(found_file, 'r') as f:
            content = f.read()
            if "COPYRIGHT 2026" in content: print("      - Copyright: OK")
            if "_v2" in content: print("      - Naming Rule: OK")
            if "logging" in content: print("      - Logging Rule: OK")
    else:
        print(colored("   ❌ File not found. Check Dashboard logs.", "red"))

if __name__ == "__main__":
    run()