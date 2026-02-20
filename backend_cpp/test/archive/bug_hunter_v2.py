import os
import stat

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
main_path = os.path.join(BASE, "src/main.cpp")
ae_path = os.path.join(BASE, "src/agent/AgentExecutor.cpp")

def fix_code():
    # --- 1. Fix AgentExecutor.cpp ---
    if os.path.exists(ae_path):
        os.chmod(ae_path, stat.S_IWRITE)
        with open(ae_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        
        # Clean up the malformed '\n' from the previous failed run
        content = content.replace('\\n        last_gen.text = scrub_json_string(last_gen.text);', '')
        content = content.replace('\n        last_gen.text = scrub_json_string(last_gen.text);', '')
        
        # Apply the clean fix
        target = "last_gen = ai_service_->generate_text_elite(prompt_template);"
        replacement = target + "\n        last_gen.text = scrub_json_string(last_gen.text);"
        
        if replacement not in content:
            content = content.replace(target, replacement)
        
        with open(ae_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✅ AgentExecutor.cpp: Fixed syntax errors.")

    # --- 2. Fix main.cpp ---
    if os.path.exists(main_path):
        os.chmod(main_path, stat.S_IWRITE)
        with open(main_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        # Rename 'f' to 'crash_file' to avoid the "redefinition" error
        # and clean up previous failed injections
        bad_block_start = 'std::ofstream f("DEBUG_CRASH_DUMP.txt");'
        if bad_block_start in content:
            content = content.replace('std::ofstream f("DEBUG_CRASH_DUMP.txt");', 'std::ofstream crash_file("DEBUG_CRASH_DUMP.txt");')
            content = content.replace('f << "ERROR: "', 'crash_file << "ERROR: "')
            content = content.replace('f << "BODY RECEIVED:\\n" << req.body;', 'crash_file << "BODY RECEIVED:\\n" << req.body;')
            content = content.replace('f.close();', 'crash_file.close();')
        
        with open(main_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✅ main.cpp: Fixed redefinition errors.")

if __name__ == "__main__":
    fix_code()