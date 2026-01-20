import requests
import json
import sys
import os
import argparse
from termcolor import colored

DEFAULT_API_URL = "http://127.0.0.1:5002"
DATA_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/Release/data"))

def find_latest_project():
    if not os.path.exists(DATA_DIR): return None
    subdirs = [os.path.join(DATA_DIR, d) for d in os.listdir(DATA_DIR) if os.path.isdir(os.path.join(DATA_DIR, d))]
    if not subdirs: return None
    return os.path.basename(max(subdirs, key=os.path.getmtime))

def check_recall(candidates, expected_list):
    if not candidates: return 0.0
    found = 0
    # Normalize paths (lower case, forward slashes)
    file_paths = [c['file_path'].lower().replace('\\', '/') for c in candidates]
    
    for exp in expected_list:
        exp_clean = exp.lower()
        # Loose match: Is the expected filename inside the retrieved path?
        if any(exp_clean in fp for fp in file_paths):
            found += 1
            
    return found / len(expected_list) if expected_list else 1.0

def run_benchmark(project_id, api_url):
    if project_id == "AUTO":
        project_id = find_latest_project()
        if not project_id:
            print(colored("❌ No projects found. Sync a project first.", "red"))
            sys.exit(1)

    print(colored(f"📉 RUNNING REGRESSION BENCHMARK: {project_id}", "cyan", attrs=['bold']))
    
    # Load Golden Data
    try:
        with open('golden_data.json', 'r') as f:
            BENCHMARK = json.load(f)
    except FileNotFoundError:
        print(colored("❌ 'golden_data.json' not found in test directory.", "red"))
        sys.exit(1)

    total_score = 0
    
    for case in BENCHMARK:
        q = case['question']
        print(f"Testing: {q[:40]}...", end="")
        
        try:
            res = requests.post(f"{api_url}/retrieve-context-candidates", json={
                "project_id": project_id, "prompt": q
            })
            candidates = res.json().get('candidates', [])
            
            recall = check_recall(candidates, case['expected_files'])
            total_score += recall
            
            if recall == 1.0:
                print(colored(" PASS", "green"))
            else:
                print(colored(f" FAIL ({recall*100:.0f}%)", "red"))
                print(f"   Wanted: {case['expected_files']}")
                # Print only filenames to keep output clean
                top_files = [os.path.basename(c['file_path']) for c in candidates[:3]]
                print(f"   Got Top 3: {top_files}")
        except Exception as e:
            print(colored(f" ERROR: {e}", "red"))

    if not BENCHMARK:
        print("Empty benchmark file.")
        return

    avg = total_score / len(BENCHMARK)
    print("\n" + "="*40)
    print(f"🏆 Final Recall Score: {avg:.2f}")
    if avg < 0.8:
        print(colored("❌ BUILD FAILED: Accuracy below 80%", "red"))
        # In a real CI pipeline, uncomment the next line:
        # sys.exit(1) 
    else:
        print(colored("✅ BUILD PASSED", "green"))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("project_id", type=str, nargs='?', default="AUTO")
    parser.add_argument("--url", type=str, default=DEFAULT_API_URL)
    args = parser.parse_args()
    
    run_benchmark(args.project_id, args.url)