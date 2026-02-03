#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <shared_mutex>
#include <optional>

#include "SystemMonitor.hpp" 
#include "embedding_service.hpp"

namespace code_assistance {

namespace fs = std::filesystem; 
using json = nlohmann::json;

// 🚀 COMPLETION CACHE - Sub-millisecond lookups
class CompletionCache {
private:
    struct CacheEntry {
        std::string completion;
        std::chrono::steady_clock::time_point timestamp;
        size_t hit_count = 0;
    };
    
    std::unordered_map<std::string, CacheEntry> cache_;
    mutable std::shared_mutex mutex_;
    size_t max_size_ = 2000;
    std::chrono::seconds ttl_{600}; // 10 min TTL
    
    std::string make_key(const std::string& prefix, const std::string& suffix, const std::string& file_path) const {
        size_t prefix_start = prefix.length() > 80 ? prefix.length() - 80 : 0;
        std::string prefix_tail = prefix.substr(prefix_start);
        std::string suffix_head = suffix.substr(0, (std::min)((size_t)30, suffix.length()));
        std::hash<std::string> hasher;
        return std::to_string(hasher(prefix_tail + "|" + suffix_head + "|" + file_path));
    }

public:
    std::optional<std::string> get(const std::string& prefix, const std::string& suffix, const std::string& file_path) {
        std::shared_lock lock(mutex_);
        auto key = make_key(prefix, suffix, file_path);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.timestamp;
            if (age < ttl_) {
                it->second.hit_count++;
                return it->second.completion;
            } else {
                cache_.erase(it);
            }
        }
        return std::nullopt;
    }
    
    void set(const std::string& prefix, const std::string& suffix, const std::string& file_path, const std::string& completion) {
        std::unique_lock lock(mutex_);
        auto key = make_key(prefix, suffix, file_path);
        if (cache_.size() >= max_size_) {
            cache_.erase(cache_.begin()); // Simple eviction
        }
        cache_[key] = {completion, std::chrono::steady_clock::now(), 0};
    }
    
    void clear() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }
};

// 🚀 CONTEXT PRELOADER
class ContextPreloader {
private:
    struct ContextEntry {
        std::string imports_and_defs;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::unordered_map<std::string, ContextEntry> contexts_;
    mutable std::shared_mutex mutex_;
    std::chrono::seconds ttl_{300}; 
    
public:
    void preload(const std::string& file_path, const std::string& full_content) {
        std::unique_lock lock(mutex_);
        std::string compact_context = full_content.substr(0, (std::min)((size_t)1200, full_content.length()));
        contexts_[file_path] = {compact_context, std::chrono::steady_clock::now()};
    }
    
    std::string get(const std::string& file_path) const {
        std::shared_lock lock(mutex_);
        auto it = contexts_.find(file_path);
        if (it != contexts_.end()) {
            return it->second.imports_and_defs;
        }
        return "";
    }
    
    void invalidate(const std::string& file_path) {
        std::unique_lock lock(mutex_);
        contexts_.erase(file_path);
    }
};

static CompletionCache g_completion_cache;
static ContextPreloader g_context_preloader;

// Utility Implementation
std::string utf8_safe_substr(const std::string& str, size_t length) {
    if (str.length() <= length) return str;
    return str.substr(0, length);
}

// 🚀 PUBLIC API IMPLEMENTATION
void preload_file_context(const std::string& file_path, const std::string& full_content) {
    g_context_preloader.preload(file_path, full_content);
}

void invalidate_file_context(const std::string& file_path) {
    g_context_preloader.invalidate(file_path);
}

void clear_completion_cache() {
    g_completion_cache.clear();
}

// --- EmbeddingService Implementation ---

EmbeddingService::EmbeddingService(std::shared_ptr<KeyManager> key_manager)
    : key_manager_(key_manager), cache_manager_(std::make_shared<CacheManager>()) {}

std::string EmbeddingService::get_endpoint_url(const std::string& action) {
    std::string key = key_manager_->get_current_key();
    std::string model = key_manager_->get_current_model();
    
    std::string model_path;
    if (action == "embedContent" || action == "batchEmbedContents") {
        model_path = "models/text-embedding-004"; 
    } else {
        if (model.find("models/") == 0) model_path = model;
        else model_path = "models/" + model;
    }
    return base_url_ + model_path + ":" + action + "?key=" + key;
}

// Helper: Fail-Fast Retry
template<typename Func>
cpr::Response perform_request_with_retry_fast(Func request_factory, std::shared_ptr<KeyManager> km) {
    const int MAX_RETRIES = 2;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        cpr::Response r = request_factory();
        if (r.status_code == 200) return r;
        if (r.status_code == 404 || r.status_code == 400) return r;
        
        if (r.status_code == 429 || r.status_code >= 500) {
            if (attempt < MAX_RETRIES - 1) {
                km->rotate_key();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }
        return r;
    }
    return cpr::Response{};
}

GenerationResult EmbeddingService::call_gemini_api(const std::string& prompt) {
    GenerationResult final_result;
    auto r = perform_request_with_retry_fast([&]() {
        return cpr::Post(cpr::Url{get_endpoint_url("generateContent")},
                      cpr::Body{json{
                          {"contents", {{ {"parts", {{{"text", prompt}}}} }}}
                      }.dump()},
                      cpr::Header{{"Content-Type", "application/json"}},
                      cpr::VerifySsl{false}, 
                      cpr::Timeout{120000});
    }, key_manager_);

    if (r.status_code == 200) {
        try {
            auto response_json = json::parse(r.text);
            if (response_json.contains("candidates") && !response_json["candidates"].empty()) {
                final_result.text = response_json["candidates"][0]["content"]["parts"][0]["text"];
                final_result.success = true;
                return final_result;
            }
        } catch (...) {}
    } 
    final_result.success = false;
    return final_result;
}

GenerationResult EmbeddingService::call_python_bridge(const std::string& prompt) {
    cpr::Response r = cpr::Post(
        cpr::Url{python_bridge_url_},
        cpr::Body{json{{"prompt", prompt}}.dump()},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Timeout{180000} 
    );

    GenerationResult result;
    if (r.status_code == 200) {
        try {
            auto j = json::parse(r.text);
            if (j.value("success", false)) {
                result.text = j.value("text", "");
                result.success = true;
                return result;
            }
        } catch (...) {}
    }
    result.success = false;
    return result;
}

std::vector<float> EmbeddingService::generate_embedding(const std::string& text) {
    auto r = perform_request_with_retry_fast([&]() {
        std::string key = key_manager_->get_current_key();
        std::string url = base_url_ + "models/text-embedding-004:embedContent?key=" + key;
        return cpr::Post(cpr::Url{url},
                         cpr::Body(json{
                             {"model", "models/text-embedding-004"},
                             {"content", {{"parts", {{{"text", text}}}}}}
                         }.dump()),
                         cpr::Header{{"Content-Type", "application/json"}},
                         cpr::VerifySsl{false});
    }, key_manager_);
    
    if (r.status_code == 200) {
        try {
            auto j = json::parse(r.text);
            if (j.contains("embedding") && j["embedding"].contains("values")) {
                std::vector<float> vec = j["embedding"]["values"];
                cache_manager_->set_embedding(text, vec);
                return vec;
            }
        } catch(...) {}
    }
    return {};
}

std::vector<std::vector<float>> EmbeddingService::generate_embeddings_batch(const std::vector<std::string>& texts) {
    if (texts.empty()) return {};
    std::string key = key_manager_->get_current_key();
    std::string url = base_url_ + "models/text-embedding-004:batchEmbedContents?key=" + key;

    json requests = json::array();
    for (const auto& text : texts) {
        requests.push_back({
            {"model", "models/text-embedding-004"},
            {"content", {{"parts", {{{"text", text}}}}}}
        });
    }

    auto r = cpr::Post(cpr::Url{url},
        cpr::Body(json{{"requests", requests}}.dump()),
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::VerifySsl{false}
    );

    std::vector<std::vector<float>> results;
    if (r.status_code == 200) {
        try {
            auto j = json::parse(r.text);
            if (j.contains("embeddings")) {
                for (const auto& item : j["embeddings"]) {
                    if (item.contains("values")) {
                        results.push_back(item["values"].get<std::vector<float>>());
                    } else {
                        results.push_back({});
                    }
                }
            }
        } catch(...) {}
    }
    return results;
}

std::string EmbeddingService::generate_text(const std::string& prompt) {
    return generate_text_elite(prompt).text;
}

GenerationResult EmbeddingService::generate_text_elite(const std::string& prompt, RoutingStrategy strategy) {
    if (strategy == RoutingStrategy::SPEED_FIRST) {
        GenerationResult api_res = call_gemini_api(prompt);
        if (api_res.success) return api_res;
        return call_python_bridge(prompt);
    } else {
        GenerationResult bridge_res = call_python_bridge(prompt);
        if (bridge_res.success) return bridge_res;
        return call_gemini_api(prompt);
    }
}

VisionResult EmbeddingService::analyze_vision(const std::string& prompt, const std::string& base64_image) {
    VisionResult result;
    result.success = false;
    json payload = {
        {"contents", {{
            {"parts", {
                {{"text", prompt}},
                {{"inline_data", {{"mime_type", "image/jpeg"}, {"data", base64_image}}}}
            }}
        }}}
    };
    auto r = cpr::Post(cpr::Url{get_endpoint_url("generateContent")},
                  cpr::Body{payload.dump()},
                  cpr::Header{{"Content-Type", "application/json"}});
    if (r.status_code == 200) {
        auto j = json::parse(r.text);
        if (j["candidates"][0]["content"]["parts"].size() > 0) {
            result.analysis = j["candidates"][0]["content"]["parts"][0]["text"];
            result.success = true;
        }
    }
    return result;
}

std::string EmbeddingService::generate_autocomplete(
    const std::string& prefix, 
    const std::string& suffix, 
    const std::string& project_context,
    const std::string& file_path
) {
    // 1. Check Cache
    auto cached = g_completion_cache.get(prefix, suffix, file_path);
    if (cached.has_value()) return cached.value();
    
    // 2. Get Preloaded Context
    std::string context = g_context_preloader.get(file_path);
    if (context.empty() && !prefix.empty()) {
        size_t ctx_start = prefix.length() > 600 ? prefix.length() - 600 : 0;
        context = prefix.substr(ctx_start);
    }
    
    size_t prefix_tail_size = (std::min)((size_t)150, prefix.length());
    std::string prefix_tail = prefix.substr(prefix.length() - prefix_tail_size);
    size_t suffix_head_size = (std::min)((size_t)80, suffix.length());
    std::string suffix_head = suffix.substr(0, suffix_head_size);
    
    std::string prompt = 
        "Complete code at <CURSOR>. Return ONLY the completion.\n\n"
        "File context:\n" + context.substr(0, (std::min)((size_t)400, context.length())) + "\n\n"
        "Code:\n" + prefix_tail + "<CURSOR>" + suffix_head;
    
    auto pair = key_manager_->get_current_pair();
    std::string url = base_url_ + pair.model + ":generateContent?key=" + pair.key;
    
    json payload = {
        {"contents", {{{"parts", {{{"text", prompt}}}}}}},
        {"generationConfig", {
            {"maxOutputTokens", 40},
            {"temperature", 0.0},
            {"topP", 0.9},
            {"candidateCount", 1},
            {"stopSequences", {"```", "\n\n", "//", "#"}}
        }}
    };
    
    cpr::Response r = cpr::Post(
        cpr::Url{url},
        cpr::Body{payload.dump()},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::VerifySsl{false},
        cpr::Timeout{1500}
    );
    
    if (r.status_code != 200) return "";
    
    try {
        auto j = json::parse(r.text);
        if (j["candidates"].empty()) return "";
        std::string text = j["candidates"][0]["content"]["parts"][0]["text"];
        
        // Quick cleanup
        if (text.find("```") != std::string::npos) {
            size_t start_pos = text.find("```");
            size_t end_pos = text.rfind("```");
            if (start_pos != std::string::npos) text = text.substr(text.find('\n', start_pos) + 1);
            if (end_pos != std::string::npos && end_pos > 0) text = text.substr(0, end_pos);
        }
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        
        if (!text.empty()) {
            g_completion_cache.set(prefix, suffix, file_path, text);
        }
        return text;
    } catch(...) { return ""; }
}

} // namespace code_assistance