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
    
    std::string model_path;
    if (action == "embedContent" || action == "batchEmbedContents") {
        model_path = "models/text-embedding-004"; 
    } else {
        if (model.find("models/") == 0) model_path = model;
        else model_path = "models/" + model;
    }
    
    std::string final_url = base_url_ + model_path + ":" + action + "?key=" + key;
    
    spdlog::info("🔍 Action: {}, Model Path: {}", action, model_path);
    
    return final_url;
}

// 🚀 ELITE: Robust Request Wrapper
template<typename Func>
cpr::Response perform_request_with_retry(Func request_factory, std::shared_ptr<KeyManager> km) {
    
    // 1. Determine how many unique keys we have. We will try ALL of them before failing.
    size_t max_retries = km->get_total_keys();
    if (max_retries == 0) max_retries = 1; // Safety

    cpr::Response r; 
    
    for (size_t i = 0; i < max_retries; ++i) {
        
        // Factory gets the *current* key from KeyManager
        r = request_factory(); 

        // ✅ Success
        if (r.status_code == 200) {
            if (i > 0) spdlog::info("✅ API Recovered on attempt {}/{}", i + 1, max_retries);
            return r;
        }

        // ❌ Error Handling
        spdlog::warn("⚠️ API FAILURE {}/{} | Status: {} | Error: {}", 
                     i + 1, max_retries, r.status_code, r.text.substr(0, 100));

        // 404 Not Found (Model name wrong? Key invalid?) -> Rotate Key Immediately
        if (r.status_code == 404) {
            // spdlog::error("❌ 404 Error: Invalid Model or Endpoint. Rotating Key...");
            // km->rotate_key();
            // Optional: Rotate model too if you suspect the model name is the issue
            spdlog::error("❌ 404 Error: Invalid Model/Endpoint. Stopping retries to save time.");
            break; // Retry immediately with next key
        }

        // 429 Rate Limit -> Rotate Key + Small Sleep
        if (r.status_code == 429) {
            // Calculate delay: 500ms * 2^attempt
            int delay_ms = 500 * std::pow(2, i);
            spdlog::warn("⏳ Rate Limit Hit. Backing off for {}ms...", delay_ms);
            km->rotate_key();
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms)); 
            continue;
        }

        // 500+ Server Error -> Rotate Key (Maybe that region is down)
        if (r.status_code >= 500) {
            km->rotate_key();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // 400 Bad Request (Invalid JSON/Prompt) -> Don't retry, it won't help
        if (r.status_code == 400) {
            spdlog::error("🛑 400 Bad Request. Stopping retries.");
            break;
        }
    }

    return r; // Return the last response (likely error) if all keys failed
}

GenerationResult EmbeddingService::call_gemini_api(const std::string& prompt) {
    GenerationResult final_result;
    std::string url = get_endpoint_url("generateContent");

    auto r = perform_request_with_retry([&]() {
        return cpr::Post(cpr::Url{get_endpoint_url("generateContent")},
                      cpr::Body{json{
                          {"contents", {{ {"parts", {{{"text", prompt}}}} }}}
                      }.dump()},
                      cpr::Header{{"Content-Type", "application/json"}},
                      cpr::VerifySsl{false}, 
                      cpr::Timeout{120000}
        );
    }, key_manager_);

    if (r.status_code == 200) {
        try {
            auto response_json = json::parse(r.text);
            if (response_json.contains("candidates") && !response_json["candidates"].empty()) {
                final_result.text = response_json["candidates"][0]["content"]["parts"][0]["text"];
                
                if (response_json.contains("usageMetadata")) {
                    auto& usage = response_json["usageMetadata"];
                    final_result.prompt_tokens = usage.value("promptTokenCount", 0);
                    final_result.completion_tokens = usage.value("candidatesTokenCount", 0);
                    final_result.total_tokens = usage.value("totalTokenCount", 0);
                    SystemMonitor::global_output_tokens.store(final_result.completion_tokens);
                }
                final_result.success = true;
                return final_result;
            }
        } catch (...) {}
    } 
    
    spdlog::error("❌ API ERROR: {}", r.status_code);
    final_result.text = "API Failure";
    final_result.success = false;
    return final_result;
}

GenerationResult EmbeddingService::call_python_bridge(const std::string& prompt) {

    if (prompt.length() > 50000) {
        spdlog::info("📦 Prompt too large ({} chars). Switching to File Upload Strategy...", prompt.length());
        
        // 1. Create Temp File
        std::string temp_filename = "context_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt";
        fs::path temp_path = fs::temp_directory_path() / temp_filename;
        
        std::ofstream ofs(temp_path);
        ofs << prompt;
        ofs.close();

        // 2. Call Bridge with 'upload_extract' (which we defined in Python)
        // We actually need a new route in main.cpp for this or adapt the existing one.
        // For now, let's adapt the JSON payload to tell the Python server what to do.
        
        // NOTE: You need to update `handle_generate_code` in main.cpp to handle this flag 
        // OR simply pass the file path in the JSON if the Python server supports it.
        
        // Let's assume we stick to the text paste for now but truncate context if it fails repeatedly.
    }

    spdlog::warn("🌉 Switching to Python Browser Bridge (Free Tier)...");
    auto start = std::chrono::high_resolution_clock::now();

    // High timeout (180s) because Browser automation is slower than API
    cpr::Response r = cpr::Post(
        cpr::Url{python_bridge_url_},
        cpr::Body{json{{"prompt", prompt}}.dump()},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Timeout{180000} 
    );

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    GenerationResult result;
    if (r.status_code == 200) {
        try {
            auto j = json::parse(r.text);
            if (j.value("success", false)) {
                result.text = j.value("text", "");
                result.success = true;
                
                // Estimate tokens since Bridge doesn't provide exact usage
                result.completion_tokens = j.value("estimated_tokens", 0);
                result.prompt_tokens = prompt.length() / 4;
                result.total_tokens = result.prompt_tokens + result.completion_tokens;
                
                spdlog::info("✅ [Bridge] Success in {:.2f}s", ms / 1000.0);
                return result;
            }
        } catch (...) {}
    }

    spdlog::error("❌ [Bridge] Failed. Status: {} | Body: {}", r.status_code, r.text);
    result.success = false;
    result.text = "Bridge Failure";
    return result;
}

std::vector<float> EmbeddingService::generate_embedding(const std::string& text) {
    // if (auto cached = cache_manager_->get_embedding(text)) return *cached;
    
    // // Direct call - no fallback needed
    // auto r = perform_request_with_retry([&]() {
    //     std::string key = key_manager_->get_current_key();
    //     std::string url = base_url_ + "models/text-embedding-004:embedContent?key=" + key;

    //     // 🔍 ADD THIS LOG HERE:
    //     spdlog::info("🔍 BASE_URL: {}", base_url_);
        
    //     return cpr::Post(cpr::Url{url},
    //                      cpr::Body(json{
    //                          {"model", "models/text-embedding-004"},
    //                          {"content", {{"parts", {{{"text", text}}}}}}
    //                      }.dump()),
    //                      cpr::Header{{"Content-Type", "application/json"}},
    //                      cpr::VerifySsl{false});
    // }, key_manager_);
    
    // if (r.status_code == 200) {
    //     try {
    //         auto j = json::parse(r.text);
    //         if (j.contains("embedding") && j["embedding"].contains("values")) {
    //             std::vector<float> vec = j["embedding"]["values"];
    //             cache_manager_->set_embedding(text, vec);
    //             return vec;
    //         }
    //     } catch(...) { 
    //         spdlog::error("❌ Failed to parse embedding JSON");
    //     }
    // }

    // spdlog::error("❌ Embedding request failed with status: {}", r.status_code);
    return {};
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

GenerationResult EmbeddingService::generate_text_elite(const std::string& prompt, RoutingStrategy strategy) {
    
    if (strategy == RoutingStrategy::SPEED_FIRST) {
        spdlog::info("⚡ Routing: SPEED MODE (API -> Bridge)");
        
        // Try API First
        GenerationResult api_res = call_gemini_api(prompt);
        if (api_res.success) return api_res;
        
        // Fallback to Bridge
        spdlog::warn("⚠️ API failed. Falling back to Bridge...");
        return call_python_bridge(prompt);
    } 
    else {
        spdlog::info("🌉 Routing: QUALITY MODE (Bridge -> API)");
        
        // Try Bridge First
        GenerationResult bridge_res = call_python_bridge(prompt);
        if (bridge_res.success) return bridge_res;
        
        // Fallback to API
        spdlog::warn("⚠️ Bridge failed. Falling back to API...");
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

    std::string slim_context = project_context;
    if (slim_context.length() > 20000) {
        // Just take the beginning (Topology) and maybe recent file definitions
        slim_context = slim_context.substr(0, 20000) + "\n...[Truncated for Speed]...";
    }
    
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