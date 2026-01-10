import os
import json
import base64

# 🚀 CONFIGURATION
TARGET_PATH = "D:/Projects/SA_ETF"
# Explicitly encode to match what VS Code sends
PROJECT_ID = base64.b64encode(TARGET_PATH.encode('utf-8')).decode('utf-8')

# 🚀 DEFINING THE RULES
CONFIG_DATA = {
    "local_path": TARGET_PATH,
    "allowed_extensions": ["txt", "html", "ts", "json", "py", "cpp"],
    "ignored_paths": [
        "ignore01",           # <--- BLOCK THIS
        "test01/ignore01",
        "test02",
        "node_modules",
        ".git"
    ],
    "included_paths": [
        "test02/exception01"  # <--- ALLOW THIS
    ]
}

def setup_config():
    # 1. Determine where the server looks for data
    # Assuming running from 'test' folder, we go up to 'build/Release/data'
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/Release/data"))
    project_dir = os.path.join(base_dir, PROJECT_ID)
    
    if not os.path.exists(project_dir):
        os.makedirs(project_dir)
        print(f"📂 Created Data Dir: {project_dir}")

    config_path = os.path.join(project_dir, "config.json")

    # 2. Write the JSON
    with open(config_path, "w") as f:
        json.dump(CONFIG_DATA, f, indent=4)
    
    print(f"✅ Configuration Saved: {config_path}")
    print(f"🔒 Ignore Rules Active: {CONFIG_DATA['ignored_paths']}")

if __name__ == "__main__":
    setup_config()