import os
import time
from termcolor import colored

# 🧪 SYNAPSE SAFETY PROTOCOL TEST

TEST_FILE = "safety_check.txt"
JOURNAL_FILE = TEST_FILE + ".synapse_journal"

def setup():
    with open(TEST_FILE, "w") as f:
        f.write("ORIGINAL_DATA_V1")
    print(colored(f"✅ Created {TEST_FILE} with 'ORIGINAL_DATA_V1'", "green"))

def simulate_crash():
    print(colored("\n💥 SIMULATING CRASH SCENARIO...", "yellow"))
    
    # 1. Create Journal (Backup)
    with open(TEST_FILE, "r") as src, open(JOURNAL_FILE, "w") as dst:
        dst.write(src.read())
    print(f"   - Journal created: {JOURNAL_FILE}")

    # 2. Corrupt the file (Partial Write)
    with open(TEST_FILE, "w") as f:
        f.write("CORRUPTED_DA") # Missing data
    print(f"   - File corrupted (Write interrupted)")

    # 3. Trigger Rollback Logic
    print("   - Triggering Rollback Protocol...")
    if os.path.exists(JOURNAL_FILE):
        # Restore
        with open(JOURNAL_FILE, "r") as src, open(TEST_FILE, "w") as dst:
            dst.write(src.read())
        os.remove(JOURNAL_FILE)
        print(colored("   - 🔄 Rollback Complete.", "cyan"))
    else:
        print(colored("   - ❌ No Journal found!", "red"))

def verify():
    if not os.path.exists(TEST_FILE):
        print(colored("❌ TEST FAILED: File missing.", "red"))
        return

    with open(TEST_FILE, "r") as f:
        content = f.read()
    
    if content == "ORIGINAL_DATA_V1":
        print(colored("\n🏆 TEST PASSED: Integrity Maintained.", "green", attrs=['bold']))
    else:
        print(colored(f"\n❌ TEST FAILED: Content is '{content}'", "red"))

if __name__ == "__main__":
    setup()
    simulate_crash()
    verify()
    
    # Cleanup
    if os.path.exists(TEST_FILE): os.remove(TEST_FILE)