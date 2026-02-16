import requests
import base64
import time
import os
from termcolor import colored

# --- CONFIGURATION (Matches your environment) ---
REST_URL = "http://127.0.0.1:5002"
# Normalizing path for Windows
PROJECT_PATH = os.path.abspath(r"D:\Projects\SkillTest").replace("\\", "/")
PROJECT_ID = base64.b64encode(PROJECT_PATH.encode('utf-8')).decode('utf-8')

def run_isolated_phase4():
    session = requests.Session()
    
    print(colored("="*70, "cyan"))
    print(colored("🚀 ISOLATED TEST: PHASE 4 - AGENT SELF-HEALING", "cyan", attrs=["bold"]))
    print(colored("="*70, "cyan"))

    # 1. Quick Register (Essential so the Agent knows the workspace root)
    print(f"📡 Registering workspace: {PROJECT_PATH}")
    try:
        reg_res = session.post(f"{REST_URL}/sync/register/{PROJECT_ID}", json={
            "local_path": PROJECT_PATH,
            "allowed_extensions": ["py"]
        }, timeout=5)
        if reg_res.status_code != 200:
            print(colored(f"❌ Registration Failed: {reg_res.text}", "red"))
            return
    except Exception as e:
        print(colored(f"❌ Connection Error: {e}", "red"))
        return

    # 2. Trigger the Agent Task
    prompt = (
        "Create a file 'verify_test.py'. Write a function with a purposeful "
        "indentation error. Then use your self-healing logic to fix it."
    )
    
    print(f"🧠 Prompting Agent: {colored(prompt, 'yellow')}")
    print("⏳ (Agent is forming a plan and executing tools... this takes 30-60s)")
    
    start_time = time.time()
    try:
        # High timeout (5 minutes) for Agent loops
        res = session.post(f"{REST_URL}/generate-code-suggestion", json={
            "project_id": PROJECT_ID,
            "prompt": prompt,
            "session_id": f"DEBUG_PH4_{int(time.time())}"
        }, timeout=300)
        
        duration = time.time() - start_time

        if res.status_code == 200:
            print(colored(f"\n✅ Success! Agent responded in {duration:.2f}s", "green", attrs=["bold"]))
            print(colored("-" * 70, "white"))
            print(res.json().get("suggestion", "No text returned."))
            print(colored("-" * 70, "white"))
        else:
            print(colored(f"\n❌ Agent Error: HTTP {res.status_code}", "red", attrs=["bold"]))
            print(f"Details: {res.text}")
            if "type_error.316" in res.text:
                print(colored("\n🎯 DIAGNOSIS: The UTF-8 crash is still active in the C++ backend.", "magenta"))
                print(colored("Please apply the 'fix_utf8.py' script to the backend code first.", "magenta"))

    except requests.exceptions.Timeout:
        print(colored("\n⏰ Error: Agent Timed Out (check C++ console for crashes).", "red"))
    except Exception as e:
        print(colored(f"\n💥 Crash: {e}", "red"))

if __name__ == "__main__":
    # Ensure test dir exists
    if not os.path.exists(PROJECT_PATH):
        os.makedirs(PROJECT_PATH)
    run_isolated_phase4()