import requests
import base64
import os
import time
from termcolor import colored

REST_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest"
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')
SKILL_DIR = os.path.join(TARGET_PATH, ".study_assistant", "business_metadata")

def run_skill_test():
    print(colored("🚀 TESTING SKILL INJECTION...", "cyan", attrs=["bold"]))

    # 1. Create the Skill Rule
    if not os.path.exists(SKILL_DIR): os.makedirs(SKILL_DIR)
    
    rule_content = """
    RULE_NAME: Global_Coding_Standard
    RULES:
    - Every file MUST start with the header: '# COPYRIGHT 2026 SYNAPSE-FLOW'
    - Use the variable name 'logger_v2' for all print statements.
    - All functions must have a docstring starting with 'STRICT:'
    """
    
    with open(os.path.join(SKILL_DIR, "coding_rules.yaml"), "w") as f:
        f.write(rule_content)
    print("✅ Skill 'coding_rules.yaml' injected into project metadata.")

    # 2. Trigger a sync so the C++ Brain loads the new skill
    requests.post(f"{REST_URL}/sync/run/{PROJECT_ID}", json={})
    time.sleep(2) # Give it a second to index

    # 3. Prompt the Agent
    prompt = "Create a file 'utility.py' with a function that adds two numbers."
    print(f"🧠 Prompting Agent: {prompt}")

    res = requests.post(f"{REST_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": "SKILL_PROBE_001"
    }, timeout=120)

    if res.status_code == 200:
        suggestion = res.json().get("suggestion", "")
        print(colored("\n🤖 Agent Response:", "green"))
        print(suggestion)

        # 4. Verify Compliance
        passed = True
        if "COPYRIGHT 2026" not in suggestion: 
            print(colored("❌ FAILED: Missing Copyright Header", "red"))
            passed = False
        if "STRICT:" not in suggestion:
            print(colored("❌ FAILED: Missing Strict Docstring", "red"))
            passed = False
        
        if passed:
            print(colored("\n🏆 SUCCESS: Agent followed all Business Rules!", "green", attrs=["bold"]))
    else:
        print(colored(f"❌ Error: {res.text}", "red"))

if __name__ == "__main__":
    run_skill_test()