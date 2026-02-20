import requests
import json
import base64
import time
import os
from termcolor import colored

API_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest" 
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def run():
    print(colored(f"💾 TEST 3: Verifying Skill Persistence (Disk Check)...", "cyan", attrs=['bold']))
    
    # 1. Register
    requests.post(f"{API_URL}/sync/register/{PROJECT_ID}", json={
        "local_path": TARGET_PATH, 
        "allowed_extensions": ["py", "yaml"],
        "ignored_paths": [], 
        "included_paths": []
    })

    # 2. Strong Prompt: Explicitly asks for a FILE
    prompt = "Create a file named 'fib_service.py'. Inside it, write a function to calculate fibonacci numbers. Ensure it follows all company coding standards."
    session_id = f"PERSIST_TEST_{int(time.time())}"
    
    print(f"1️⃣  Sending Prompt: '{prompt}'")
    
    # --- TURN 1: Proposal ---
    res = requests.post(f"{API_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": session_id
    }, timeout=60)
    
    data = res.json()
    suggestion = data.get("suggestion", "")

    if "proposed a plan" in suggestion.lower():
        print(colored("   ✨ Plan Proposed. Approving...", "green"))
        
        # --- TURN 2: Execution ---
        time.sleep(1)
        res = requests.post(f"{API_URL}/generate-code-suggestion", json={
            "project_id": PROJECT_ID,
            "prompt": "Plan approved. Execute the file creation now.",
            "session_id": session_id
        }, timeout=120)
        
        print("   🤖 Agent finished execution loop.")
    else:
        print("   ⚠️  Agent acted immediately (No Plan).")

    # --- TURN 3: Disk Verification ---
    print("\n" + "="*50)
    print("🔍 DISK VERIFICATION")
    print("="*50)
    
    target_file = os.path.join(TARGET_PATH, "fib_service.py")
    
    if os.path.exists(target_file):
        print(colored(f"✅ FILE FOUND: {target_file}", "green"))
        
        with open(target_file, 'r') as f:
            content = f.read()
            
        print("\n--- CONTENT PREVIEW ---")
        print(content)
        print("-----------------------")
        
        score = 0
        if "logging.info" in content: score += 1
        if "_v2" in content: score += 1
        if "COPYRIGHT 2026" in content: score += 1
        
        if score == 3:
            print(colored("\n🏆 SUCCESS: File created AND Skills applied!", "green", attrs=['bold']))
        else:
            print(colored(f"\n⚠️  PARTIAL: File created but missed {3-score} rules.", "yellow"))
    else:
        print(colored(f"❌ FAIL: File not found at {target_file}", "red"))

if __name__ == "__main__":
    run()