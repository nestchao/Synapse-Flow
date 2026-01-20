import os
import json

# 🚀 TARGET CONFIG
# Ensure this matches the project_id in your probe_reasoning.py
TARGET_ROOT = "D:/Projects/SA_ETF"

def setup():
    # 1. Define paths
    config_dir = os.path.join(TARGET_ROOT, "config")
    file_path = os.path.join(config_dir, "missing_settings.json")

    # 2. Create Directory if missing
    if not os.path.exists(config_dir):
        os.makedirs(config_dir)
        print(f"📂 Created directory: {config_dir}")

    # 3. Create JSON Content
    data = {
        "app_name": "Synapse Test",
        "version": "2.1.0",
        "features": {
            "agent": True,
            "reasoning": True
        }
    }

    # 4. Write File
    try:
        with open(file_path, "w") as f:
            json.dump(data, f, indent=4)
        print(f"✅ File Created: {file_path}")
        print("   (Content: Dummy JSON settings)")
    except Exception as e:
        print(f"❌ Error creating file: {e}")

if __name__ == "__main__":
    # Ensure root exists
    if not os.path.exists(TARGET_ROOT):
        os.makedirs(TARGET_ROOT)
        print(f"📂 Created Root: {TARGET_ROOT}")
        
    setup()