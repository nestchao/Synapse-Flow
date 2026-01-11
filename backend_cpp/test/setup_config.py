import os
import json
import base64

# 🚀 TARGET CONFIG: SWITCH TO OOP ASSIGNMENT
TARGET_PATH = "D:/Projects/OOPAssignment"
# Base64 Encode the ID
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

CONFIG_DATA = {
    "local_path": TARGET_PATH,
    # Ensure 'java' is included
    "allowed_extensions": ["java", "json", "xml", "txt", "md"],
    "ignored_paths": [
        "node_modules", ".git", "bin", "obj", ".vscode", ".idea"
    ],
    "included_paths": [] 
}

def setup_config():
    # Locate the server's data directory (relative to this script in 'test')
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/Release/data"))
    project_dir = os.path.join(base_dir, PROJECT_ID)
    
    if not os.path.exists(project_dir):
        os.makedirs(project_dir)
        print(f"📂 Created Data Dir: {project_dir}")

    config_path = os.path.join(project_dir, "config.json")

    with open(config_path, "w") as f:
        json.dump(CONFIG_DATA, f, indent=4)
    
    print(f"✅ Configuration Saved for: {TARGET_PATH}")
    print(f"🆔 Project ID: {PROJECT_ID}")

if __name__ == "__main__":
    setup_config()