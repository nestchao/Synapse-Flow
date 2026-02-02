#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")
#else
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace code_assistance {

struct TelemetryData {
    // System
    double cpu_usage = 0.0;
    size_t ram_usage_mb = 0;
    size_t ram_total_mb = 0;
    
    // Application Latency
    double vector_latency_ms = 0.0;
    double embedding_latency_ms = 0.0;
    double llm_generation_ms = 0.0; 
    
    // AI Throughput
    int output_token_count = 0;     
    double tokens_per_second = 0.0; 
    int graph_nodes_scanned = 0; 

    double last_sync_duration_ms = 0.0; 
    double cache_size_mb = 0.0; 
};

class SystemMonitor {
public:
    // Global Atomic Metrics
    inline static std::atomic<double> global_vector_latency_ms{0.0};
    inline static std::atomic<double> global_embedding_latency_ms{0.0};
    inline static std::atomic<double> global_llm_generation_ms{0.0}; 
    inline static std::atomic<int> global_output_tokens{0};          
    inline static std::atomic<int> global_graph_nodes_scanned{0};
    inline static std::atomic<double> global_sync_latency_ms{0.0};
    inline static std::atomic<double> global_cache_size_mb{0.0};

    SystemMonitor() : stop_thread_(false) {
#ifdef _WIN32
        PdhOpenQuery(NULL, NULL, &cpuQuery);
        PdhAddCounter(cpuQuery, TEXT("\\Processor(_Total)\\% Processor Time"), NULL, &cpuCounter);
        PdhCollectQueryData(cpuQuery);
#endif
        monitor_thread_ = std::thread(&SystemMonitor::poll_routine, this);
    }

    ~SystemMonitor() {
        stop_thread_ = true;
        if (monitor_thread_.joinable()) monitor_thread_.join();
#ifdef _WIN32
        PdhCloseQuery(cpuQuery);
#endif
    }

    TelemetryData get_latest_snapshot() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return current_data_;
    }

private:
#ifdef _WIN32
    PDH_HQUERY cpuQuery;
    PDH_HCOUNTER cpuCounter;
#else
    // Linux CPU Calculation State
    unsigned long long prev_total_user = 0;
    unsigned long long prev_total_user_low = 0;
    unsigned long long prev_total_sys = 0;
    unsigned long long prev_total_idle = 0;
#endif

    TelemetryData current_data_;
    std::mutex data_mutex_;
    std::thread monitor_thread_;
    std::atomic<bool> stop_thread_;

    void poll_routine() {
        while (!stop_thread_) {
            TelemetryData snapshot;

#ifdef _WIN32
            // --- WINDOWS IMPLEMENTATION ---
            PDH_FMT_COUNTERVALUE counterVal;
            PdhCollectQueryData(cpuQuery);
            PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, NULL, &counterVal);
            snapshot.cpu_usage = counterVal.doubleValue;

            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                snapshot.ram_usage_mb = pmc.PrivateUsage / 1024 / 1024;
            }
            
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&memInfo);
            snapshot.ram_total_mb = memInfo.ullTotalPhys / 1024 / 1024;
#else
            // --- LINUX IMPLEMENTATION ---
            // 1. RAM Usage
            struct sysinfo memInfo;
            sysinfo(&memInfo);
            // Convert to MB
            snapshot.ram_total_mb = (memInfo.totalram * memInfo.mem_unit) / 1024 / 1024;
            long long physProc = (memInfo.totalram - memInfo.freeram) * memInfo.mem_unit;
            snapshot.ram_usage_mb = physProc / 1024 / 1024;

            // 2. CPU Usage (Parse /proc/stat)
            std::ifstream fileStat("/proc/stat");
            std::string line;
            if (std::getline(fileStat, line)) {
                std::istringstream iss(line);
                std::string cpuLabel;
                unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
                if (iss >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
                     unsigned long long total_idle = idle + iowait;
                     unsigned long long total_non_idle = user + nice + system + irq + softirq + steal;
                     unsigned long long total = total_idle + total_non_idle;
                     
                     unsigned long long prev_total = prev_total_idle + (prev_total_user + prev_total_user_low + prev_total_sys);
                     
                     unsigned long long total_diff = total - prev_total;
                     unsigned long long idle_diff = total_idle - prev_total_idle;

                     if (total_diff > 0) {
                        snapshot.cpu_usage = (double)(total_diff - idle_diff) / total_diff * 100.0;
                     }

                     // Update State
                     prev_total_idle = total_idle;
                     prev_total_user = user;
                     prev_total_user_low = nice;
                     prev_total_sys = system + irq + softirq + steal;
                }
            }
#endif

            // 3. Common Telemetry
            snapshot.vector_latency_ms = global_vector_latency_ms.load();
            snapshot.embedding_latency_ms = global_embedding_latency_ms.load();
            snapshot.llm_generation_ms = global_llm_generation_ms.load();
            snapshot.output_token_count = global_output_tokens.load();
            snapshot.graph_nodes_scanned = global_graph_nodes_scanned.load();
            snapshot.last_sync_duration_ms = global_sync_latency_ms.load();
            snapshot.cache_size_mb = global_cache_size_mb.load();

            if (snapshot.llm_generation_ms > 0) {
                snapshot.tokens_per_second = (snapshot.output_token_count / snapshot.llm_generation_ms) * 1000.0;
            } else {
                snapshot.tokens_per_second = 0.0;
            }

            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                current_data_ = snapshot;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
};
}