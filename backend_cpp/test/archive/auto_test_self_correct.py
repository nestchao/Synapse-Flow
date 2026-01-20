import requests
import json
import base64
import os
import time
from termcolor import colored

# 🚀 CONFIGURATION
API_URL = "http://127.0.0.1:5002"
PROJECT_ROOT = "D:/Projects/OOPAssignment"
PROJECT_ID = base64.b64encode(PROJECT_ROOT.encode('utf-8')).decode('utf-8')

SRC_DIR = os.path.join(PROJECT_ROOT, "OOP", "src", "main", "java")

def autonomous_test_generation(file_path):
    rel_path = os.path.relpath(file_path, PROJECT_ROOT).replace("\\", "/")
    filename = os.path.basename(file_path)
    class_name = filename.replace(".java", "")
    test_class_name = class_name + "Test"
    
    # Calculate package
    try:
        package_rel = rel_path.split("src/main/java/")[1]
        package_name = os.path.dirname(package_rel).replace("/", ".")
    except:
        package_name = "assignment"

    rel_test_path = rel_path.replace("src/main/java", "src/test/java").replace(".java", "Test.java")

    print(colored(f"\n🧪 Target: {test_class_name}", "cyan", attrs=['bold']))

    # 🚀 THE SUPER-PROMPT
    # Instructs AI to Write -> Run -> Fix
    prompt = (
        f"Goal: Create and Verify a JUnit 5 test for '{class_name}'.\n\n"
        f"### CONSTRAINTS\n"
        f"1. Package: `package {package_name};`\n"
        f"2. File Path: `{rel_test_path}`\n\n"
        f"### EXECUTION LOOP (YOU MUST FOLLOW THIS)\n"
        f"1. **Read**: Read '{rel_path}' to understand the class.\n"
        f"2. **Write**: Create '{test_class_name}' with basic tests. Save it.\n"
        f"3. **Verify**: Use `run_command` to execute: `mvn test -Dtest={test_class_name}`\n"
        f"4. **Fix**: If `mvn` returns errors (compilation or failure), read the error log, EDIT the test file to fix imports/logic, and RUN IT AGAIN.\n"
        f"5. **Finish**: Only output FINAL_ANSWER when the test passes or you have tried 3 fixes.\n"
    )

    session_id = f"AUTO_GEN_{class_name}_{int(time.time())}"

    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": session_id
    }

    try:
        start = time.time()
        # Give it 3 minutes to loop through errors
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=180)
        
        if res.status_code == 200:
            print(colored(f"   ✅ Agent Finished ({time.time()-start:.1f}s)", "green"))
            print(f"   Check Dashboard for the fix loop.")
        else:
            print(colored(f"   ❌ Agent Failed: {res.status_code}", "red"))

    except Exception as e:
        print(colored(f"   ❌ Error: {e}", "red"))

def main():
    if not os.path.exists(SRC_DIR):
        print("Source dir not found.")
        return

    # Scan for files
    files_to_process = []
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            if file.endswith(".java") and "Test" not in file:
                files_to_process.append(os.path.join(root, file))

    print(f"Found {len(files_to_process)} files.")

    # Process
    for f in files_to_process:
        autonomous_test_generation(f)
        time.sleep(2) # Brief cooldown

if __name__ == "__main__":
    main()