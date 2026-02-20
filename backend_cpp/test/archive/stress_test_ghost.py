import requests
import base64
import time
import statistics

API_URL = "http://127.0.0.1:5002"
TARGET_DIR = r"D:\Projects\SkillTest" 
PROJECT_ID = base64.b64encode(TARGET_DIR.encode('utf-8')).decode('utf-8')

def measure_ghost_latency(iterations=10):
    latencies = []
    print(f"👻 Measuring Ghost Text Latency ({iterations} iters)...")
    
    payload = {
        "prefix": "def calculate_fibonacci(n):\n    if n <= 1: return n\n    return ",
        "suffix": "\n",
        "project_id": PROJECT_ID,
        "file_path": "main.py"
    }

    for i in range(iterations):
        start = time.time()
        try:
            res = requests.post(f"{API_URL}/complete", json=payload, timeout=10)
            lat = (time.time() - start) * 1000 # to ms
            
            if res.status_code == 200:
                latencies.append(lat)
                print(f"   Iter {i+1}: {lat:.0f}ms")
            else:
                print(f"   Iter {i+1}: ERROR {res.status_code}")
        except Exception as e:
            print(f"   Iter {i+1}: TIMEOUT/ERROR - {e}")

    if latencies:
        avg = statistics.mean(latencies)
        
        # Manual P95 calculation (Works on all Python versions)
        latencies.sort()
        index = int(len(latencies) * 0.95)
        # Clamp index
        index = min(index, len(latencies) - 1)
        p95 = latencies[index]

        print(f"\n📊 RESULTS:")
        print(f"   Avg: {avg:.0f}ms")
        print(f"   P95: {p95:.0f}ms")
        
        if avg > 1000:
            print("\n⚠️  PERFORMANCE WARNING: Latency > 1000ms.")
            print("    Ghost Text requires < 600ms to feel usable.")
            print("    Cause: Browser Bridge overhead or large context retrieval.")

def single_request():
    payload = {
        "prefix": "def calculate_pi(n):\n    ",
        "suffix": "\n",
        "project_id": "TEST_PROJ",
        "file_path": "math_utils.py"
    }
    start = time.time()
    try:
        res = requests.post(API_URL, json=payload, timeout=5)
        return (time.time() - start) * 1000
    except:
        return None

def run_pressure_test(concurrent_users):
    with ThreadPoolExecutor(max_workers=concurrent_users) as executor:
        results = list(executor.map(lambda _: single_request(), range(100)))
    
    valid_results = [r for r in results if r is not None]
    print(f"Concurrent Users: {concurrent_users}")
    print(f"Average Latency: {sum(valid_results)/len(valid_results):.2f}ms")
    print(f"Success Rate: {len(valid_results)}%")
    
if __name__ == "__main__":
    measure_ghost_latency()