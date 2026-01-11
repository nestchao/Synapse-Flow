import os
import json
import base64

TARGET_PATH = "D:/Projects/OOPAssignment"
# Base64 Encode
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

CONFIG_DATA = {
    "local_path": TARGET_PATH,
    "allowed_extensions": ["java", "xml", "json", "txt"],
    "ignored_paths": [".git", "target", ".vscode", "node_modules"],
    "included_paths": []
}

def setup():
    # Adjust path to where your backend_cpp/build/Release/data folder is
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/Release/data"))
    project_dir = os.path.join(base_dir, PROJECT_ID)
    
    if not os.path.exists(project_dir): os.makedirs(project_dir)
    
    with open(os.path.join(project_dir, "config.json"), "w") as f:
        json.dump(CONFIG_DATA, f, indent=4)
    print(f"✅ Config registered for {TARGET_PATH}")

if __name__ == "__main__":
    setup()