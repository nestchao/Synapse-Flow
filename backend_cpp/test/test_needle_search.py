import requests
import base64
import os
import time
from termcolor import colored

REST_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest"
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def hide_the_needle():
    print(colored("🏗️  GENERATING HAYSTACK (50 Files)...", "white"))
    # Create noise
    for i in range(50):
        with open(os.path.join(TARGET_PATH, f"module_{i}.py"), "w") as f:
            f.write(f"def noise_{i}():\n    return 'Junk Data {i}'\n")
    
    # Hide the needle in module 37
    with open(os.path.join(TARGET_PATH, "module_37.py"), "a") as f:
        f.write("\n\ndef get_secret_access_key():\n    # The secret code is: 'VOID-SOUL-999'\n    return True\n")
    print("✅ Needle 'VOID-SOUL-999' hidden in module_37.py")

def run_stress_test():
    hide_the_needle()
    
    print("🔄 Synchronizing Engine...")
    requests.post(f"{REST_URL}/sync/run/{PROJECT_ID}", json={})
    time.sleep(5) # Wait for Vector Indexing

    prompt = "Look through all modules and find the secret access key. What is its value?"
    print(f"🧠 Prompting Agent: {colored(prompt, 'yellow')}")

    start_time = time.time()
    res = requests.post(f"{REST_URL}/generate-code-suggestion", json={
        "project_id": PROJECT_ID,
        "prompt": prompt,
        "session_id": f"STRESS_TEST_{int(time.time())}"
    }, timeout=120)

    if res.status_code == 200:
        answer = res.json().get("suggestion", "")
        print(f"\n🤖 Agent Response: {answer}")
        
        if "VOID-SOUL-999" in answer:
            print(colored(f"\n🏆 SUCCESS: Needle found in {time.time()-start_time:.1f}s!", "green", attrs=["bold"]))
        else:
            print(colored("\n❌ FAILED: The Agent missed the secret.", "red"))
    else:
        print(f"❌ Error: {res.text}")

if __name__ == "__main__":
    run_stress_test()