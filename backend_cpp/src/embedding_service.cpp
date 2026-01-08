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
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return true;
}

std::string utf8_safe_substr(const std::string& str, size_t length) {
    if (str.length() <= length) return str;
    return str.substr(0, length);
}

EmbeddingService::EmbeddingService(std::shared_ptr<KeyManager> key_manager)
    : key_manager_(key_manager), cache_manager_(std::make_shared<CacheManager>()) {}

std::string EmbeddingService::get_endpoint_url(const std::string& action) {
    std::string key = key_manager_->get_current_key();
    std::string model = key_manager_->get_current_model();
    
    std::string model_path = (action == "embedContent" || action == "batchEmbedContents") 
        ? "text-embedding-004" 
        : model;
        
    return base_url_ + model_path + ":" + action + "?key=" + key;
}

// 🚀 ELITE: Robust Request Wrapper
template<typename Func>
cpr::Response perform_request_with_retry(Func request_factory, std::shared_ptr<KeyManager> km) {
    int max_retries = 3; // Reduced for faster debugging
    cpr::Response r; 
    
    for (int i = 0; i < max_retries; ++i) {
        r = request_factory(); 
        if (r.status_code == 200) return r;

        // 🚀 DEBUG LOG: Print Retry info
        spdlog::warn("⚠️  API RETRY {}/{} | Status: {} | Error: {}", 
                     i+1, max_retries, r.status_code, r.error.message);

        if (r.status_code == 429 || r.status_code >= 500) {
            if(km) km->report_rate_limit();
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (i+1)));
            continue;
        }
        break; 
    }
    return r;
}

std::vector<float> EmbeddingService::generate_embedding(const std::string& text) {
    if (auto cached = cache_manager_->get_embedding(text)) return *cached;
    
    auto r = perform_request_with_retry([&]() {
        return cpr::Post(cpr::Url{get_endpoint_url("embedContent")},
                         cpr::Body(json{
                             {"model", "models/text-embedding-004"},
                             {"content", {{"parts", {{{"text", text}}}}}}
                         }.dump()),
                         cpr::Header{{"Content-Type", "application/json"}},
                         cpr::VerifySsl{false});
    }, key_manager_);
    
    if (r.status_code != 200) return {};
    try {
        auto j = json::parse(r.text);
        std::vector<float> vec = j["embedding"]["values"];
        cache_manager_->set_embedding(text, vec);
        return vec;
    } catch(...) { return {}; }
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
    return generate_text_elite(prompt).text;
}

GenerationResult EmbeddingService::generate_text_elite(const std::string& prompt) {
    GenerationResult final_result;
    
    std::string url = get_endpoint_url("generateContent");
    
    // Mask key for logging
    std::string masked_url = url;
    size_t key_pos = masked_url.find("key=");
    std::string key_hash = "UNKNOWN";
    if (key_pos != std::string::npos) {
        std::string key_val = url.substr(key_pos + 4);
        key_hash = key_val.substr(0, 4) + "..." + key_val.substr(key_val.length() - 4);
        masked_url.replace(key_pos + 4, key_val.length(), "*****");
    }
    
    spdlog::info("🚀 AI REQUEST | Key: {} | Length: {}", key_hash, prompt.length());

    auto r = perform_request_with_retry([&]() {
        return cpr::Post(cpr::Url{get_endpoint_url("generateContent")},
                      cpr::Body{json{
                          {"contents", {{ {"parts", {{{"text", prompt}}}} }}}
                      }.dump()},
                      cpr::Header{{"Content-Type", "application/json"}},
                      cpr::VerifySsl{false}, // 🛡️ CRITICAL FIX FOR WINDOWS
                      cpr::Timeout{15000}
        );
    }, key_manager_);

    spdlog::info("📥 AI RESPONSE | Status: {} | Time: {}s", r.status_code, r.elapsed);

    if (r.status_code == 200) {
        try {
            auto response_json = json::parse(r.text);
            if (response_json.contains("candidates") && !response_json["candidates"].empty()) {
                final_result.text = response_json["candidates"][0]["content"]["parts"][0]["text"];
                
                // Metrics
                if (response_json.contains("usageMetadata")) {
                    auto& usage = response_json["usageMetadata"];
                    final_result.prompt_tokens = usage.value("promptTokenCount", 0);
                    final_result.completion_tokens = usage.value("candidatesTokenCount", 0);
                    final_result.total_tokens = usage.value("totalTokenCount", 0);
                    SystemMonitor::global_output_tokens.store(final_result.completion_tokens);
                }

                final_result.success = true;
                return final_result;
            } else {
                spdlog::error("❌ AI LOGIC ERROR: {}", r.text);
            }
        } catch (const std::exception& e) {
            spdlog::error("❌ JSON PARSE ERROR: {}", e.what());
        }
    } else {
        spdlog::error("❌ HTTP ERROR: {}", r.status_code);
        spdlog::error("❌ RAW BODY:   {}", r.text);
    }

    final_result.text = "AI Service Error";
    final_result.success = false;
    return final_result;
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