#pragma once
#include "tools/ToolRegistry.hpp"
#include "tools/FileSystemTools.hpp"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace code_assistance {

class ShellExecutionTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {
            "run_command",
            "Executes a shell command in the project root. Use this to compile code, run tests (mvn test), or check build status. Returns stdout/stderr.",
            "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"},\"project_id\":{\"type\":\"string\"}},\"required\":[\"command\",\"project_id\"]}"
        };
    }

    std::string execute(const std::string& args_json) override {
        try {
            auto j = nlohmann::json::parse(args_json);
            std::string cmd = j.value("command", "");
            std::string project_id = j.value("project_id", "");

            if (cmd.empty()) return "ERROR: No command provided.";

            // 1. Resolve Root Path
            std::string root_str = FileSystemTools::resolve_project_root(project_id);
            if (root_str.empty()) return "ERROR: Invalid Project ID.";

            // 2. Safety Filter (Prevent dangerous commands)
            // Allow only build/test related tools
            std::string safe_cmd = cmd;
            std::transform(safe_cmd.begin(), safe_cmd.end(), safe_cmd.begin(), ::tolower);
            
            bool is_safe = (safe_cmd.find("mvn") == 0 || 
                            safe_cmd.find("javac") == 0 || 
                            safe_cmd.find("java") == 0 || 
                            safe_cmd.find("gradle") == 0 ||
                            safe_cmd.find("python") == 0 ||  
                            safe_cmd.find("python3") == 0 ||
                            safe_cmd.find("pip") == 0 ||     
                            safe_cmd.find("dir") == 0 || 
                            safe_cmd.find("ls") == 0);

            if (!is_safe) {
                return "ERROR: Security Block. Only 'mvn', 'java', 'python', 'pip', 'gradle' commands are allowed.";
            }

            // 3. Construct Command (CD into root first)
            // Windows: "cd /d D:\Path && cmd"
            std::string full_cmd = "cd /d \"" + root_str + "\" && " + cmd + " 2>&1";

            // 4. Execute
            std::array<char, 128> buffer;
            std::string result;
            
            #ifdef _WIN32
                FILE* pipe = _popen(full_cmd.c_str(), "r");
            #else
                FILE* pipe = popen(full_cmd.c_str(), "r");
            #endif

            if (!pipe) return "ERROR: popen() failed!";

            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
                // Safety limit for output size
                if (result.size() > 8000) {
                    result += "\n... [Output Truncated]";
                    break;
                }
            }

            #ifdef _WIN32
                int rc = _pclose(pipe);
            #else
                int rc = pclose(pipe);
            #endif

            return "Exit Code: " + std::to_string(rc) + "\nOUTPUT:\n" + result;

        } catch (const std::exception& e) {
            return "ERROR: Shell Tool Exception: " + std::string(e.what());
        }
    }
};

}