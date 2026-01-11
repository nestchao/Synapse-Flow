#pragma once
#include "tools/ToolRegistry.hpp"
#include "tools/FileSystemTools.hpp"
#include <fstream>
#include <string>
#include <array>
#include <cstdio>
#include <random>

namespace code_assistance {

class CodeExecutionTool : public ITool {
public:
    ToolMetadata get_metadata() override {
        return {
            "execute_code",
            "Executes Python code in a sandbox environment. Use this to test logic, debug errors, or perform calculations. Returns stdout and stderr.",
            "{\"type\":\"object\",\"properties\":{\"code\":{\"type\":\"string\"},\"language\":{\"type\":\"string\", \"enum\":[\"python\"]}},\"required\":[\"code\"]}"
        };
    }

    std::string execute(const std::string& args_json) override {
        try {
            auto j = nlohmann::json::parse(args_json);
            std::string code = j.value("code", "");
            std::string lang = j.value("language", "python");

            if (lang != "python") {
                return "ERROR: Only 'python' is currently supported for execution security.";
            }
            if (code.empty()) return "ERROR: No code provided.";

            // 1. Create Unique Temp File
            long long timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            std::string filename = "temp_exec_" + std::to_string(timestamp) + "_" + std::to_string(rand() % 9999) + ".py";
            
            // Write code to file
            std::ofstream out(filename);
            out << code;
            out.close();

            // 2. Prepare Command (Capture Stderr into Stdout)
            std::string cmd = "python " + filename + " 2>&1"; 

            // 3. Execute and Capture Output
            std::string result;
            std::array<char, 128> buffer;
            
            #ifdef _WIN32
                FILE* pipe = _popen(cmd.c_str(), "r");
            #else
                FILE* pipe = popen(cmd.c_str(), "r");
            #endif

            if (!pipe) {
                std::filesystem::remove(filename);
                return "ERROR: Failed to open execution pipe (Is Python installed?).";
            }

            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }

            #ifdef _WIN32
                int return_code = _pclose(pipe);
            #else
                int return_code = pclose(pipe);
            #endif

            // 4. Cleanup
            std::filesystem::remove(filename);

            // 5. Format Result
            if (result.empty()) result = "(No output)";
            
            if (return_code != 0) {
                return "❌ EXECUTION FAILED (Exit Code " + std::to_string(return_code) + "):\n" + result;
            }
            
            return "✅ EXECUTION OUTPUT:\n" + result;

        } catch (const std::exception& e) {
            return "ERROR: Execution Tool Exception: " + std::string(e.what());
        }
    }
};

}