import requests
import json
import base64
import os
import time
from termcolor import colored

# 🚀 CONFIGURATION
API_URL = "http://127.0.0.1:5002"
PROJECT_ROOT = "D:/Projects/OOPAssignment"
PROJECT_ID = base64.b64encode(PROJECT_ROOT.encode('utf-8')).decode('utf-8')

def interactive_session():
    print(colored(f"\n🤖 SYNAPSE-FLOW INTERACTIVE CONSOLE", "cyan", attrs=['bold']))
    print(f"Target: {PROJECT_ROOT}")
    print("Type 'exit' to quit.\n")

    # Generate a session ID once for the whole conversation
    session_id = f"USER_SESSION_{int(time.time())}"

    while True:
        user_input = input(colored("\nYou: ", "green", attrs=['bold'])).strip()
        
        if user_input.lower() in ['exit', 'quit']:
            break
        if not user_input:
            continue

        print(colored("Thinking...", "yellow"), end="", flush=True)

        payload = {
            "project_id": PROJECT_ID,
            "prompt": user_input,
            "session_id": session_id # Persists context across turns
        }

        try:
            # Note: We use a long timeout because the agent might run tests/edits
            res = requests.post(f"{API_URL}/generate-code-suggestion", json=payload, timeout=180)
            
            if res.status_code == 200:
                data = res.json()
                print("\r" + " " * 20 + "\r", end="") # Clear "Thinking..."
                print(colored("Agent:", "magenta", attrs=['bold']))
                print(data.get("suggestion", ""))
            else:
                print(f"\n❌ Server Error: {res.status_code}")

        except Exception as e:
            print(f"\n❌ Connection Error: {e}")

if __name__ == "__main__":
    interactive_session()