import requests
import json
import time
import sys
import os

# --- WINDOWS CONSOLE FIX ---
# Forces UTF-8 so emojis (✅, 🔴) don't crash the build system
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# Dependency Check
try:
    from termcolor import colored
except ImportError:
    print("⚠️  'termcolor' module not found. Run: pip install termcolor")
    # Fallback to plain text if missing
    def colored(text, color=None, attrs=None): return text

# CONFIGURATION
API_URL = "http://localhost:5002"
TELEMETRY_URL = f"{API_URL}/api/admin/telemetry"
GENERATE_URL = f"{API_URL}/generate-code-suggestion"

# !!! IMPORTANT !!! 
# REPLACE THIS with the Project ID seen in your Dashboard Logs
TARGET_PROJECT_ID = "REPLACE_WITH_YOUR_ACTUAL_PROJECT_ID" 

TEST_CASES = [
    {
        "name": "Architecture Check",
        "prompt": "explain the whole architecture",
        "expected_files": ["main.ts", "app.config.ts"], 
    },
    {
        "name": "Login Logic",
        "prompt": "how does the login component work?",
        "expected_files": ["login.component.ts"],
    }
]

def run_tests():
    print(colored(f"\n🚀 STARTING FLIGHT CERTIFICATION FOR: {TARGET_PROJECT_ID}", "cyan", attrs=['bold']))
    
    # Check if server is up
    try:
        requests.get(f"{API_URL}/api/hello", timeout=1)
    except requests.exceptions.ConnectionError:
        print(colored("❌ CRITICAL: Server is OFFLINE. Run code_assistance_server.exe first.", "red"))
        sys.exit(1)

    total_score = 0
    
    for test in TEST_CASES:
        print(f"\n🧪 Testing: {test['name']}...")
        
        # 1. Fire Request
        try:
            payload = {
                "project_id": TARGET_PROJECT_ID,
                "prompt": test['prompt'],
                "use_hyde": False
            }
            # Wait for generation (this blocks until AI is done)
            requests.post(GENERATE_URL, json=payload)
        except Exception as e:
            print(colored(f"❌ API Failed: {e}", "red"))
            continue
            
        # 2. Inspect Telemetry
        # The log we just generated should be at index 0 (newest)
        time.sleep(0.5) # Slight buffer for async write
        telem_res = requests.get(TELEMETRY_URL)
        telemetry = telem_res.json()
        
        if not telemetry['logs']:
            print(colored("❌ NO LOGS FOUND. Check Project ID.", "red"))
            continue

        latest_log = telemetry['logs'][0]
        
        # 3. Validation Logic
        context = latest_log['full_prompt']
        retrieved_files = []
        
        for line in context.split('\n'):
            if line.startswith("# FILE:"):
                parts = line.split('|')
                path = parts[0].replace("# FILE:", "").strip()
                retrieved_files.append(path)
                
        found_count = 0
        missing = []
        
        for expected in test['expected_files']:
            # Flexible matching (find "main.ts" inside "src/main.ts")
            if any(expected in f for f in retrieved_files):
                found_count += 1
            else:
                missing.append(expected)
        
        score = (found_count / len(test['expected_files'])) * 100
        total_score += score
        
        # 4. Report Card
        if len(missing) == 0:
            print(colored(f"  ✅ RETRIEVAL: PERFECT ({len(retrieved_files)} files scanned)", "green"))
        elif score > 0:
            print(colored(f"  ⚠️ RETRIEVAL: PARTIAL ({score:.0f}%) - Missing: {missing}", "yellow"))
        else:
            print(colored(f"  🔴 RETRIEVAL: FAILED - Context Empty or Wrong", "red"))
            
        print(f"  ⏱️ LATENCY:   {latest_log['duration_ms']:.0f}ms")

    print(colored("\n" + "="*40, "white"))
    final_avg = total_score / len(TEST_CASES)
    print(f"🏆 FINAL SCORE: {final_avg:.1f}/100")

if __name__ == "__main__":
    if TARGET_PROJECT_ID == "REPLACE_WITH_YOUR_ACTUAL_PROJECT_ID":
        print(colored("❌ CONFIG ERROR: Please open test_quality.py and set TARGET_PROJECT_ID", "red"))
    else:
        run_tests()