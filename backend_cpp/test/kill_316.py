import os
import stat

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
ae_path = os.path.join(BASE, "src/agent/AgentExecutor.cpp")

def final_boss_fix():
    if not os.path.exists(ae_path):
        print("❌ AgentExecutor.cpp not found")
        return

    os.chmod(ae_path, stat.S_IWRITE)
    with open(ae_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    print("🛠️  Hardening internal JSON parser...")

    # 1. Fix the return line inside extract_json
    # This is where index 5115 is likely being processed
    old_parse = "return nlohmann::json::parse(json_str);"
    new_parse = "return nlohmann::json::parse(code_assistance::scrub_json_string(json_str));"
    
    if old_parse in content:
        content = content.replace(old_parse, new_parse)
    else:
        # Fallback if the line looks slightly different
        content = content.replace("nlohmann::json::parse(json_str)", "nlohmann::json::parse(code_assistance::scrub_json_string(json_str))")

    # 2. Fix the loop logic where raw_thought is handled
    # We want to scrub it the second it comes back from the AI
    target_loop = "nlohmann::json extracted = extract_json(raw_thought);"
    safe_loop = "nlohmann::json extracted = extract_json(code_assistance::scrub_json_string(raw_thought));"
    content = content.replace(target_loop, safe_loop)

    with open(ae_path, "w", encoding="utf-8") as f:
        f.write(content)
    
    print("✅ Internal parser hardened against UTF-8 corruption.")

if __name__ == "__main__":
    final_boss_fix()