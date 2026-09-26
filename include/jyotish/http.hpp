#pragma once

#include <jyotish/types.hpp>
#include <httplib.h>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace jyotish::http {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8000;
    std::vector<std::string> cors_origins = {
        "http://localhost:5173",
        "http://127.0.0.1:5173"
    };
    size_t max_body_size = 10 * 1024 * 1024;  // 10 MB
};

class Server {
public:
    explicit Server(const ServerConfig& config);
    ~Server();
    
    void run();
    void stop();
    
private:
    ServerConfig config_;
    std::unique_ptr<httplib::Server> server_;

    // Last oracle answer's grounded flag per client (the 👍/👎 feedback
    // endpoint records it together with the rating). Bounded, best-effort.
    std::unordered_map<std::string, bool> grounded_;
    std::mutex mu_grounded_;
    
    void setup_routes();
    void setup_cors();
    
    // Handlers
    void health(const httplib::Request&, httplib::Response&);
    void geocode(const httplib::Request&, httplib::Response&);
    void chart(const httplib::Request&, httplib::Response&);
    void dasha(const httplib::Request&, httplib::Response&);
    void predictions(const httplib::Request&, httplib::Response&);
    void photo(const httplib::Request&, httplib::Response&);
    void transits(const httplib::Request&, httplib::Response&);
    void period(const httplib::Request&, httplib::Response&);
    void chat(const httplib::Request&, httplib::Response&);
    void oracle(const httplib::Request&, httplib::Response&);
    
    // Coins / sessions / payments / manual-answer delivery / health.
    void session(const httplib::Request&, httplib::Response&);
    void balance(const httplib::Request&, httplib::Response&);
    void wallet_my(const httplib::Request&, httplib::Response&);
    void wallet_restore(const httplib::Request&, httplib::Response&);
    void feedback(const httplib::Request&, httplib::Response&);
    void payments_create(const httplib::Request&, httplib::Response&);
    void payments_list(const httplib::Request&, httplib::Response&);
    void manual_poll(const httplib::Request&, httplib::Response&);
    void healthz(const httplib::Request&, httplib::Response&);

    // Admin login (username/password → grants token usage) and admin API
    // (requires X-Admin-Token / Bearer token).
    void admin_login(const httplib::Request&, httplib::Response&);
    void admin_dashboard(const httplib::Request&, httplib::Response&);
    void admin_maintenance(const httplib::Request&, httplib::Response&);
    void admin_topup(const httplib::Request&, httplib::Response&);
    void admin_override(const httplib::Request&, httplib::Response&);
    void admin_payments_approve(const httplib::Request&, httplib::Response&);
    void admin_manual_answer(const httplib::Request&, httplib::Response&);
    void admin_sessions(const httplib::Request&, httplib::Response&);
    void admin_logs(const httplib::Request&, httplib::Response&);
    
    // Helpers
    static nlohmann::json parse_json_body(const httplib::Request& req);
    static Chart::BirthData parse_birth_data(const nlohmann::json& j);
    static void parse_birth_data_helper(Chart::BirthData& birth);
    static void set_cors_headers(httplib::Response& res, const std::string& origin);
};

} // namespace jyotish::http