// Ollama HTTP client (real implementation)
#include <jyotish/ollama.hpp>
#include <jyotish/utf8.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <cstdio>

namespace jyotish::ollama {

Client::Client(const std::string& base_url) : base_url_(base_url) {}

nlohmann::json Client::chat_raw(const std::string& model, const nlohmann::json& messages,
                                double temperature, const nlohmann::json& options,
                                const nlohmann::json& tools, int timeout_ms) {
    nlohmann::json payload = {
        {"model", model},
        {"messages", messages},
        {"stream", false},
        {"options", {{"temperature", temperature}}}
    };
    if (!options.is_null() && !options.empty()) {
        for (auto it = options.begin(); it != options.end(); ++it) payload["options"][it.key()] = it.value();
    }
    if (tools.is_array() && !tools.empty()) payload["tools"] = tools;

    // The prompt may embed web snippets / raw history that carry a stranded
    // invalid UTF-8 byte; sanitizing the whole payload keeps type_error.316
    // from aborting the request to the model.
    jyotish::utf8::sanitize(payload);

    httplib::Client cli(base_url_);
    cli.set_connection_timeout(timeout_ms / 1000);
    cli.set_read_timeout(timeout_ms / 1000);
    cli.set_write_timeout(timeout_ms / 1000);

    auto res = cli.Post("/api/chat", payload.dump(), "application/json");
    if (!res) return nlohmann::json{{"error", "connection failed"}};
    if (res->status != 200) return nlohmann::json{{"error", "HTTP " + std::to_string(res->status)}, {"body", res->body}};

    try {
        return nlohmann::json::parse(res->body);
    } catch (...) {
        return nlohmann::json{{"error", "parse error"}};
    }
}

std::string Client::chat(const std::string& model, const nlohmann::json& messages,
                         double temperature, const nlohmann::json& options,
                         const nlohmann::json& tools, int timeout_ms) {
    auto j = chat_raw(model, messages, temperature, options, tools, timeout_ms);
    if (j.contains("error")) return "(Ollama error: " + j["error"].get<std::string>() + ")";
    if (j.contains("message")) return j["message"].value("content", "");
    return "";
}

std::string Client::generate(const std::string& model, const std::string& prompt,
                             const std::string& image_b64, double temperature, int timeout_ms,
                             int num_gpu) {
    nlohmann::json payload = {
        {"model", model},
        {"prompt", prompt},
        {"stream", false},
        {"options", {{"temperature", temperature}, {"num_predict", 512}, {"repeat_penalty", 1.2}}}
    };
    if (num_gpu >= 0) payload["options"]["num_gpu"] = num_gpu;
    if (!image_b64.empty()) payload["images"] = {image_b64};

    jyotish::utf8::sanitize(payload);

    httplib::Client cli(base_url_);
    cli.set_connection_timeout(timeout_ms / 1000);
    cli.set_read_timeout(timeout_ms / 1000);
    cli.set_write_timeout(timeout_ms / 1000);

    auto res = cli.Post("/api/generate", payload.dump(), "application/json");
    if (!res) return "(Ollama error: connection failed)";
    if (res->status != 200) return "(Ollama error: HTTP " + std::to_string(res->status) + ")";

    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("error")) return "(Ollama error: " + j["error"].get<std::string>() + ")";
        return j.value("response", "");
    } catch (...) {
        return "(Ollama error: parse error)";
    }
}

nlohmann::json Client::list_models() {
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(15);
    auto res = cli.Get("/api/tags");
    if (!res || res->status != 200) return nlohmann::json::array();
    try {
        return nlohmann::json::parse(res->body);
    } catch (...) {
        return nlohmann::json::array();
    }
}

std::string Client::http_get(const std::string& url, int timeout_ms) {
    httplib::Client cli("http://localhost:11434");  // placeholder, replaced below
    // Parse scheme/host manually: only http/https with default ports supported here.
    std::string rest = url;
    std::string scheme = "http";
    if (rest.rfind("http://", 0) == 0) {
        rest = rest.substr(7);
    } else if (rest.rfind("https://", 0) == 0) {
        scheme = "https";
        rest = rest.substr(8);
    } else {
        return "";
    }

    size_t slash = rest.find('/');
    std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

    std::unique_ptr<httplib::Client> ucli;
    if (scheme == "https") {
        ucli = std::make_unique<httplib::Client>("https://" + host);
    } else {
        ucli = std::make_unique<httplib::Client>("http://" + host);
    }
    ucli->set_connection_timeout(timeout_ms / 1000);
    ucli->set_read_timeout(timeout_ms / 1000);

    auto res = ucli->Get(path);
    if (!res || res->status != 200) return "";
    return res->body;
}

} // namespace jyotish::ollama