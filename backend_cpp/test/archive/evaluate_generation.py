import requests
import json
import time
import argparse
import sys
import os
import csv
import datetime
from termcolor import colored

# ---------------------------------------------------------
# CONFIGURATION
# ---------------------------------------------------------
DEFAULT_API_URL = "http://127.0.0.1:5002"
REPORT_DIR = os.path.join(os.path.dirname(__file__), "reports")

# The Test Suite
TEST_QUESTIONS = [
    "Explain the high-level architecture of this project.",
    "How does the authentication mechanism work?",
    "List the main API endpoints defined in the code.",
    "What database or storage system is being used?",
    "Identify any potential security risks in the current implementation.",
    "Explain the relationship between the frontend and backend."
]

# Path to the data directory (relative to this script)
DATA_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/Release/data"))

# ---------------------------------------------------------
# UTILITIES
# ---------------------------------------------------------
def find_latest_project():
    if not os.path.exists(DATA_DIR):
        return None
    subdirs = [os.path.join(DATA_DIR, d) for d in os.listdir(DATA_DIR) if os.path.isdir(os.path.join(DATA_DIR, d))]
    if not subdirs: return None
    return os.path.basename(max(subdirs, key=os.path.getmtime))

def ensure_report_dir():
    if not os.path.exists(REPORT_DIR):
        os.makedirs(REPORT_DIR)

def save_to_csv(data, project_id):
    ensure_report_dir()
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"report_{project_id}_{timestamp}.csv"
    filepath = os.path.join(REPORT_DIR, filename)

    keys = data[0].keys()
    
    with open(filepath, 'w', newline='', encoding='utf-8') as output_file:
        dict_writer = csv.DictWriter(output_file, keys)
        dict_writer.writeheader()
        dict_writer.writerows(data)
        
    print(colored(f"\n📊 CSV Report saved to: {filepath}", "green", attrs=['bold']))
    return filepath

# ---------------------------------------------------------
# MAIN EXECUTION
# ---------------------------------------------------------
def run_gen_test(project_id, api_url):
    # 1. Auto Discovery
    if project_id == "AUTO":
        project_id = find_latest_project()
        if not project_id:
            print(colored("❌ No projects found. Sync a project first.", "red"))
            sys.exit(1)

    print(colored(f"\n🚀 STARTING BENCHMARK: {project_id}", "cyan", attrs=['bold']))
    print("="*80)
    print(f"{'#':<3} | {'Latency':<10} | {'Status':<10} | {'Citations':<10} | {'Question'}")
    print("-" * 80)
    
    results = []
    
    for i, q in enumerate(TEST_QUESTIONS):
        start_ts = time.time()
        error_msg = ""
        answer = ""
        status = "FAIL"
        
        try:
            res = requests.post(f"{api_url}/generate-code-suggestion", json={
                "project_id": project_id,
                "prompt": q,
                "use_hyde": False 
            })
            duration = time.time() - start_ts
            
            if res.status_code == 200:
                data = res.json()
                if "error" in data:
                    error_msg = data['error']
                else:
                    answer = data.get('suggestion', '').strip()
                    status = "SUCCESS"
            else:
                error_msg = f"HTTP {res.status_code}"

        except Exception as e:
            duration = time.time() - start_ts
            error_msg = str(e)

        # Analysis Logic
        word_count = len(answer.split())
        has_citations = any(ext in answer for ext in ['.ts', '.js', '.py', '.cpp', '.h', '.dart', 'src/'])
        
        # Console Row Output
        lat_str = f"{duration:.2f}s"
        cit_str = "YES" if has_citations else "NO"
        status_color = "green" if status == "SUCCESS" else "red"
        cit_color = "green" if has_citations else "yellow"
        
        print(f"{i+1:<3} | {lat_str:<10} | {colored(status, status_color):<10} | {colored(cit_str, cit_color):<10} | {q[:40]}...")

        # Structured Data Collection
        results.append({
            "timestamp": datetime.datetime.now().isoformat(),
            "project_id": project_id,
            "question": q,
            "latency_seconds": round(duration, 3),
            "status": status,
            "word_count": word_count,
            "has_citations": has_citations,
            "wps_speed": round(word_count / duration, 2) if duration > 0 else 0,
            "full_answer": answer,
            "error_details": error_msg
        })

    # Export
    if results:
        save_to_csv(results, project_id)

    failed_citations = [r for r in results if not r['has_citations'] and r['status'] == 'SUCCESS']
    if len(failed_citations) > 2:
        print("❌ BUILD FAILED: Too many answers without source code citations.")
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("project_id", type=str, nargs='?', default="AUTO")
    parser.add_argument("--url", type=str, default=DEFAULT_API_URL)
    args = parser.parse_args()
    
    run_gen_test(args.project_id, args.url)