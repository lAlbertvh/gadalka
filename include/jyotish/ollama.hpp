#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <chrono>

namespace jyotish::ollama {

// Real HTTP client for Ollama (chat, vision, list models, generate with tools).
class Client {
public:
    explicit Client(const std::string& base_url = "http://localhost:11434");

    // Chat completion against /api/chat. Returns the assistant message content.
    // If options contains a "tools" array, tools are passed through.
    std::string chat(const std::string& model, const nlohmann::json& messages,
                     double temperature = 0.7,
                     const nlohmann::json& options = nlohmann::json::object(),
                     const nlohmann::json& tools = nlohmann::json::array(),
                     int timeout_ms = 300000);

    // Single turn generation against /api/generate (used for vision).
    // num_gpu: 0 forces CPU (avoids GPU OOM on small cards), -1 lets Ollama decide.
    std::string generate(const std::string& model, const std::string& prompt,
                         const std::string& image_b64 = "",
                         double temperature = 0.7,
                         int timeout_ms = 300000,
                         int num_gpu = 0);

    // Full response JSON from /api/chat (used when tools / metadata needed).
    nlohmann::json chat_raw(const std::string& model, const nlohmann::json& messages,
                            double temperature = 0.7,
                            const nlohmann::json& options = nlohmann::json::object(),
                            const nlohmann::json& tools = nlohmann::json::array(),
                            int timeout_ms = 300000);

    // List installed models -> json array from /api/tags.
    nlohmann::json list_models();

    // Fetch a URL body (helpers for news/tools). Returns empty on failure.
    std::string http_get(const std::string& url, int timeout_ms = 15000);

private:
    std::string base_url_;
};

} // namespace jyotish::ollama