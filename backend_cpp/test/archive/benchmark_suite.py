import requests
import base64
import time
import statistics
import os
from concurrent.futures import ThreadPoolExecutor
from termcolor import colored

# --- CONFIGURATION ---
REST_URL = "http://127.0.0.1:5002"
TARGET_DIR = r"D:\Projects\SkillTest" 
PROJECT_ID = base64.b64encode(TARGET_DIR.encode('utf-8')).decode('utf-8')

class SynapseBenchmarker:
    def __init__(self):
        self.session = requests.Session() # Use session for connection pooling

    def get_system_vitals(self):
        """Fetch real-time metrics from the C++ SystemMonitor."""
        try:
            res = self.session.get(f"{REST_URL}/api/admin/telemetry", timeout=2)
            if res.status_code == 200:
                m = res.json().get('metrics', {})
                return f"CPU: {m.get('cpu', 0):.1f}% | RAM: {m.get('ram_mb', 0)}MB | Cache: {m.get('cache_size_mb', 0):.1f}MB"
        except: return "Telemetry Offline"

    # --- GHOST TEXT TESTS ---

    def test_ghost_speed(self, iterations=5):
        print(colored(f"\n🚀 PHASE 1: Ghost Text Latency (Sequential)", "cyan"))
        latencies = []
        payload = {
            "prefix": "def sort_list(items):\n    ",
            "suffix": "\n",
            "project_id": PROJECT_ID,
            "file_path": "benchmark.py"
        }

        for i in range(iterations):
            start = time.time()
            try:
                res = self.session.post(f"{REST_URL}/complete", json=payload, timeout=10)
                lat = (time.time() - start) * 1000
                if res.status_code == 200:
                    latencies.append(lat)
                    print(f"   [{i+1}] {lat:.0f}ms | {self.get_system_vitals()}")
            except Exception as e: print(f"   [{i+1}] FAILED: {e}")

        if latencies:
            print(colored(f"📊 AVG: {statistics.mean(latencies):.0f}ms | P95: {max(latencies):.0f}ms", "green"))

    def test_ghost_pressure(self, concurrent_users=5, total_requests=20):
        print(colored(f"\n🔥 PHASE 2: Ghost Text Pressure (Concurrency: {concurrent_users})", "magenta"))
        
        def task():
            start = time.time()
            try:
                r = requests.post(f"{REST_URL}/complete", json={
                    "prefix": "class UserProfile:\n    ", "suffix": "",
                    "project_id": PROJECT_ID, "file_path": "pressure.py"
                }, timeout=15)
                return (time.time() - start) * 1000 if r.status_code == 200 else None
            except: return None

        start_time = time.time()
        with ThreadPoolExecutor(max_workers=concurrent_users) as executor:
            results = list(executor.map(lambda _: task(), range(total_requests)))
        
        duration = time.time() - start_time
        valid = [r for r in results if r]
        
        print(colored(f"🏁 Finished {total_requests} requests in {duration:.2f}s", "white"))
        print(f"   Success Rate: {len(valid)}/{total_requests}")
        if valid:
            print(f"   Median Latency under Load: {statistics.median(valid):.0f}ms")

    # --- CHAT / AGENT TESTS ---

    def test_agent_stress(self):
        """Tests the Browser Bridge + C++ Agent reasoning ability."""
        print(colored(f"\n🧠 PHASE 3: Chat Agent Cognitive Load", "yellow"))
        prompt = "Create a python script 'stress_test.py'. Inside, write 50 functions that return their own index name. Then use execute_code to run the last function."
        
        payload = {
            "project_id": PROJECT_ID,
            "prompt": prompt,
            "session_id": f"STRESS_{int(time.time())}"
        }

        print("   (Sending complex multi-step reasoning task to Browser Bridge...)")
        start = time.time()
        try:
            # Note: 5-minute timeout for Agentic loops
            res = self.session.post(f"{REST_URL}/generate-code-suggestion", json=payload, timeout=300)
            duration = time.time() - start
            
            if res.status_code == 200:
                print(colored(f"   ✅ Agent responded in {duration:.2f}s", "green"))
                print(f"   Summary: {res.json().get('suggestion')[:150]}...")
            else:
                print(colored(f"   ❌ Agent Failed with code {res.status_code}", "red"))
        except Exception as e:
            print(colored(f"   💥 Crash/Timeout: {e}", "red"))

if __name__ == "__main__":
    tester = SynapseBenchmarker()
    print(colored("=== SYNAPSE-FLOW PERFORMANCE AUDIT ===", "white", attrs=['bold']))
    
    # 1. Warm up
    tester.get_system_vitals()
    
    # 2. Run Suites
    tester.test_ghost_speed(iterations=5)
    tester.test_ghost_pressure(concurrent_users=4, total_requests=12)
    tester.test_agent_stress()