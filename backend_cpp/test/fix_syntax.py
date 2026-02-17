import os
import stat

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
main_path = os.path.join(BASE, "src/main.cpp")

def fix_main():
    if not os.path.exists(main_path):
        print("❌ Could not find main.cpp")
        return

    os.chmod(main_path, stat.S_IWRITE)
    with open(main_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    new_lines = []
    found_scrubber_include = False
    
    # Check for include and fix the code
    for line in lines:
        if 'utils/Scrubber.hpp' in line:
            found_scrubber_include = True
        
        # Fix the specific broken loading logic from the previous attempt
        if "ss << scrub_json_string(raw_content);" in line:
            line = line.replace("ss << scrub_json_string(raw_content);", 
                                "ss << code_assistance::scrub_json_string(raw_content);")
        
        # If the include wasn't handled correctly by namespace
        if "scrub_json_string(" in line and "code_assistance::" not in line:
            line = line.replace("scrub_json_string(", "code_assistance::scrub_json_string(")

        new_lines.append(line)

    # 1. Ensure the include is at the very top if missing
    if not found_scrubber_include:
        new_lines.insert(0, '#include "utils/Scrubber.hpp"\n')

    with open(main_path, "w", encoding="utf-8") as f:
        f.writelines(new_lines)
    
    print("✅ main.cpp: Namespace and Include fixed.")

if __name__ == "__main__":
    fix_main()