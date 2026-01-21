#pragma once
#include <deque>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem> 
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace code_assistance {

struct AgentTrace {
    std::string session_id;
    std::string timestamp; // Not strictly used in JSON output logic below but good to have
    std::string state;
    std::string detail;
    double duration_ms;
};

struct InteractionLog {
    long long timestamp;
    std::string project_id;
    std::string request_type; // "AGENT" or "GHOST"
    std::string user_query;   
    std::string full_prompt;  // 🚀 What AI saw
    std::string ai_response;
    std::vector<float> vector_snapshot; // 🚀 DNA
    double duration_ms;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

class LogManager {
public:
    // Singleton access
    static LogManager& instance() {
        static LogManager instance;
        return instance;
    }

    void add_log(const InteractionLog& log) {
        std::lock_guard<std::mutex> lock(mtx_);
        logs_.push_back(log);
        if (logs_.size() > 50) logs_.pop_front();
        save_logs_to_disk(); // 🚀 Auto-save on new log
    }

    json get_logs_json() {
        std::lock_guard<std::mutex> lock(mtx_);
        json j_list = json::array();
        for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
            j_list.push_back({
                {"timestamp", (long long)it->timestamp},
                {"project_id", it->project_id},
                {"request_type", it->request_type},
                {"user_query", it->user_query},
                {"full_prompt", it->full_prompt},
                {"ai_response", it->ai_response},
                {"vector_snapshot", it->vector_snapshot},
                {"duration_ms", it->duration_ms},
                {"total_tokens", it->total_tokens},
                {"prompt_tokens", it->prompt_tokens},
                {"completion_tokens", it->completion_tokens}
            });
        }
        return j_list;
    }

    void add_trace(const AgentTrace& trace) {
        std::lock_guard<std::mutex> lock(mtx_);
        agent_traces_.push_back(trace);
        if (agent_traces_.size() > 100) agent_traces_.pop_front();
    }

    json get_traces_json() {
        std::lock_guard<std::mutex> lock(mtx_);
        json j = json::array();
        for (const auto& t : agent_traces_) {
            j.push_back({
                {"session_id", t.session_id},
                {"state", t.state},
                {"detail", t.detail},
                {"duration", t.duration_ms}
            });
        }
        return j;
    }

private:
    LogManager() {
        load_logs_from_disk(); // 🚀 Load on startup
    }

    std::deque<InteractionLog> logs_;
    std::deque<AgentTrace> agent_traces_;
    std::mutex mtx_;
    const std::string LOG_FILE = "data/logs.json";

    void save_logs_to_disk() {
        // Simple JSON dump of current memory state
        json j = json::array();
        for (const auto& log : logs_) {
            j.push_back({
                {"t", (long long)log.timestamp},
                {"p", log.project_id},
                {"rt", log.request_type},
                {"q", log.user_query},
                {"fp", log.full_prompt},
                {"r", log.ai_response},
                {"d", log.duration_ms}
            });
        }
        
        std::filesystem::create_directories("data");
        std::ofstream o(LOG_FILE);
        o << j.dump(2);
    }

    void load_logs_from_disk() {
        if (!std::filesystem::exists(LOG_FILE)) return;
        try {
            std::ifstream i(LOG_FILE);
            json j;
            i >> j;
            for (const auto& item : j) {
                InteractionLog log;
                log.timestamp = item.value("t", 0LL);
                log.project_id = item.value("p", "");
                log.request_type = item.value("rt", "AGENT");
                log.user_query = item.value("q", "");
                log.full_prompt = item.value("fp", "");
                log.ai_response = item.value("r", "");
                log.duration_ms = item.value("d", 0.0);
                logs_.push_back(log);
            }
        } catch (...) {
            std::cerr << "Failed to load logs. Starting fresh." << std::endl;
        }
    }
};

}