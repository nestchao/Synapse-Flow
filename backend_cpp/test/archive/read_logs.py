import json
import glob
import os
import sys  

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')

# Path to where your C++ server saves logs
LOG_DIR = "logs"

def print_latest_log():
    # Check if directory exists
    if not os.path.exists(LOG_DIR):
        print(f"Log directory not found at: {os.path.abspath(LOG_DIR)}")
        print("   Make sure you have run the server and generated a request.")
        return

    # Find all json files
    files = glob.glob(os.path.join(LOG_DIR, "*.json"))
    if not files:
        print(f"No log files found in {LOG_DIR}.")
        return

    # Get the most recent file
    latest_file = max(files, key=os.path.getctime)
    
    print(f"Reading: {latest_file}\n")
    
    with open(latest_file, 'r', encoding='utf-8') as f:
        data = json.load(f)
        
        print("="*60)
        print(f"PROJECT: {data.get('project_id')}")
        print("-" * 60)
        
        print("PROMPT:")
        print(data['inputs']['user_prompt'])
        print("-" * 60)
        
        print("REPLY:")
        print(data['outputs']['ai_reply'])
        print("-" * 60)
        
        # Handle Vector
        vec = data.get('vector_embedding', [])
        print(f"VECTOR ({len(vec)} dimensions):")
        # Print first 10 values
        print(vec[:10], "...")
        print("="*60)

if __name__ == "__main__":
    print_latest_log()