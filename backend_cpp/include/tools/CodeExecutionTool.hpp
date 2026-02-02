#pragma once
#include "tools/ToolRegistry.hpp"
#include "tools/DockerExecutor.hpp"

namespace code_assistance {

class CodeExecutionTool : public ITool {
    std::shared_ptr<DockerExecutor> executor_;

public:
    CodeExecutionTool() {
        executor_ = std::make_shared<DockerExecutor>();
    }

    ToolMetadata get_metadata() override {
        return {
            "execute_code",
            "Executes Python or JavaScript code in a secure sandbox. Input: {'lang': 'python'|'js', 'code': 'string'}",
            "{\"type\":\"object\",\"properties\":{\"lang\":{\"type\":\"string\"},\"code\":{\"type\":\"string\"}},\"required\":[\"lang\",\"code\"]}"
        };
    }

    std::string execute(const std::string& args_json) override {
        try {
            auto j = nlohmann::json::parse(args_json);
            std::string lang = j.value("lang", "python");
            std::string code = j.value("code", "");

            if (code.empty()) return "ERROR: Code cannot be empty.";

            DockerExecutor::ExecResult res;
            
            if (lang == "python" || lang == "py") {
                res = executor_->execute_python(code);
            } else if (lang == "js" || lang == "javascript" || lang == "node") {
                res = executor_->execute_js(code);
            } else {
                return "ERROR: Unsupported language. Use 'python' or 'js'.";
            }

            std::string status = (res.exit_code == 0) ? "SUCCESS" : "RUNTIME_ERROR";
            return "### EXECUTION RESULT (" + status + ")\n" + res.output;

        } catch (const std::exception& e) {
            return "ERROR: Sandbox Interface Failure: " + std::string(e.what());
        }
    }
};

}