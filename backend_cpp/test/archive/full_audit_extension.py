import requests
import base64
import time
import os
import shutil
import statistics
from termcolor import colored

# --- CONFIGURATION ---
REST_URL = "http://127.0.0.1:5002"
# raw string 'r' to handle Windows paths correctly
DATA_PATH = r"D:\Projects\ali_study_assistance\Synapse-Flow\backend_cpp\build\Release\data"
TARGET_RAW_PATH = r"D:\Projects\SkillTest"

def hard_reset_data():
    """Wipes the entire C++ backend database to ensure zero vector pollution."""
    print(colored("\n☢️  PREPARING NUCLEAR RESET", "red", attrs=['bold']))
    print(colored("!!! PLEASE STOP THE C++ SERVER NOW !!!", "yellow", attrs=['blink']))
    input("Press Enter once the C++ Server window is CLOSED...")
    
    if os.path.exists(DATA_PATH):
        try:
            shutil.rmtree(DATA_PATH)
            print(colored("✅ Data wiped successfully.", "green"))
        except Exception as e:
            print(colored(f"❌ Could not wipe data: {e}", "red"))
            exit(1)
    else:
        print(colored("ℹ️  Data folder already empty.", "white"))
    
    print(colored("\n🚀 START THE C++ SERVER NOW...", "magenta", attrs=['bold']))
    input("Press Enter once the C++ Server is BACK ONLINE...")

class SynapseAccuracyAudit:
    def __init__(self):
        self.session = requests.Session()
        # Normalize path with forward slashes for consistent Base64 ID generation
        norm_path = os.path.abspath(TARGET_RAW_PATH).replace("\\", "/")
        self.project_id = base64.b64encode(norm_path.encode('utf-8')).decode('utf-8')

    def create_valid_test_files(self):
        """Wipes old files (stress_test.py) to remove noise and injects specific code."""
        if os.path.exists(TARGET_RAW_PATH):
            print(colored(f"🧹 Clearing noise from {TARGET_RAW_PATH}...", "yellow"))
            for filename in os.listdir(TARGET_RAW_PATH):
                file_path = os.path.join(TARGET_RAW_PATH, filename)
                try:
                    if os.path.isfile(file_path) or os.path.islink(file_path):
                        os.unlink(file_path)
                    elif os.path.isdir(file_path):
                        shutil.rmtree(file_path)
                except Exception as e:
                    print(f"      ⚠️ Failed to delete {file_path}: {e}")
        else:
            os.makedirs(TARGET_RAW_PATH)
        
        print(colored(f"🔨 Injecting fresh code into {TARGET_RAW_PATH}...", "white"))
        
        # Inject File 1: Fibonacci Logic
        with open(os.path.join(TARGET_RAW_PATH, "math_utils.py"), "w") as f:
            f.write("def calculate_fibonacci(n):\n")
            f.write("    \"\"\"Calculates the nth number in the fibonacci sequence.\"\"\"\n")
            f.write("    if n <= 1: return n\n")
            f.write("    return calculate_fibonacci(n-1) + calculate_fibonacci(n-2)\n")

        # Inject File 2: Database Logic
        with open(os.path.join(TARGET_RAW_PATH, "db_core.py"), "w") as f:
            f.write("class DatabaseManager:\n")
            f.write("    def connect_to_storage(self, connection_url):\n")
            f.write("        \"\"\"Establishes a connection to the database storage.\"\"\"\n")
            f.write("        print(f'Connecting to {connection_url}')\n")
            f.write("        return True\n")
        print(f"      ✅ math_utils.py and db_core.py created.")

    def ensure_project_ready(self):
        """Prepares folder, registers project, and triggers sync."""
        print(colored(f"🛰️  Syncing Project: {TARGET_RAW_PATH}", "white"))
        
        # 🚀 CALL THE FILE CREATION HERE
        self.create_valid_test_files()
        
        # Registration with IGNORES
        self.session.post(f"{REST_URL}/sync/register/{self.project_id}", json={
            "local_path": TARGET_RAW_PATH,
            "allowed_extensions": ["py"],
            "ignored_paths": ["venv", ".study_assistant", "__pycache__", ".git"],
            "included_paths": []
        })
        self.session.post(f"{REST_URL}/sync/run/{self.project_id}", json={})
        print("   🔄 Indexing (Waiting 5s for C++ Brain)...")
        time.sleep(5)
        return True

    def test_rag_accuracy(self):
        """Runs the Mean Reciprocal Rank (MRR) accuracy test."""
        print(colored("\n🔎 PHASE 4: RAG Retrieval Accuracy (Deep Inspection)", "blue", attrs=['bold']))
        
        test_cases = [
            ("Calculate the nth fibonacci number in python", "math_utils"),
            ("Connect to the database storage manager", "db_core"),
        ]

        ranks = []
        hits_at_1 = 0

        for query, target in test_cases:
            try:
                payload = {"project_id": self.project_id, "prompt": query}
                res = self.session.post(f"{REST_URL}/retrieve-context-candidates", json=payload, timeout=15)
                
                if res.status_code != 200:
                    print(f"   ❌ Query Error {res.status_code}")
                    continue

                data = res.json()
                candidates = data.get('candidates', [])
                
                print(f"\n   📡 Query: '{query}'")
                
                found_rank = 0
                for i, cand in enumerate(candidates):
                    path = cand.get('file_path', '').lower()
                    if target.lower() in path:
                        found_rank = i + 1
                        break
                
                if found_rank > 0:
                    score = 1.0 / found_rank
                    ranks.append(score)
                    if found_rank == 1: hits_at_1 += 1
                    color = "green" if found_rank == 1 else "yellow"
                    print(f"      ✅ MATCH FOUND at Rank {colored(found_rank, color)}")
                else:
                    ranks.append(0.0)
                    top_file = candidates[0].get('file_path') if candidates else "None"
                    print(f"      ❌ TARGET '{target}' NOT FOUND (Top result: {top_file})")

            except Exception as e:
                print(f"   - Request Error: {e}")

        if ranks:
            mrr = statistics.mean(ranks)
            accuracy = (hits_at_1 / len(test_cases)) * 100
            print(colored(f"\n📊 Final MRR Score: {mrr:.3f}", "cyan", attrs=['bold']))
            print(colored(f"🎯 Rank-1 Accuracy: {accuracy:.1f}%", "cyan"))

if __name__ == "__main__":
    # 1. Start with a clean database
    hard_reset_data()
    
    # 2. Run Audit
    audit = SynapseAccuracyAudit()
    print(colored("\n=== SYNAPSE-FLOW ACCURACY AUDIT ===", "white", attrs=['bold', 'underline']))
    
    if audit.ensure_project_ready():
        audit.test_rag_accuracy()