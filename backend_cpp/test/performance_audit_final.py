import requests
import base64
import time
import os
import shutil
import statistics
from termcolor import colored
from concurrent.futures import ThreadPoolExecutor

# --- CONFIGURATION ---
REST_URL = "http://127.0.0.1:5002"
PROJECT_PATH = os.path.abspath(r"D:\Projects\SkillTest").replace("\\", "/")
PROJECT_ID = base64.b64encode(PROJECT_PATH.encode('utf-8')).decode('utf-8')

class SynapseFullAuditor:
    def __init__(self):
        self.session = requests.Session()

    def print_header(self, text):
        print(f"\n{colored('='*70, 'white')}")
        print(f"{colored('🚀 ' + text, 'cyan', attrs=['bold'])}")
        print(f"{colored('='*70, 'white')}")

    def run_sync_stress(self, file_count=100):
        self.print_header(f"PHASE 1: INDEXING THROUGHPUT ({file_count} Files)")
        if not os.path.exists(PROJECT_PATH): os.makedirs(PROJECT_PATH)
        
        # Generate files
        for i in range(file_count):
            with open(os.path.join(PROJECT_PATH, f"perf_module_{i}.py"), "w") as f:
                f.write(f"def func_logic_{i}(data):\n    '''Logic for node {i}'''\n    return data * {i}\n")

        start = time.time()
        self.session.post(f"{REST_URL}/sync/register/{PROJECT_ID}", json={"local_path": PROJECT_PATH, "allowed_extensions": ["py"]})
        self.session.post(f"{REST_URL}/sync/run/{PROJECT_ID}", json={})
        
        # Polling Telemetry
        while True:
            try:
                telem = self.session.get(f"{REST_URL}/api/admin/telemetry").json()
                if telem['metrics'].get('last_sync_duration_ms', 0) > 0:
                    duration = telem['metrics']['last_sync_duration_ms'] / 1000
                    print(colored(f"✅ Indexed {file_count} files.", "green"))
                    print(colored("⏳ Cooling down API for 10 seconds...", "yellow"))
                    time.sleep(10) # 🚀 ESSENTIAL: Prevents 429 Rate Limits
                    break
            except: pass
            time.sleep(0.5)

    def run_ghost_latency(self, iters=20):
        self.print_header("PHASE 2: GHOST TEXT LATENCY (Sequential)")
        latencies = []
        payload = {"prefix": "def fib(n):\n    ", "suffix": "", "project_id": PROJECT_ID, "file_path": "math.py"}
        
        for i in range(iters):
            t0 = time.time()
            try:
                self.session.post(f"{REST_URL}/complete", json=payload, timeout=5)
                latencies.append((time.time() - t0) * 1000)
            except: pass
        
        if latencies:
            avg = statistics.mean(latencies)
            p95 = sorted(latencies)[int(len(latencies)*0.95)-1]
            print(f"📊 Avg: {colored(f'{avg:.0f}ms', 'green')} | P95: {colored(f'{p95:.0f}ms', 'yellow')}")

    def run_rag_precision(self, retries=5): # 🚀 Added 'retries' parameter here
        self.print_header("PHASE 3: RAG PRECISION (Rank-1 Accuracy)")
        print("   (Waiting for index stabilization...)")
        time.sleep(3) 

        for attempt in range(retries):
            try:
                res = self.session.post(f"{REST_URL}/retrieve-context-candidates", 
                                        json={"project_id": PROJECT_ID, "prompt": "Find the logic for perf module 50"},
                                        timeout=10)
                
                if res.status_code == 200:
                    candidates = res.json().get('candidates', [])
                    found_rank = 0
                    for i, c in enumerate(candidates):
                        if "perf_module_50" in c.get('file_path', ''):
                            found_rank = i + 1
                            break
                    
                    if found_rank == 1:
                        print(colored("🎯 PERFECT MATCH: Target found at Rank 1", "green", attrs=['bold']))
                    elif found_rank > 0:
                        print(colored(f"⚠️  ACCURACY DROP: Target found at Rank {found_rank}", "yellow"))
                    else:
                        print(colored("❌ NOT FOUND: Target missing from top results", "red"))
                    return # Exit function on successful API call
                else:
                    print(colored(f"   ⚠️ Attempt {attempt+1} failed (HTTP {res.status_code}). Retrying...", "yellow"))
            except Exception as e:
                print(colored(f"   ⚠️ Attempt {attempt+1} connection error. Retrying...", "yellow"))
            
            time.sleep(5)
        
        print(colored("❌ PHASE 3 FAILED after multiple retries.", "red"))

    def run_agent_loop(self):
        self.print_header("PHASE 4: AGENT SELF-HEALING SPEED")
        prompt = "Create a file 'verify_test.py'. Write a function with a purposeful indentation error. Then use self-healing to fix it."
        
        print("   (Agent is thinking and executing tools...)")
        start = time.time()
        try:
            res = self.session.post(f"{REST_URL}/generate-code-suggestion", 
                                    json={"project_id": PROJECT_ID, "prompt": prompt}, 
                                    timeout=180) # High timeout for agent loops
            duration = time.time() - start
            
            if res.status_code == 200:
                print(colored(f"🤖 Agent Loop Finished in {duration:.2f}s", "magenta"))
                print(f"📄 Result Preview: {res.json().get('suggestion', '')[:100]}...")
            else:
                print(colored(f"❌ Agent Error: HTTP {res.status_code}", "red"))
        except Exception as e:
            print(colored(f"💥 Agent Connection Reset (Check if C++ Server Crashed): {e}", "red"))

if __name__ == "__main__":
    auditor = SynapseFullAuditor()
    auditor.run_sync_stress(100)
    auditor.run_ghost_latency(20)
    auditor.run_rag_precision()
    auditor.run_agent_loop()