#include <jyotish/http.hpp>

#include <cstdlib>

namespace {

const char* env_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : fallback;
}

} // namespace

int main() {
    jyotish::http::ServerConfig config;
    config.host = env_or("JYOTISH_HOST", "0.0.0.0");
    config.port = std::atoi(env_or("JYOTISH_PORT", "8000"));
    if (config.port <= 0 || config.port > 65535) config.port = 8000;

    jyotish::http::Server server(config);
    server.run();
    return 0;
}