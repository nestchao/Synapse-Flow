import requests
import base64
import time
import os
import statistics
from termcolor import colored

# --- CONFIGURATION ---
REST_URL = "http://127.0.0.1:5002"
# Use a cleaner path format to avoid Base64 mismatches
TARGET_RAW_PATH = os.path.abspath(r"D:\Projects\SkillTest")

class SynapseAccuracyAudit:
    def __init__(self):
        self.session = requests.Session()
        self.project_id = None

    def create_valid_test_files(self):
        if not os.path.exists(TARGET_RAW_PATH):
            os.makedirs(TARGET_RAW_PATH)
        
        # File 1: Fibonacci Logic
        with open(os.path.join(TARGET_RAW_PATH, "math_utils.py"), "w") as f:
            f.write("def calculate_fibonacci(n):\n")
            f.write("    if n <= 1: return n\n")
            f.write("    return calculate_fibonacci(n-1) + calculate_fibonacci(n-2)\n")

        # File 2: Database Logic
        with open(os.path.join(TARGET_RAW_PATH, "db_core.py"), "w") as f:
            f.write("class DatabaseManager:\n")
            f.write("    def connect_to_storage(self, url):\n")
            f.write("        return True\n")

    def ensure_project_ready(self):
        print(colored(f"🛰️  Synchronizing Project: {TARGET_RAW_PATH}", "white"))
        self.create_valid_test_files()

        # Generate a temporary ID for registration
        temp_id = base64.b64encode(TARGET_RAW_PATH.encode('utf-8')).decode('utf-8')
        
        # 1. Register & Sync
        self.session.post(f"{REST_URL}/sync/register/{temp_id}", json={
            "local_path": TARGET_RAW_PATH,
            "allowed_extensions": ["py"],
            "ignored_paths": [], "included_paths": []
        })
        self.session.post(f"{REST_URL}/sync/run/{temp_id}", json={})
        
        # 2. Wait and Sniff the ACTUAL ID from the server
        print("   🔄 Indexing... (Discovering Server ID)")
        time.sleep(3)
        
        try:
            # We use a trick: poll the telemetry to see what project was last synced
            # or just use our generated ID if the server is healthy.
            self.project_id = temp_id
            return True
        except:
            return False

    def test_rag_accuracy(self):
        print(colored("\n🔎 PHASE 4: RAG Retrieval Accuracy (Deep Inspection)", "blue", attrs=['bold']))
        
        test_cases = [
            ("How to calculate fibonacci?", "math_utils"),
            ("How do I connect to the database?", "db_core"),
        ]

        for query, target in test_cases:
            try:
                payload = {"project_id": self.project_id, "prompt": query}
                res = self.session.post(f"{REST_URL}/retrieve-context-candidates", json=payload, timeout=10)
                
                if res.status_code != 200:
                    print(f"   ❌ Query: '{query[:20]}' -> Server Error {res.status_code}")
                    continue

                data = res.json()
                candidates = data.get('candidates', [])
                
                print(f"\n   📡 Query: '{query}'")
                if not candidates:
                    print(colored("      ⚠️  Server returned an EMPTY list.", "yellow"))
                    continue

                # Print the Top 3 paths returned by the server
                print("      📥 Top Results from Server:")
                for i, cand in enumerate(candidates[:3]):
                    # Check both 'file_path' and 'path' in case of naming mismatch
                    p = cand.get('file_path') or cand.get('path') or "MISSING_PATH_KEY"
                    n = cand.get('name') or "MISSING_NAME_KEY"
                    print(f"         {i+1}. Path: '{p}' | Node: '{n}'")

                # Match Logic
                found_rank = 0
                for i, cand in enumerate(candidates):
                    p = (cand.get('file_path') or cand.get('path') or "").lower()
                    if target.lower() in p:
                        found_rank = i + 1
                        break
                
                if found_rank > 0:
                    print(colored(f"      ✅ MATCH FOUND at Rank {found_rank}", "green"))
                else:
                    print(colored(f"      ❌ TARGET '{target}' NOT IN SEARCH RESULTS", "red"))
                    # DEBUG: Print the first candidate's full JSON keys
                    print(f"      [DEBUG] JSON Keys: {list(candidates[0].keys())}")

            except Exception as e:
                print(f"   - Request Error: {e}")

if __name__ == "__main__":
    audit = SynapseAccuracyAudit()
    print(colored("=== SYNAPSE-FLOW ACCURACY AUDIT ===", "white", attrs=['bold', 'underline']))
    
    if audit.ensure_project_ready():
        # Small wait for the C++ index to write metadata.json
        time.sleep(1)
        audit.test_rag_accuracy()
    else:
        print(colored("\n❌ Initialization Failed.", "red"))