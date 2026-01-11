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

# Path inside the project
SRC_DIR = os.path.join(PROJECT_ROOT, "OOP", "src", "main", "java")
TEST_DIR = os.path.join(PROJECT_ROOT, "OOP", "src", "test", "java")

def generate_test_for_file(file_path):
    # Calculate relative paths
    rel_path = os.path.relpath(file_path, PROJECT_ROOT).replace("\\", "/")
    
    # Calculate target test path (e.g., src/main/.../Entity.java -> src/test/.../EntityTest.java)
    rel_test_path = rel_path.replace("src/main/java", "src/test/java").replace(".java", "Test.java")
    
    filename = os.path.basename(file_path)
    
    print(colored(f"\n🧪 Generating tests for: {filename}", "cyan"))
    print(f"   Target: {rel_test_path}")

    # 🚀 THE PROMPT
    # We instruct the AI to read the specific file and create a test for it.
    prompt = (
        f"I need to help user to understand and use Unit Test for '{rel_path}'.\n"
        f"1. Read '{rel_path}' to understand its methods and logic.\n"
        f"2. How to run the Unit Test\n"
    )

    payload = {
        "project_id": PROJECT_ID,
        "prompt": prompt
    }

    try:
        start = time.time()
        # 90s timeout because writing code + thinking takes time
        res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=90)
        
        duration = time.time() - start
        
        if res.status_code == 200:
            print(colored(f"   ✅ Done in {duration:.2f}s", "green"))
        else:
            print(colored(f"   ❌ Failed: {res.text}", "red"))

    except Exception as e:
        print(colored(f"   ❌ Error: {e}", "red"))

def scan_and_generate():
    if not os.path.exists(SRC_DIR):
        print(f"❌ Source directory not found: {SRC_DIR}")
        return

    files_processed = 0
    
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            if file.endswith(".java"):
                full_path = os.path.join(root, file)
                
                # Skip existing tests or irrelevant files
                if "Test" in file or "package-info" in file:
                    continue

                generate_test_for_file(full_path)
                files_processed += 1
                
                # 🛑 RATE LIMITING (Optional)
                # Sleep briefly to let the C++ backend clear its memory/logs
                time.sleep(2) 

    print(colored(f"\n🎉 Completed! Generated tests for {files_processed} files.", "magenta", attrs=['bold']))

if __name__ == "__main__":
    scan_and_generate()