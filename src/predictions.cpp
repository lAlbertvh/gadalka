#include <jyotish/predictions.hpp>
#include <jyotish/config.hpp>
#include <jyotish/utf8.hpp>
#include <nlohmann/json.hpp>
#include <string>

#ifdef USE_SYSTEM_HTTPLIB
#include <httplib.h>
#else
#include "httplib.h"
#endif

namespace jyotish::predictions {

std::string ollama_chat(const std::vector<nlohmann::json>& messages, double temperature, const std::string& model) {
    const std::string base_url = settings().ollama_base_url;
    const std::string model_name = model.empty() ? settings().chat_model : model;
    
    nlohmann::json payload;
    payload["model"] = model_name;
    payload["messages"] = messages;
    payload["stream"] = false;
    payload["options"] = nlohmann::json::object();
    payload["options"]["temperature"] = temperature;
    
    httplib::Client cli(base_url);
    cli.set_connection_timeout(30);
    cli.set_read_timeout(600);
    cli.set_write_timeout(60);
    
    jyotish::utf8::sanitize(payload);
    std::string dumped = payload.dump();
    auto res = cli.Post("/api/chat", dumped, "application/json");
    if (!res) { return "(Ollama error)"; }
    if (res->status != 200) { return "(Ollama error)"; }
    
    try {
        auto j = nlohmann::json::parse(res->body);
        return j.value("message", nlohmann::json::object()).value("content", "");
    } catch (...) {
        return "(Parse error)";
    }
}

} // namespace jyotish::predictions