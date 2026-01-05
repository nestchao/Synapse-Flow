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
    
    std::atomic<size_t> current_key_index{0};
    std::atomic<size_t> current_model_index{0};
    
    std::string serper_key;

public:
    KeyManager() {
        refresh_key_pool();
    }

    void refresh_key_pool() {
        std::unique_lock lock(pool_mutex);

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
                model_pool = { "gemini-2.0-flash", "gemini-1.5-flash" };
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
        size_t key_index;
        size_t model_index;
    };

    KeyModelPair get_current_pair() const {
        std::shared_lock lock(pool_mutex); // Reader lock
        if (key_pool.empty() || model_pool.empty()) return {"", "", 0, 0};
        
        size_t start_idx = current_key_index.load();
        size_t pool_size = key_pool.size();
        
        // 🚀 SMART SEARCH: Iterate to find the first ACTIVE key
        // This prevents us from using a key we already know is 429'd
        for (size_t i = 0; i < pool_size; ++i) {
            size_t idx = (start_idx + i) % pool_size;
            if (key_pool[idx].is_active) {
                // Found a live one!
                size_t m_idx = current_model_index.load() % model_pool.size();
                return { key_pool[idx].key, model_pool[m_idx], idx, m_idx };
            }
        }
        
        // ⚠️ ALL KEYS DEAD?
        // Fallback: Return the current one anyway. The request will fail,
        // triggering report_rate_limit(), which will trigger the Phoenix Protocol.
        size_t k_idx = start_idx % pool_size;
        size_t m_idx = current_model_index.load() % model_pool.size();
        return { key_pool[k_idx].key, model_pool[m_idx], k_idx, m_idx };
    }

    std::string get_current_key() const { return get_current_pair().key; }
    std::string get_current_model() const { return get_current_pair().model; }

    std::string get_serper_key() const { 
        std::shared_lock lock(pool_mutex);
        return serper_key; 
    }

    void rotate_key() {
        current_key_index++;
    }

    void rotate_model() {
        current_model_index++;
        // NOTE: We do NOT reset current_key_index here anymore.
        // We want to keep using the current active key logic even on a new model.
    }

    // 🚀 INTELLIGENT DECOMMISSIONING
    void report_rate_limit() {
        std::unique_lock lock(pool_mutex); // Writer lock
        if (key_pool.empty()) return;
        
        size_t idx = current_key_index.load() % key_pool.size();
        
        // Only penalize if currently considered active
        if (key_pool[idx].is_active) {
            key_pool[idx].fail_count++;
            
            // Tolerance threshold (allow 2 failures before ban)
            if (key_pool[idx].fail_count > 2) {
                key_pool[idx].is_active = false;
                spdlog::warn("⚠️ Key #{} Decommissioned due to Rate Limits", idx);
            }
        }
        
        // 🚀 PHOENIX PROTOCOL: Check if we have burned the entire inventory
        bool any_active = false;
        for(const auto& k : key_pool) {
            if(k.is_active) { any_active = true; break; }
        }
        
        if (!any_active) {
            spdlog::error("🔥 PHOENIX PROTOCOL: All keys exhausted. Reviving Vault.");
            // Reset everything to give it another shot
            for(auto& k : key_pool) { 
                k.is_active = true; 
                k.fail_count = 0; 
            }
        }

        // Move to next key immediately
        current_key_index++;
    }

    size_t get_active_key_count() const {
        std::shared_lock lock(pool_mutex);
        size_t count = 0;
        for (const auto& k : key_pool) if (k.is_active) count++;
        return count;
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