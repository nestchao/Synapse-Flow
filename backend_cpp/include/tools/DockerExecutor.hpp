#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "utils/SubProcess.hpp"

namespace code_assistance {

namespace fs = std::filesystem;

class DockerExecutor {
public:
    struct ExecResult {
        std::string output;
        int exit_code;
        bool timeout;
    };

    DockerExecutor() {
        // Simple check if docker is alive
        auto res = SubProcess::run("docker --version");
        if (!res.success) {
            spdlog::warn("⚠️ Docker not detected. Code Execution tool will fail.");
        }
    }

    ExecResult execute_python(const std::string& code, int timeout_sec = 5) {
        return execute_in_container("python:3.11-slim", "python", code, timeout_sec);
    }

    ExecResult execute_js(const std::string& code, int timeout_sec = 5) {
        return execute_in_container("node:18-alpine", "node", code, timeout_sec);
    }

private:
    ExecResult execute_in_container(const std::string& image, const std::string& interpreter, 
                                   const std::string& code, int timeout_sec) {
        
        // 1. Create a temporary file on HOST to mount
        // (Safer than passing code via CLI arguments which have length limits)
        std::string temp_name = "exec_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".tmp";
        fs::path temp_path = fs::temp_directory_path() / temp_name;
        
        std::ofstream out(temp_path);
        out << code;
        out.close();

        // 2. Build Docker Command
        // --rm: Delete container after run
        // --network none: No internet access (Security)
        // --memory 128m: Resource limit
        // -v: Mount the temp file as /code.script
        std::stringstream cmd;
        cmd << "docker run --rm --network none --memory 128m --cpus 0.5 "
            << "-v " << temp_path.string() << ":/code.script "
            << image << " " << interpreter << " /code.script";

        spdlog::info("🐳 Sandbox: Executing code via {}", image);
        
        // 3. Execute
        // Note: Simple popen implementation blocks. 
        // In Phase 7.1 we can add true async timeout killing.
        auto res = SubProcess::run(cmd.str());

        // 4. Cleanup
        try { fs::remove(temp_path); } catch(...) {}

        return { res.output, res.exit_code, false };
    }
};

}