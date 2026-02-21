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
    - Every file MUST start with the exact header: '# COPYRIGHT 2026 SYNAPSE-FLOW'
    - Use the variable name 'logger_v2' for all print statements.
    - All functions must have a docstring starting with 'STRICT:'
    """
    
    with open(os.path.join(SKILL_DIR, "coding_rules.yaml"), "w") as f:
        f.write(rule_content)
    print("✅ Skill 'coding_rules.yaml' verified on disk.")

    # 2. Prompt the Agent (Notice the hint to trigger the Vector Search)
    prompt = "Create a file 'utility_v2.py' with a function that adds two numbers. Ensure you follow the Global Coding Standards."
    print(f"🧠 Prompting Agent: {prompt}")

    res = requests.post(f"{REST_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": f"SKILL_PROBE_{int(time.time())}"
    }, timeout=120)

    if res.status_code == 200:
        suggestion = res.json().get("suggestion", "")
        print(colored("\n🤖 Agent Response:", "green"))
        print(suggestion)

        # 3. Verify Compliance
        passed = True
        
        # Check actual file if the AI applied the edit
        check_path = os.path.join(TARGET_PATH, "utility_v2.py")
        content_to_check = suggestion
        
        if os.path.exists(check_path):
            with open(check_path, 'r') as f:
                content_to_check = f.read()
                print(colored(f"\n📁 Validating actual file on disk...", "yellow"))

        if "COPYRIGHT 2026" not in content_to_check: 
            print(colored("❌ FAILED: Missing Copyright Header", "red"))
            passed = False
        if "STRICT:" not in content_to_check:
            print(colored("❌ FAILED: Missing Strict Docstring", "red"))
            passed = False
        if "logger_v2" not in content_to_check:
            print(colored("❌ FAILED: Missing logger_v2 requirement", "red"))
            passed = False
            
        if passed:
            print(colored("\n🏆 SUCCESS: Agent followed all Business Rules perfectly!", "green", attrs=["bold"]))
    else:
        print(colored(f"❌ Error: {res.text}", "red"))

if __name__ == "__main__":
    run_skill_test()