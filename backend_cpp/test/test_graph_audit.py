import requests
import base64
from termcolor import colored

REST_URL = "http://127.0.0.1:5002"
TARGET_PATH = "D:/Projects/SkillTest"
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

def audit_neural_network():
    print(colored("🔎 AUDITING NEURAL GRAPH DATA...", "magenta", attrs=["bold"]))
    
    endpoint = f"{REST_URL}/api/admin/graph/{PROJECT_ID}"
    try:
        res = requests.get(endpoint, timeout=10)
        if res.status_code == 200:
            nodes = res.json()
            node_count = len(nodes)
            
            print(f"📊 Total Nodes in Memory: {colored(node_count, 'yellow')}")
            
            # Check for specific node types
            types = [n.get('type') for n in nodes]
            print(f"   - Tool Calls: {types.count('TOOL_CALL')}")
            print(f"   - AI Thoughts: {types.count('SYSTEM_THOUGHT')}")
            print(f"   - User Prompts: {types.count('PROMPT')}")

            if node_count > 200:
                print(colored("\n✅ GRAPH INTEGRITY VERIFIED: Brain is retaining memory.", "green"))
            else:
                print(colored("\n⚠️ WARNING: Low node count. Memory might be volatile.", "red"))
        else:
            print(colored(f"❌ API Error: {res.status_code}", "red"))
    except Exception as e:
        print(colored(f"💥 Connection Failed: {e}", "red"))

if __name__ == "__main__":
    audit_neural_network()