import requests
import json

# This bypasses the AI and tests the C++ Tool Registry directly
URL = "http://127.0.0.1:5002/sync/file/SYNAPSE_INTERNAL_TEST"
payload = {"file_path": "."}

print("🛰️ Probing C++ list_dir implementation...")
try:
    # We use the REST port (5002) to check tool health
    res = requests.post(URL, json=payload)
    print(f"Status: {res.status_code}")
    print(f"Payload: {res.json()}")
except Exception as e:
    print(f"❌ Probe Failed: {e}")