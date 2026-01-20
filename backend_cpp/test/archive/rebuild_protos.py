import os
import subprocess
import sys
from pathlib import Path

def rebuild():
    # 1. Dynamic Path Resolution (SpaceX Standard)
    # This script is at Synapse-Flow/backend_cpp/test/rebuild_protos.py
    # The proto is at   Synapse-Flow/backend_cpp/include/proto/agent.proto
    script_dir = Path(__file__).parent.absolute()
    project_root = script_dir.parent # This is backend_cpp
    
    # Define search paths for agent.proto
    proto_candidates = [
        project_root / "include" / "proto" / "agent.proto",
        project_root / "proto" / "agent.proto",
        script_dir.parent.parent / "backend_cpp" / "include" / "proto" / "agent.proto"
    ]

    target_proto = None
    for candidate in proto_candidates:
        if candidate.exists():
            target_proto = candidate
            break

    if not target_proto:
        print("❌ CRITICAL: agent.proto not found in expected locations:")
        for c in proto_candidates: print(f"   - {c}")
        sys.exit(1)

    print(f"🛰️  Targeting Proto: {target_proto}")
    
    # 2. Prepare Command
    # -I defines the "Import root". Protoc is picky about paths being inside the -I root.
    proto_dir = target_proto.parent
    
    cmd = [
        sys.executable, "-m", "grpc_tools.protoc",
        f"-I{proto_dir}",
        f"--python_out={script_dir}",
        f"--grpc_python_out={script_dir}",
        str(target_proto)
    ]

    # 3. Execution
    try:
        subprocess.check_call(cmd)
        print("✅  Stubs Rebuilt Successfully in 'test' directory.")
        print("   -> agent_pb2.py")
        print("   -> agent_pb2_grpc.py")
    except subprocess.CalledProcessError as e:
        print(f"💥 Protoc failed with exit code {e.returncode}")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    rebuild()