import subprocess
import os
import time
import psutil
import sys
from termcolor import colored

# --- CONFIGURATION ---
BASE_DIR = r"D:\Projects\ali_study_assistance\Synapse-Flow\backend_cpp"
BUILD_DIR = os.path.join(BASE_DIR, "build")
RELEASE_DIR = os.path.join(BUILD_DIR, "Release")
EXES = ["agent_service.exe", "code_assistance_server.exe"]

def kill_boosters():
    print(f"🧹 {colored('Scrubbing Pad:', 'yellow')} Killing existing processes...")
    for proc in psutil.process_iter(['name']):
        if proc.info['name'] in EXES:
            try:
                proc.kill()
                print(f"   - Terminated: {proc.info['name']}")
            except: pass
    time.sleep(1)

def build_boosters():
    print(f"🏗️  {colored('Building Engines:', 'cyan')} Executing CMake...")
    try:
        subprocess.check_call(
            ["cmake", "--build", ".", "--config", "Release"],
            cwd=BUILD_DIR
        )
        print(f"   ✅ {colored('Build Successful.', 'green')}")
    except subprocess.CalledProcessError:
        print(f"   ❌ {colored('BUILD FAILED.', 'red')}")
        sys.exit(1)

def launch_boosters():
    print(f"🚀 {colored('Ignition:', 'magenta')} Launching boosters in new windows...")
    
    # Launch REST Server
    subprocess.Popen(
        ["start", "cmd", "/k", "code_assistance_server.exe"],
        shell=True, cwd=RELEASE_DIR
    )
    
    # Launch gRPC Brain (Wait 2s for REST to initialize)
    time.sleep(2)
    subprocess.Popen(
        ["start", "cmd", "/k", "agent_service.exe"],
        shell=True, cwd=RELEASE_DIR
    )
    
    print(f"   ✅ {colored('Boosters Released.', 'green')}")

def run_tests():
    print(f"📡 {colored('Deploying Probe:', 'white')} Running flight_test_suite.py...")
    subprocess.call([sys.executable, "flight_test_suite.py"])

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", action="store_true", help="Run tests after launch")
    parser.add_argument("--no-build", action="store_true", help="Skip the build phase")
    args = parser.parse_args()

    print(colored("\n=== SYNAPSE-FLOW AUTO-LAUNCH SEQUENCE ===\n", attrs=['bold']))
    
    kill_boosters()
    
    if not args.no_build:
        build_boosters()
        
    launch_boosters()
    
    if args.test:
        time.sleep(3) # Wait for AI handshake
        run_tests()
        
    print(f"\n✨ {colored('Ready for Manual Testing.', 'green')} Terminals are open.")