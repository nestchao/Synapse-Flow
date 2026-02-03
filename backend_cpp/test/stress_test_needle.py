import os
import time
import requests
import base64
import shutil
import random
import string

# CONFIG
API_URL = "http://127.0.0.1:5002"
# 🚀 FIX: Dynamic path to prevent WinError 32 lock issues
TEST_ROOT = r"D:\Projects"
TARGET_DIR = os.path.join(TEST_ROOT, f"TokenStressTest_{int(time.time())}")
PROJECT_ID = base64.b64encode(TARGET_DIR.encode('utf-8')).decode('utf-8')

# The "Needle" the AI must find
SECRET_KEY = "SYNAPSE_SECRET_KEY_999"
NEEDLE_CODE = f"export const CONFIG_SECRET = '{SECRET_KEY}';"

def generate_junk_file(filename, size_kb):
    """Generates a file with random code-like noise."""
    content = []
    content.append(f"// Start of {filename}")
    for _ in range(size_kb * 10): 
        var_name = ''.join(random.choices(string.ascii_lowercase, k=8))
        val = random.randint(0, 1000)
        content.append(f"const {var_name} = {val};")
        content.append(f"function func_{var_name}() {{ return {var_name} * 2; }}")
    return "\n".join(content)

def setup_environment(total_size_mb, needle_position_percent):
    if os.path.exists(TARGET_DIR):
        try:
            shutil.rmtree(TARGET_DIR)
        except OSError:
            print(f"⚠️ Warning: Could not fully clean previous folder. Using new path.")

    os.makedirs(TARGET_DIR, exist_ok=True)

    total_files = 20 
    file_size_kb = int((total_size_mb * 1024) // total_files)
    if file_size_kb < 1: file_size_kb = 1
    
    needle_file_index = int(total_files * (needle_position_percent / 100))
    
    print(f"🏗️ Generating {total_size_mb}MB Project at: {TARGET_DIR}")

    for i in range(total_files):
        fname = f"data_chunk_{i}.ts"
        content = generate_junk_file(fname, file_size_kb)
        
        if i == needle_file_index:
            content += f"\n\n// CRITICAL SECTION\n{NEEDLE_CODE}\n\n"
            
        with open(os.path.join(TARGET_DIR, fname), "w") as f:
            f.write(content)

    print("🔄 Syncing to Backend...")
    try:
        requests.post(f"{API_URL}/sync/register/{PROJECT_ID}", json={
            "local_path": TARGET_DIR,
            "allowed_extensions": ["ts"],
            "ignored_paths": [], "included_paths": []
        })
        requests.post(f"{API_URL}/sync/run/{PROJECT_ID}", json={})
        time.sleep(5) 
    except Exception as e:
        print(f"❌ Sync failed: {e}")

def run_probe(size_mb):
    prompt = "What is the value of CONFIG_SECRET? Return ONLY the value."
    
    print(f"🚀 Probing AI with {size_mb}MB Context...")
    start = time.time()
    try:
        res = requests.post(f"{API_URL}/generate-code-suggestion", json={
            "project_id": PROJECT_ID,
            "prompt": prompt,
            "session_id": f"STRESS_{int(time.time())}"
        }, timeout=300) 
        
        duration = time.time() - start
        response = res.json().get("suggestion", "")
        
        success = SECRET_KEY in response
        status = "✅ PASS" if success else "❌ FAIL"
        
        print(f"   result: {status} | Time: {duration:.2f}s")
        return success, duration
    except Exception as e:
        print(f"   💥 CRASH: {e}")
        return False, 0

# --- MAIN EXECUTION LOOP ---
SIZES_TO_TEST = [0.5, 1.0, 2.0] # Reduced sizes for quicker debug

results = []

for size in SIZES_TO_TEST:
    setup_environment(size, 50) 
    success, time_taken = run_probe(size)
    results.append({"size_mb": size, "success": success, "time": time_taken})
    
    if not success:
        print("🛑 Stop Testing: Limit Reached.")
        break

print("\n📊 FINAL REPORT:")
for r in results:
    print(f"Size: {r['size_mb']}MB | Pass: {r['success']} | Latency: {r['time']:.2f}s")