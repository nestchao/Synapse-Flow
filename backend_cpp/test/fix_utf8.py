import os
import stat

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
ae_path = os.path.join(BASE, "src/agent/AgentExecutor.cpp")

def hunt():
    if not os.path.exists(ae_path):
        print("❌ Could not find AgentExecutor.cpp")
        return

    # Force permission
    os.chmod(ae_path, stat.S_IWRITE)
    
    with open(ae_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # We are going to wrap the nlohmann::json::parse call with a diagnostic dump
    diagnostic_code = """
    try {
        return nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::exception& e) {
        std::ofstream bug_file("JSON_DUMP_ERROR.txt");
        bug_file << json_str;
        bug_file.close();
        spdlog::critical("!!! UTF-8 BUG FOUND !!! String dumped to JSON_DUMP_ERROR.txt. Error: {}", e.what());
        return nlohmann::json::object();
    }
    """
    
    # Target the specific catch block in extract_json
    old_code = """try {
        return nlohmann::json::parse(json_str);
    } catch (...) {"""
    
    if old_code in content:
        content = content.replace(old_code, diagnostic_code + " catch (...) {")
        with open(ae_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✅ Diagnostic 'Bug Hunter' injected into AgentExecutor.cpp")
    else:
        print("⚠️ Could not find exact insertion point. Manual edit required.")

hunt()