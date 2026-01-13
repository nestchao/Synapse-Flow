#pragma once
#include <string>
#include <array>
#include <memory>
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace code_assistance {

struct ProcessResult {
    std::string output;
    int exit_code;
    bool success;
};

class SubProcess {
public:
    static ProcessResult run(const std::string& cmd) {
        std::array<char, 128> buffer;
        std::string result;
        
        // Redirect stderr to stdout to capture everything
        std::string full_cmd = cmd + " 2>&1";

        FILE* pipe = POPEN(full_cmd.c_str(), "r");
        if (!pipe) throw std::runtime_error("popen() failed!");

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }

        int rc = PCLOSE(pipe);
        
        return { result, rc, rc == 0 };
    }
};

}