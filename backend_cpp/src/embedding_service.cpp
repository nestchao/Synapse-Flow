// backend_cpp/src/embedding_service.cpp
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <cmath>

#include "SystemMonitor.hpp" 
#include "embedding_service.hpp"

namespace code_assistance {

using json = nlohmann::json;

// 🚀 UTILITY: Shutdown-aware sleep
// Returns false if shutdown requested, true if sleep completed
bool smart_sleep(int milliseconds) {
    int slices = milliseconds / 100;
    for (int i = 0; i < slices; ++i) {
        // In a real engine, we'd check a global shutdown flag here
        // if (global_shutdown) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

std::string utf8_safe_substr(const std::string& str, size_t length) {
    if (str.length() <= length) return str;
    std::string sub = str.substr(0, length);
    if (sub.empty()) return sub;
    while (!sub.empty()) {
        unsigned char c = static_cast<unsigned char>(sub.back());
        if (c < 0x80) break;
        if (c >= 0xC0) { sub.pop_back(); break; }
        sub.pop_back();
    }
    return sub;
}

EmbeddingService::EmbeddingService(std::shared_ptr<KeyManager> key_manager)
    : key_manager_(key_manager), cache_manager_(std::make_shared<CacheManager>()) {}

std::string EmbeddingService::get_endpoint_url(const std::string& action) {
    std::string model = (action == "embedContent" || action == "batchEmbedContents") 
        ? "text-embedding-004" 
        : key_manager_->get_current_model();
        
    return base_url_ + model + ":" + action + "?key=" + key_manager_->get_current_key();
}

// 🚀 ELITE: Robust Request Wrapper
template<typename Func>
cpr::Response perform_request_with_retry(Func request_factory, std::shared_ptr<KeyManager> km) {
    int max_retries = 5; 
    cpr::Response r; 
    
    for (int i = 0; i < max_retries; ++i) {
        r = request_factory(); 
        
        if (r.status_code == 200) return r;

        bool is_quota = (r.status_code == 429);
        bool is_server_err = (r.status_code >= 500);

        if ((is_quota || is_server_err) && km) {
            km->report_rate_limit(); // Rotates key internally
            
            // 🚀 STRATEGY: If we switched keys, try immediately (minimal jitter). 
            // Only sleep deep if we looped through all keys.
            int active_keys = km->get_active_key_count();
            int backoff_ms = (i > active_keys) ? (1000 * std::pow(2, i - active_keys)) : 50;

            spdlog::warn("⚠️ API {} | Retry {}/{} | Backoff: {}ms", 
                         r.status_code, i + 1, max_retries, backoff_ms);
            
            if (!smart_sleep(backoff_ms)) break; // Exit if system shutting down
            continue;
        }
        break; // Fatal error (400, 401, etc)
    }
    return r;
}

std::vector<float> EmbeddingService::generate_embedding(const std::string& text) {
    if (auto cached = cache_manager_->get_embedding(text)) return *cached;

    auto start = std::chrono::high_resolution_clock::now();

    auto r = perform_request_with_retry([&]() {
        return cpr::Post(cpr::Url{get_endpoint_url("embedContent")},
                         cpr::Body(json{
                             {"model", "models/text-embedding-004"},
                             {"content", {{"parts", {{{"text", text}}}}}}
                         }.dump()),
                         cpr::Header{{"Content-Type", "application/json"}});
    }, key_manager_);

    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();
    SystemMonitor::global_embedding_latency_ms.store(duration);

    if (r.status_code != 200) {
        spdlog::error("❌ Embedding API Fatal Error [{}]: {}", r.status_code, r.text);
        throw std::runtime_error("Failed to generate embedding after retries");
    }

    try {
        auto response_json = json::parse(r.text);
        std::vector<float> embedding = response_json["embedding"]["values"];
        cache_manager_->set_embedding(text, embedding);
        return embedding;
    } catch (...) {
        throw std::runtime_error("Malformed JSON from Embedding API");
    }
}

std::vector<std::vector<float>> EmbeddingService::generate_embeddings_batch(const std::vector<std::string>& texts) {
    json requests = json::array();
    for(const auto& raw_text : texts){
        requests.push_back({
            {"model", "models/text-embedding-004"},
            {"content", { {"parts", {{{"text", raw_text}}}} }}
        });
    }
    
    // Google Batch API specific structure
    std::string payload_str = json{{"requests", requests}}.dump();
    
    auto r = perform_request_with_retry([&]() {
        return cpr::Post(cpr::Url{get_endpoint_url("batchEmbedContents")}, 
                         cpr::Body{payload_str}, 
                         cpr::Header{{"Content-Type", "application/json"}});
    }, key_manager_);

    if (r.status_code != 200) {
        spdlog::error("Batch Embedding API error [{}]: {}", r.status_code, r.text);
        throw std::runtime_error("Failed to generate batch embeddings");
    }
    
    auto response_json = json::parse(r.text);
    std::vector<std::vector<float>> embeddings;
    
    if (response_json.contains("embeddings")) {
        for(const auto& emb : response_json["embeddings"]){
            if (emb.contains("values")) {
                embeddings.push_back(emb["values"].get<std::vector<float>>());
            } else {
                embeddings.push_back({}); // Handle failure case gracefully
            }
        }
    }
    return embeddings;
}

std::string EmbeddingService::generate_text(const std::string& prompt) {
    auto res = generate_text_elite(prompt);
    return res.text;
}

GenerationResult EmbeddingService::generate_text_elite(const std::string& prompt) {
    GenerationResult final_result;
    
    auto r = perform_request_with_retry([&]() {
        json payload = {{"contents", {{ {"parts", {{{"text", prompt}}}} }}}};
        return cpr::Post(cpr::Url{get_endpoint_url("generateContent")},
                      cpr::Body{payload.dump()},
                      cpr::Header{{"Content-Type", "application/json"}});
    }, key_manager_);

    if (r.status_code == 200) {
        try {
            auto response_json = json::parse(r.text);
            
            // Safety check for candidates
            if (!response_json.contains("candidates") || response_json["candidates"].empty()) {
                final_result.text = "ERROR: Empty response from AI.";
                final_result.success = false;
                return final_result;
            }

            auto& candidate = response_json["candidates"][0];
            
            // Check for safety blocks
            if (candidate.contains("finishReason") && candidate["finishReason"] == "SAFETY") {
                final_result.text = "ERROR: Response blocked by safety filters.";
                final_result.success = false;
                return final_result;
            }

            if (candidate["content"]["parts"].empty()) {
                final_result.text = "ERROR: No text parts in response.";
                final_result.success = false;
                return final_result;
            }

            final_result.text = candidate["content"]["parts"][0]["text"];
            
            if (response_json.contains("usageMetadata")) {
                auto& usage = response_json["usageMetadata"];
                final_result.prompt_tokens = usage.value("promptTokenCount", 0);
                final_result.completion_tokens = usage.value("candidatesTokenCount", 0);
                final_result.total_tokens = usage.value("totalTokenCount", 0);
                SystemMonitor::global_output_tokens.store(final_result.completion_tokens);
            }
            
            final_result.success = true;
            return final_result;
        } catch (const std::exception& e) {
            spdlog::error("JSON Parse Error: {}", e.what());
        }
    }

    final_result.text = "ERROR: API Failure " + std::to_string(r.status_code);
    final_result.success = false;
    return final_result;
}

// ... (Vision and Autocomplete implementations remain similar) ...
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
    size_t total_keys = key_manager_->get_total_keys();
    size_t total_models = key_manager_->get_total_models();
    size_t max_attempts = total_keys * total_models;
    
    for(size_t attempt = 0; attempt < max_attempts; ++attempt) {
        auto pair = key_manager_->get_current_pair();
        std::string url = base_url_ + pair.model + ":generateContent?key=" + pair.key;
        
        // 🏗️ GOD MODE PROMPT CONSTRUCTION
        std::string full_prompt = 
            "ROLE: Senior Code Completion Engine.\n"
            "TASK: Complete the code at the <CURSOR> position. Return ONLY the code to be inserted.\n"
            "RULES:\n"
            "1. No Markdown (```). No conversational text.\n"
            "2. Use the provided PROJECT TOPOLOGY and CONTEXT to resolve imports/definitions.\n"
            "3. If the user started a word, complete it. Do not repeat the prefix.\n\n"
            + project_context + "\n\n" // <--- 0ms RAM Injection
            "### CURRENT FILE: " + file_path + "\n"
            "[START]\n" + prefix + "<CURSOR>" + suffix + "\n[END]";

        json payload = {
            {"contents", {{ {"parts", {{{"text", full_prompt}}}} }}},
            {"generationConfig", {
                {"maxOutputTokens", 64}, // Low token count for latency
                {"stopSequences", {"```", "\n\n\n"}} 
            }}
        };

        // Short timeout (4s) for Ghost Text - fail fast if network lags
        auto r = cpr::Post(cpr::Url{url}, cpr::Body{payload.dump()}, cpr::Header{{"Content-Type", "application/json"}}, cpr::Timeout{4000});
        
        if (r.status_code == 200) {
            try {
                auto j = json::parse(r.text);
                if (j["candidates"].empty()) { key_manager_->rotate_key(); continue; }
                
                std::string text = j["candidates"][0]["content"]["parts"][0]["text"];
                
                // 1. Sanitize Markdown
                if (text.find("```") != std::string::npos) {
                    size_t start = text.find("```");
                    size_t end = text.rfind("```");
                    if (start != std::string::npos) text = text.substr(text.find('\n', start) + 1);
                    if (end != std::string::npos && end > 0) text = text.substr(0, end);
                }

                // 2. Smart Overlap Detection
                // Prevents "std::vec" -> "vector" becoming "std::vecvector"
                if (!prefix.empty() && !text.empty()) {
                    size_t check_len = (std::min)((size_t)15, prefix.length());
                    std::string tail = prefix.substr(prefix.length() - check_len);
                    
                    for (size_t i = 0; i < check_len; ++i) {
                        std::string sub = tail.substr(i);
                        if (text.find(sub) == 0) {
                            text = text.substr(sub.length());
                            break;
                        }
                    }
                }

                // 3. Trim whitespace logic
                if (!text.empty() && text.back() == '\n') text.pop_back();

                spdlog::info("✅ Ghost Generated ({} chars) via {}", text.length(), pair.model);
                return text;

            } catch(...) { key_manager_->rotate_key(); continue; }
        }
        
        if (r.status_code == 429) {
            key_manager_->report_rate_limit();
            continue;
        }
        key_manager_->rotate_key();
    }
    return "";
}

} // namespace code_assistance