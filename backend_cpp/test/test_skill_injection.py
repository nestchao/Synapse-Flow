import requests
import json
import base64
import time
from termcolor import colored

API_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest" 
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def run():
    print(colored(f"🧪 TEST 2 (v2): Verifying Skill Injection + Planning...", "cyan", attrs=['bold']))
    
    session_id = f"SKILL_TEST_{int(time.time())}"
    prompt = "Write a python function to calculate fibonacci numbers."
    
    print(f"1️⃣  Sending Prompt: '{prompt}'")
    
    # --- TURN 1: Initial Request ---
    res = requests.post(f"{API_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": session_id
    }, timeout=60)
    
    data = res.json()
    suggestion = data.get("suggestion", "")

    # Check if it entered Planning Mode
    if "proposed a plan" in suggestion.lower():
        print(colored("   ✨ Agent entered Planning Mode (Good!)", "green"))
        print(f"   Response: {suggestion.strip()}")
        
        # --- TURN 2: Approve Plan ---
        print("\n2️⃣  Approving Plan...")
        time.sleep(1)
        res = requests.post(f"{API_URL}/generate-code-suggestion", json={
            "project_id": PROJECT_ID,
            "prompt": "Plan approved. Proceed.",
            "session_id": session_id
        }, timeout=120)
        
        final_data = res.json()
        final_code = final_data.get("suggestion", "")
    else:
        # Agent wrote code immediately
        final_code = suggestion

    # --- VERIFICATION ---
    print("\n" + "="*50)
    print("🔍 CODE ANALYSIS")
    print("="*50)
    print(final_code[:500] + "...\n")
    
    score = 0
    
    # Check Rule 1: Logging instead of print
    if "logging.info" in final_code:
        print(colored("✅ PASS: Agent used 'logging.info' (Rule Followed)", "green"))
        score += 1
    elif "print(" in final_code:
        print(colored("❌ FAIL: Agent used 'print' (Rule Ignored)", "red"))
    else:
        print(colored("⚠️ WARN: No output statement found.", "yellow"))

    # Check Rule 2: Naming Convention (_v2)
    if "_v2" in final_code:
        print(colored("✅ PASS: Function name ends in '_v2' (Rule Followed)", "green"))
        score += 1
    else:
        print(colored("❌ FAIL: Function name missing '_v2' suffix.", "red"))

    # Check Rule 3: Copyright Header
    if "COPYRIGHT 2026" in final_code:
        print(colored("✅ PASS: Copyright header present (Rule Followed)", "green"))
        score += 1
    else:
        print(colored("❌ FAIL: Missing Copyright header.", "red"))

    print("-" * 50)
    if score == 3:
        print(colored("🏆 ALL SKILLS SUCCESSFULLY INJECTED", "green", attrs=['bold']))
    else:
        print(colored(f"⚠️ Partial Success ({score}/3)", "yellow"))

if __name__ == "__main__":
    run()