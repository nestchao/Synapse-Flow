#pragma once
#include <vector>
#include <string>
#include <shared_mutex>
#include <nlohmann/json.hpp>
#include <fstream>
#include <atomic>
#include <spdlog/spdlog.h>

namespace code_assistance {

class KeyManager {
private:
    struct ApiKey {
        std::string key;
        bool is_active = true;
        int fail_count = 0;
    };

    std::vector<ApiKey> key_pool;
    std::vector<std::string> model_pool;
    mutable std::shared_mutex pool_mutex;
    
    // Atomic index acts as the "Pointer" to the last usable key
    std::atomic<size_t> current_key_index{0};
    std::atomic<size_t> current_model_index{0};
    
    std::string serper_key;

public:
    KeyManager() {
        refresh_key_pool();
    }

    void refresh_key_pool() {
        std::unique_lock lock(pool_mutex);

        // Try to load keys.json
        std::vector<std::string> search_paths = {
            "keys.json", "../keys.json", "build/keys.json", "Release/keys.json", "../../keys.json"
        };

        std::ifstream f;
        for (const auto& path : search_paths) {
            f.open(path);
            if (f.is_open()) break;
        }

        if (!f.is_open()) {
            spdlog::error("🚨 CRITICAL: keys.json not found!");
            return;
        }

        try {
            auto j = nlohmann::json::parse(f);
            
            key_pool.clear();
            for (auto& k : j["keys"]) {
                key_pool.push_back({k.get<std::string>(), true, 0});
            }
            
            model_pool.clear();
            if (j.contains("models") && j["models"].is_array()) {
                for (auto& m : j["models"]) {
                    model_pool.push_back(m.get<std::string>());
                }
            } else {
                // 🚀 FIX 404: Use currently valid model names
                model_pool = { "gemini-3-flash-preview", "gemini-2.5-flash" };
            }
            
            serper_key = j.value("serper", "");
            current_key_index = 0;
            current_model_index = 0;
            
            spdlog::info("🛰️ Unified Vault: {} keys, {} models loaded.", key_pool.size(), model_pool.size());
            
        } catch (const std::exception& e) {
            spdlog::error("💥 Failed to parse keys.json: {}", e.what());
        }
    }

    struct KeyModelPair {
        std::string key;
        std::string model;
    };

    KeyModelPair get_current_pair() const {
        std::shared_lock lock(pool_mutex);
        if (key_pool.empty()) return {"", ""};
        if (model_pool.empty()) return {"", "gemini-3-flash-preview"};

        // Simple atomic read - gets the key currently pointed to
        size_t k_idx = current_key_index.load() % key_pool.size();
        size_t m_idx = current_model_index.load() % model_pool.size();
        
        return { key_pool[k_idx].key, model_pool[m_idx] };
    }

    std::string get_current_key() const { return get_current_pair().key; }
    std::string get_current_model() const { return get_current_pair().model; }
    std::string get_serper_key() const { std::shared_lock lock(pool_mutex); return serper_key; }

    // 🚀 NEW: Explicit rotation for Retries
    void rotate_key() {
        size_t prev = current_key_index.fetch_add(1);
        spdlog::info("🔄 Rotating Key Pointer: {} -> {}", prev, prev + 1);
    }

    void rotate_model() {
        current_model_index++;
    }

    // Logic to mark a key as "dead" if it fails too often (Optional, handled by retry loop now)
    void report_rate_limit() {
        rotate_key(); // Just rotate for now
    }

    size_t get_total_keys() const {
        std::shared_lock lock(pool_mutex);
        return key_pool.size();
    }

    size_t get_total_models() const {
        std::shared_lock lock(pool_mutex);
        return model_pool.size();
    }
};

}