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

def generate_test_for_file(file_path):
    rel_path = os.path.relpath(file_path, PROJECT_ROOT).replace("\\", "/")
    
    # Logic to mirror src/main/java -> src/test/java
    if "src/main/java" in rel_path:
        rel_test_path = rel_path.replace("src/main/java", "src/test/java").replace(".java", "Test.java")
    else:
        # Fallback if structure is weird
        rel_test_path = rel_path.replace(".java", "Test.java")

    filename = os.path.basename(file_path)
    class_name = filename.replace(".java", "")
    test_class_name = class_name + "Test"

    # 🚀 DYNAMIC PACKAGE CALCULATION
    # We calculate the package name from the file path to force the AI to be correct.
    # e.g., src/main/java/assignment/Entity/Account.java -> package assignment.Entity;
    try:
        # Split path by 'src/main/java/' and take the second part
        package_rel_path = rel_path.split("src/main/java/")[1]
        # Remove filename
        package_rel_dir = os.path.dirname(package_rel_path)
        # Convert slashes to dots
        package_name = package_rel_dir.replace("/", ".")
    except:
        package_name = "assignment" # Fallback

    print(colored(f"\n🧪 Generating: {test_class_name}", "cyan"))
    print(f"   Package: {package_name}")
    print(f"   Target:  {rel_test_path}")

    # 🚀 STRICT PROMPT
    prompt = (
        f"I need to create a JUnit 5 test for the Java class '{class_name}'.\n\n"
        f"### CONSTRAINTS (MUST FOLLOW)\n"
        f"1. **Package Declaration**: You MUST use `package {package_name};` at the top.\n"
        f"2. **Class Name**: The test class MUST be named `{test_class_name}`.\n"
        f"3. **Imports**: Do NOT import incorrect classes. Only import what is needed for `{class_name}`.\n"
        f"4. **No Copy-Paste**: Do NOT use code from 'AccountManagement' or other previous files. Generate fresh tests for THIS file.\n\n"
        f"### INSTRUCTIONS\n"
        f"1. Read '{rel_path}' to understand the logic of `{class_name}`.\n"
        f"2. Write the test class `{test_class_name}`.\n"
        f"3. Save it to '{rel_test_path}' using apply_edit."
    )

    # Unique Session ID per file ensures no "Short Term Memory" leak from previous file
    session_id = f"TEST_GEN_{test_class_name}_{int(time.time())}"

    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": session_id
    }

    try:
        start = time.time()
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=180)
        
        duration = time.time() - start
        
        if res.status_code == 200:
            print(colored(f"   ✅ Done in {duration:.2f}s", "green"))
            return True, rel_test_path
        elif res.status_code == 429:
            print(colored("   🛑 API QUOTA EXHAUSTED (429).", "red", attrs=['bold']))
            return False, None
        else:
            print(colored(f"   ❌ Failed: {res.status_code}", "red"))
            return True, None

    except Exception as e:
        print(colored(f"   ❌ Connection Error: {e}", "red"))
        return True, None

def scan_and_generate():
    if not os.path.exists(SRC_DIR):
        print(f"❌ Source directory not found: {SRC_DIR}")
        return

    all_java_files = []
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            if file.endswith(".java"):
                if "Test" in file or "package-info" in file: continue
                all_java_files.append(os.path.join(root, file))

    total_files = len(all_java_files)
    print(colored(f"📂 Found {total_files} candidate files.", "white", attrs=['bold']))

    waiting_for_approval = True 
    files_processed = 0

    for full_path in all_java_files:
        
        should_continue, generated_path = generate_test_for_file(full_path)
        
        if not should_continue: break 

        if waiting_for_approval and generated_path:
            print("\n" + "="*60)
            print(colored("⏸️  BATCH PAUSED FOR HUMAN REVIEW", "yellow", attrs=['bold']))
            print(f"   Check file: {os.path.join(PROJECT_ROOT, generated_path)}")
            print("-" * 60)
            
            while True:
                choice = input(colored(">> Approve batch? (y/n/skip): ", "cyan")).strip().lower()
                if choice == 'y':
                    waiting_for_approval = False
                    break
                elif choice == 'n':
                    return
                elif choice == 'skip':
                    print("Skipping...")
                    break
                else: print("Invalid input.")

        files_processed += 1
        if not waiting_for_approval:
            print(f"   💤 Cooling down (3s)...")
            time.sleep(3) 

    print(colored(f"\n🎉 Completed! Generated tests for {files_processed}/{total_files} files.", "magenta"))

if __name__ == "__main__":
    scan_and_generate()