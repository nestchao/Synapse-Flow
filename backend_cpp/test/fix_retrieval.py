import os
import stat

BASE = "D:/Projects/ali_study_assistance/Synapse-Flow/backend_cpp"
re_path = os.path.join(BASE, "src/retrieval_engine.cpp")

def fix_retrieval():
    if not os.path.exists(re_path):
        print("❌ retrieval_engine.cpp not found")
        return

    os.chmod(re_path, stat.S_IWRITE)
    with open(re_path, "r", encoding="utf-8") as f:
        content = f.read()

    # The old flawed logic
    old_logic = """while(ss >> word) {
        if(word.length() > 3) query_keywords.push_back(word);
    }"""
    
    # The new smart logic
    new_logic = """while(ss >> word) {
        // Keep words > 3 chars OR words that contain digits (like '50', 'v2', 'S3')
        bool has_digit = std::any_of(word.begin(), word.end(), ::isdigit);
        if(word.length() > 3 || has_digit) {
            query_keywords.push_back(word);
        }
    }"""

    if "if(word.length() > 3) query_keywords.push_back(word);" in content:
        content = content.replace(old_logic, new_logic)
        with open(re_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✅ C++ Retrieval Engine updated: Now tracks numbers and short IDs.")
    else:
        print("⚠️ Could not find exact string to replace. You may need to edit manually.")

if __name__ == "__main__":
    fix_retrieval()