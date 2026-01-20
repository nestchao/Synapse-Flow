import grpc
import agent_pb2
import agent_pb2_grpc
import time
import json
import statistics
from termcolor import colored
import os
from pathlib import Path

# 🛰️ DYNAMIC TARGETING
# Point this to your actual Extension Testing Folder
WORKSPACE_TARGET = "D:/Projects/SA_ETF" 

class SynapseTester:
    def __init__(self, target="127.0.0.1:50051"):
        self.channel = grpc.insecure_channel(target)
        self.stub = agent_pb2_grpc.AgentServiceStub(self.channel)
        self.latencies = []

    def run_mission(self, name, prompt):
        print(f"\n🚀 Launching Mission: [{colored(name, 'cyan')}]")
        query = agent_pb2.UserQuery(
            project_id=WORKSPACE_TARGET, 
            prompt=prompt,
            session_id=f"etf_test_{int(time.time())}"
        )
        
        start_time = time.time()
        success = False
        tool_calls = 0

        try:
            responses = self.stub.ExecuteTask(query, timeout=120)
            for res in responses:
                # 🚀 SpaceX Telemetry: Show the payload
                print(f"   📡 {colored(res.phase.upper(), 'magenta')}: {res.payload[:100]}")
                
                if "TOOL_EXEC" in res.phase:
                    tool_calls += 1
                if res.phase == "FINAL" and "timed out" not in res.payload.lower():
                    success = True
            
            if not success:
                print(f"   ⚠️ {colored('WARNING:', 'yellow')} Stream closed without 'final' phase.")
            
            duration = time.time() - start_time
            self.latencies.append(duration)
            return {"status": "PASS" if success else "FAIL", "time": duration, "tools": tool_calls}

        except Exception as e:
            print(f"   💥 {colored('CRITICAL FAILURE:', 'red')} {e}")
            return {"status": "CRASH", "time": 0, "tools": tool_calls}

# --- TEST SCENARIOS ---
tester = SynapseTester()
missions = [
    # ("Web-Oculus Check", "Search the web for 'C++23 std::expected documentation' and summarize the top result."),
    ("Surgical I/O Check", "Read the file 'test03.ts' at the root and tell me the version of nlohmann-json."),
    ("Hybrid Logic Check", "Find the current version of 'httplib' on GitHub and check if it matches our local version.")
]

results = [tester.run_mission(m[0], m[1]) for m in missions]

# Final Telemetry Report
avg_lat = statistics.mean(tester.latencies)
print("\n" + "="*40)
print(f"📊 {colored('FLIGHT READINESS REPORT', 'white', attrs=['bold'])}")
print(f"Avg Latency: {avg_lat:.2f}s")
print(f"Pass Rate: {len([r for r in results if r['status'] == 'PASS'])}/{len(missions)}")
print("="*40)