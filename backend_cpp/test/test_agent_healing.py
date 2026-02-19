import requests
import base64
import time
import os
import json
from termcolor import colored

# --- CONFIGURATION ---
REST_URL = "http://127.0.0.1:5002"
# Normalize path to forward slashes for cross-platform consistency
TARGET_PATH = os.path.abspath(r"D:\Projects\SkillTest").replace("\\", "/")
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def run_mission():
    session = requests.Session()
    
    print(colored("\n" + "="*70, "cyan"))
    print(colored("✦ SYNAPSE-FLOW: AGENT SELF-HEALING MISSION ✦", "cyan", attrs=["bold"]))
    print(colored("="*70, "cyan"))

    # 1. ATOMIC REGISTRATION
    # We register the folder so the C++ Agent knows where its "Hands" are.
    print(f"📡 Registering Workspace: {colored(TARGET_PATH, 'white')}")
    try:
        reg_res = session.post(f"{REST_URL}/sync/register/{PROJECT_ID}", json={
            "local_path": TARGET_PATH,
            "allowed_extensions": ["py", "txt"]
        }, timeout=10)
        if reg_res.status_code != 200:
            print(colored(f"❌ Registration Failed: {reg_res.text}", "red"))
            return
    except Exception as e:
        print(colored(f"❌ Connection Error: {e}", "red"))
        return

    # 2. DEFINE MISSION PARAMETERS
    # We use a multi-step prompt to force the Agent to use tools (File I/O and Shell).
    prompt = (
        "MISSION: Verify system integrity. "
        "Step 1: Create a python file 'self_heal.py' with a DELIBERATE syntax error (missing a colon). "
        "Step 2: Use the 'run_command' tool to try and compile it. "
        "Step 3: When the build fails, identify the error and use 'apply_edit' to fix the file. "
        "Step 4: Confirm success via 'FINAL_ANSWER'."
    )
    
    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": f"MISSION_HEAL_{int(time.time())}"
    }

    print(colored("\n🧠 SENDING MISSION TO AGENT...", "yellow"))
    print(colored("⏳ Note: This involves reasoning loops. It may take 60-120 seconds.", "white", attrs=["dark"]))

    start_time = time.time()
    try:
        # High timeout (10 mins) to allow for 10 reasoning steps
        res = session.post(f"{REST_URL}/generate-code-suggestion", json=payload, timeout=600)
        duration = time.time() - start_time

        if res.status_code == 200:
            print(colored(f"\n✅ MISSION ACCOMPLISHED ({duration:.1f}s)", "green", attrs=["bold"]))
            print(colored("-" * 70, "white"))
            
            # The AI might return the final answer as a raw string or JSON suggestion
            data = res.json()
            suggestion = data.get("suggestion", "")
            print(suggestion)
            
            print(colored("-" * 70, "white"))
            
            # 3. DISK VERIFICATION
            check_path = os.path.join(TARGET_PATH, "self_heal.py")
            if os.path.exists(check_path):
                print(colored(f"📁 Verification: 'self_heal.py' exists on disk.", "green"))
                with open(check_path, 'r') as f:
                    print(colored("📄 File Content:", "white"))
                    print(f.read())
            else:
                print(colored("⚠️ Warning: Mission claimed success but file not found on disk.", "yellow"))

        else:
            print(colored(f"\n❌ MISSION FAILURE (HTTP {res.status_code})", "red", attrs=["bold"]))
            print(f"Error Log: {res.text}")
            if "316" in res.text:
                print(colored("\n🎯 DIAGNOSIS: UTF-8 characters are still leaking into the JSON dump.", "magenta"))
            
    except requests.exceptions.Timeout:
        print(colored("\n⏰ ERROR: Agent connection timed out. Check the C++ Brain console for crashes.", "red"))
    except Exception as e:
        print(colored(f"\n💥 CRITICAL SYSTEM ERROR: {e}", "red"))

if __name__ == "__main__":
    # Ensure a fresh environment
    if not os.path.exists(TARGET_PATH):
        os.makedirs(TARGET_PATH)
    
    # Run the test
    run_mission()