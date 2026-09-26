#include <jyotish/http.hpp>
#include <jyotish/core.hpp>
#include <jyotish/dasha.hpp>
#include <jyotish/transits.hpp>
#include <jyotish/forecast.hpp>
#include <jyotish/geocode.hpp>
#include <jyotish/ollama.hpp>
#include <jyotish/config.hpp>
#include <jyotish/store.hpp>
#include <jyotish/predictions.hpp>
#include <jyotish/oracle.hpp>
#include <jyotish/chart_formatter.hpp>
#include <jyotish/prompt_templates.hpp>
#include <jyotish/news.hpp>
#include <jyotish/utf8.hpp>
#include <jyotish/feedback.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace jyotish::http {

namespace {

const std::chrono::steady_clock::time_point kServerStartedAt = std::chrono::steady_clock::now();

jyotish::ollama::Client& ollama() {
    static jyotish::ollama::Client c(settings().ollama_base_url);
    return c;
}

jyotish::store::Store& data_store() {
    static jyotish::store::Store st(settings().store_path);
    if (!st.ok()) {
        fmt::print(stderr, "WARNING: SQLite store unavailable at '{}'\n", settings().store_path);
    }
    return st;
}

// Whether the oracle, in its last message, asked the user a question. The
// answer to such a turn is free — it must never burn a coin or a free slot.
// The appended "[FEEDBACK|…] Был ли ответ полезен?" tail always ends with a
// question mark and would mask every horoscope as "the oracle asked", so it
// is stripped before the check. A "[CONFIRM|…]" echo is NOT treated as an
// oracle question: the "да" that follows confirms the birth data and starts
// the first (billed) reading.
bool assistant_last_question(const std::vector<std::string>& history_messages) {
    auto trim_end = [](std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                              s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    };
    for (auto it = history_messages.rbegin(); it != history_messages.rend(); ++it) {
        auto colon = it->find(':');
        std::string role = colon == std::string::npos ? "user" : it->substr(0, colon);
        if (role == "user") continue;
        std::string content = colon == std::string::npos ? *it : it->substr(colon + 2);
        if (content.find("[CONFIRM|") != std::string::npos) return false;
        content = jyotish::feedback::strip(content);  // drop "[FEEDBACK|…] …?" tail
        content = trim_end(content);
        if (content.empty()) continue;
        return content.back() == '?';
    }
    return false;
}

// Best-effort client IP behind nginx (X-Real-IP) or direct connection.
std::string client_ip(const httplib::Request& req) {
    const std::string xr = req.get_header_value("X-Real-IP");
    if (!xr.empty()) return xr;
    const std::string xff = req.get_header_value("X-Forwarded-For");
    if (!xff.empty()) return xff.substr(0, xff.find(','));
    std::string ra = req.remote_addr;
    auto colon = ra.rfind(':');
    if (colon != std::string::npos) ra = ra.substr(0, colon);
    return ra;
}

// Bruteforce guard for wallet restore: at most 10 attempts per IP per 10 min.
bool wallet_restore_blocked(const std::string& ip) {
    static std::mutex mu;
    static std::unordered_map<std::string, std::vector<long long>> buckets;
    const long long now_s = jyotish::store::Store::now();
    std::lock_guard<std::mutex> lk(mu);
    auto& bucket = buckets[ip];
    bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                [&](long long t) { return now_s - t > 600; }),
                 bucket.end());
    if (bucket.size() >= 10) return true;
    bucket.push_back(now_s);
    return false;
}

// Admin API access control: shared token via X-Admin-Token or Bearer header.
bool admin_authorized(const httplib::Request& req) {
    const std::string expected = settings().admin_token;
    if (expected.empty()) return false;
    std::string got = req.get_header_value("X-Admin-Token");
    if (!got.empty()) return got == expected;
    const std::string auth = req.get_header_value("Authorization");
    if (auth.rfind("Bearer ", 0) == 0) return auth.substr(7) == expected;
    return false;
}

void admin_denied(httplib::Response& res) {
    const bool configured = !settings().admin_token.empty();
    res.status = configured ? 403 : 503;
    res.set_content(
        nlohmann::json{{"detail", configured ? "forbidden" : "admin token not configured on server"},
                       {"code", configured ? "admin_forbidden" : "admin_disabled"}}.dump(),
        "application/json");
}

nlohmann::json balance_json(const std::string& client_id) {
    auto st = data_store().session(client_id);
    return {
        {"coins", st.coins},
        {"free_limit", st.free_limit},
        {"free_used", st.free_used},
        {"free_left", std::max(0, st.free_limit - st.free_used)},
    };
}

nlohmann::json payment_json(const jyotish::store::PaymentInfo& p) {
    nlohmann::json j = {
        {"id", p.id},
        {"client_id", p.client_id},
        {"coins", p.coins},
        {"rub", p.rub},
        {"state", p.state},
        {"created_at", p.created_at},
    };
    j["approved_at"] = p.approved_at == 0 ? nlohmann::json(nullptr)
                                          : nlohmann::json(static_cast<long long>(p.approved_at));
    return j;
}

nlohmann::json manual_json(const jyotish::store::ManualItem& m) {
    return {
        {"id", m.id},
        {"client_id", m.client_id},
        {"user_text", m.user_text},
        {"state", m.state},
        {"reply", m.reply},
        {"created_at", m.created_at},
    };
}

nlohmann::json log_json(const jyotish::store::LogRow& r) {
    return {
        {"id", r.id},
        {"ts", r.ts},
        {"session_id", r.session_id},
        {"endpoint", r.endpoint},
        {"status", r.status},
        {"ms", r.ms},
        {"coins_spent", r.coins_spent},
        {"detail", r.detail},
        {"user_text", r.user_text},
        {"reply", r.reply},
    };
}

nlohmann::json session_json(const jyotish::store::SessionInfo& s) {
    return {
        {"client_id", s.client_id},
        {"coins", s.coins},
        {"free_used", s.free_used},
        {"free_limit", s.free_limit},
        {"created_at", s.created_at},
        {"last_active", s.last_active},
        {"override", data_store().manual_override(s.client_id)},
    };
}

std::string maintenance_msg(bool ru) {
    return ru ? "Оракул на технических работах — вернёмся совсем скоро!"
              : "The oracle is under maintenance — back very soon!";
}

std::string out_of_coins_msg(bool ru) {
    return ru ? "Монетки закончились. Брось монетку в автомат, чтобы спросить ещё раз!"
              : "Out of coins. Drop one into the machine to ask again!";
}

bool has_cjk(const std::string& text) {
    // Decode UTF-8 and look for real CJK codepoints (CJK U+4E00-U+9FFF,
    // hiragana/katakana U+3040-U+30FF, CJK punct U+3000-U+303F, fullwidth U+FF00-U+FFEF,
    // Hangul syllables U+AC00-U+D7AF).
    // Em-dashes & Cyrillic are multi-byte but NOT CJK — those must not match.
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        if (b0 >= 0xE4 && b0 <= 0xE9) return true;          // CJK Unified Ideographs
        if (b0 == 0xE3 && b1 >= 0x80 && b1 <= 0x83) return true; // CJK punct/hiragana/katakana
        if ((b0 >= 0xEA && b0 <= 0xEC) || (b0 == 0xED && b1 <= 0x9F)) return true; // Hangul syllables
        if (b0 == 0xEF && (b1 == 0xBC || b1 == 0xBD)) return true; // fullwidth forms
    }
    return false;
}

bool has_cyrillic(const std::string& text) {
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        if (b0 == 0xD0 && text[i + 1] >= '\x80' && text[i + 1] <= '\xBF') return true;
        if (b0 == 0xD1 && text[i + 1] >= '\x80' && text[i + 1] <= '\xBF') return true;
    }
    return false;
}

// Replace invalid UTF-8 sequences (truncated/overlong/out-of-range bytes that
// an LLM occasionally emits mid-reply, e.g. a dangling 0xE2) with U+FFFD so
// the reply always survives nlohmann::json serialization. Telemetry: an
// invalid byte above would otherwise abort the whole JSON response with
// type_error.316 "invalid UTF-8 byte at index ...".
std::string make_valid_utf8(const std::string& s) { return jyotish::utf8::make_valid(s); }

std::string chat_retry(const std::string& question, const std::string& lang) {
    std::string system = lang == "ru" ? "Ты — ведический оракул датов. Отвечай кратко и тепло, на русском. Не выдумывай планет и дат." 
                                      : "You are a Vedic date oracle. Answer briefly, in English. Do not invent planets or dates.";
    nlohmann::json retry_system = {{"role", "system"}, {"content", system + (lang == "ru" ? " Предыдущий ответ сорвался на другой язык. Перепиши по-русски." : " Your previous reply drifted to another language. Rewrite in English.")}};
    nlohmann::json retry_question = {{"role", "user"}, {"content", question}};
    return predictions::ollama_chat({retry_system, retry_question}, 0.45);
}

// Two-stage photo analysis: a neutral description from the vision model, then a
// Vedic interpretation by the chat model. Returns "" when vision is unavailable.
// Two-stage photo analysis: the vision model produces a neutral, factual
// description; the chat model turns it into a short Jyotish-flavoured reading.
// The vision model is unreliable with rich interpretation prompts (it either
// refuses or invents a face), so we keep stage 1 strictly descriptive.
std::string analyze_photo(const std::string& image_b64, const std::string& lang,
                          const std::string& note = "", const std::string& chart_ctx = "") {
    const bool ru = lang == "ru";
    std::string vision_prompt = ru
        ? "Опиши изображение подробно и нейтрально: что или кто изображено, детали, цвета, обстановка, настроение. Только то, что реально видно, без догадок. 3-5 предложений, по-русски."
        : "Describe the image in detail and neutrally: what or who is shown, details, colours, setting, mood. Only what is actually visible, no guessing. 3-5 sentences, in English.";

    std::string desc;
    for (int attempt = 0; attempt < 2; ++attempt) {
        desc = ollama().generate(settings().photo_model, vision_prompt, image_b64, 0.4, 300000, settings().photo_num_gpu);
        if (desc.rfind("(Ollama error", 0) != 0 && !desc.empty()) break;
        desc.clear();
    }
    if (desc.empty()) return "";

    std::string system = ru
        ? "Ты — ведический астролог-оракул (джйотиш). Тебе дают текстовое описание фотографии — опирайся ТОЛЬКО на него. Начни одной фразой с того, что изображено. Затем дай краткое образное толкование в духе оракула: что этот образ символизирует сейчас (стихии, темперамент, настроение). Трактуй планеты как метафоры; НЕ выдумывай натальную карту, дома и знаки. Не ставь диагнозов, не упоминай, что «не можешь анализировать». 4-5 предложений, по-русски."
        : "You are a Vedic astrologer-oracle (Jyotish). You receive a textual description of a photo — rely ONLY on it. Start with one sentence about what is shown. Then give a short, figurative oracle reading: what this image symbolises right now (elements, temperament, mood). Treat planets as metaphors; do NOT invent a natal chart, houses or signs. No diagnoses, never say you \"cannot analyse\". 4-5 sentences, in English.";
    if (!chart_ctx.empty()) {
        system += ru ? "\n\nНАТАЛЬНАЯ КАРТА (используй как контекст, не выдумывай новых положений):\n" + chart_ctx
                     : "\n\nNATAL CHART (use as context, do not invent new placements):\n" + chart_ctx;
    }

    std::string user = (ru ? "Описание фотографии:\n" : "Photo description:\n") + desc;
    if (!note.empty()) user += (ru ? "\n\nДополнительно от пользователя: " : "\n\nUser note: ") + note;

    nlohmann::json msgs = nlohmann::json::array();
    msgs.push_back({{"role", "system"}, {"content", system}});
    msgs.push_back({{"role", "user"}, {"content", user}});

    std::string interp = ollama().chat(settings().chat_model, msgs, 0.7);
    if (interp.empty() || interp.rfind("(Ollama error", 0) == 0 || has_cjk(interp)) return desc;
    if (!ru && has_cyrillic(interp)) {
        msgs.push_back({{"role", "assistant"}, {"content", interp}});
        msgs.push_back({{"role", "user"}, {"content", "Rewrite that in English only. Do not use any Cyrillic characters."}});
        std::string retry = ollama().chat(settings().chat_model, msgs, 0.5);
        interp = (!retry.empty() && retry.rfind("(Ollama error", 0) != 0 && !has_cyrillic(retry)) ? retry : desc;
    }
    return interp;
}

} // namespace

Server::Server(const ServerConfig& config) : config_(config) {
    server_ = std::make_unique<httplib::Server>();
    setup_cors();
    setup_routes();
}

Server::~Server() = default;

void Server::setup_cors() {
    server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        auto origin = req.get_header_value("Origin");
        if (!origin.empty()) {
            for (const auto& allowed : config_.cors_origins) {
                if (allowed == origin || allowed == "*") {
                    res.set_header("Access-Control-Allow-Origin", origin);
                    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
                    res.set_header("Access-Control-Allow-Credentials", "true");
                    break;
                }
            }
        }
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
}

void Server::setup_routes() {
    server_->Get("/health", [this](const auto& req, auto& res) { health(req, res); });
    server_->Get("/api/geocode", [this](const auto& req, auto& res) { geocode(req, res); });
    
    server_->Post("/api/chart", [this](const auto& req, auto& res) { 
        chart(req, res); 
    });
    server_->Post("/api/dasha", [this](const auto& req, auto& res) { 
        dasha(req, res); 
    });
    server_->Post("/api/predictions", [this](const auto& req, auto& res) { 
        predictions(req, res); 
    });
    server_->Post("/api/photo", [this](const auto& req, auto& res) { 
        photo(req, res); 
    });
    server_->Post("/api/transits", [this](const auto& req, auto& res) { 
        transits(req, res); 
    });
    server_->Post("/api/period", [this](const auto& req, auto& res) {
        period(req, res);
    });
    server_->Post("/api/chat", [this](const auto& req, auto& res) { 
        chat(req, res); 
    });
    server_->Post("/api/oracle", [this](const auto& req, auto& res) { 
        oracle(req, res); 
    });

    server_->Post("/api/session", [this](const auto& req, auto& res) { session(req, res); });
    server_->Get("/api/balance", [this](const auto& req, auto& res) { balance(req, res); });
    server_->Get("/api/wallet/my", [this](const auto& req, auto& res) { wallet_my(req, res); });
    server_->Post("/api/wallet/restore", [this](const auto& req, auto& res) { wallet_restore(req, res); });
    server_->Post("/api/feedback", [this](const auto& req, auto& res) { feedback(req, res); });
    server_->Post("/api/payments", [this](const auto& req, auto& res) { payments_create(req, res); });
    server_->Get("/api/payments", [this](const auto& req, auto& res) { payments_list(req, res); });
    server_->Get("/api/manual", [this](const auto& req, auto& res) { manual_poll(req, res); });
    server_->Get("/healthz", [this](const auto& req, auto& res) { healthz(req, res); });

    // Admin login (username + password -> ok, then use password as token).
    server_->Post("/api/admin/login", [this](const auto& req, auto& res) { admin_login(req, res); });

    // Admin API (X-Admin-Token / Bearer).
    server_->Get("/api/admin/dashboard", [this](const auto& req, auto& res) { admin_dashboard(req, res); });
    server_->Post("/api/admin/maintenance", [this](const auto& req, auto& res) { admin_maintenance(req, res); });
    server_->Post("/api/admin/topup", [this](const auto& req, auto& res) { admin_topup(req, res); });
    server_->Post(R"(/api/admin/sessions/([^/]+)/override)", [this](const auto& req, auto& res) { admin_override(req, res); });
    server_->Post(R"(/api/admin/payments/(\d+)/approve)", [this](const auto& req, auto& res) { admin_payments_approve(req, res); });
    server_->Post(R"(/api/admin/manual/(\d+)/answer)", [this](const auto& req, auto& res) { admin_manual_answer(req, res); });
    server_->Get("/api/admin/sessions", [this](const auto& req, auto& res) { admin_sessions(req, res); });
    server_->Get("/api/admin/logs", [this](const auto& req, auto& res) { admin_logs(req, res); });
    
    server_->set_mount_point("/", "./frontend/dist");  // static files
    server_->set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status >= 400 && !res.body.empty()) return;  // handler already responded
        res.set_content(nlohmann::json{{"detail", "Not found"}, {"code", "not_found"}}.dump(), "application/json");
        res.status = 404;
    });
    server_->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) {
            res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "internal_error"}}.dump(), "application/json");
            res.status = 500;
        }
    });
}

void Server::run() {
    // Weekly housekeeping: drop sessions idle for session_ttl_days (and their
    // wallet recovery codes / manual queue / dialog log). Runs at startup and
    // then every 7 days so "forgetful-" storage never grows forever.
    std::thread([] {
        const long long ttl_sec = static_cast<long long>(settings().session_ttl_days) * 86400LL;
        constexpr long long kWeekSec = 7LL * 86400LL;
        long long sleep_sec = 10;  // short first delay so the DB is warm
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(sleep_sec));
            try {
                long long gone = data_store().purge_stale(ttl_sec);
                if (gone > 0)
                    fmt::print("Session cleanup: purged {} idle sessions\n", gone);
            } catch (const std::exception& e) {
                fmt::print(stderr, "Session cleanup failed: {}\n", e.what());
            } catch (...) {}
            std::fflush(stdout);
            std::fflush(stderr);
            sleep_sec = kWeekSec;
        }
    }).detach();

    std::thread([] {
        static const std::vector<std::string> feeds = {
            "https://lenta.ru/rss",
            "https://tass.ru/rss/v2.xml",
            "https://ria.ru/export/rss2/index.xml",
            "https://feeds.bbci.co.uk/news/world/rss.xml",
        };
        for (;;) {
            try {
                int n = jyotish::news::refresh_news(settings().news_cache_file, feeds);
                fmt::print("News cache refreshed: {} items\n", n);
            } catch (const std::exception& e) {
                fmt::print(stderr, "News refresh failed: {}\n", e.what());
            } catch (...) {}
            std::fflush(stdout);
            std::fflush(stderr);
            std::this_thread::sleep_for(std::chrono::hours(6));
        }
    }).detach();

    fmt::print("Starting Jyotish C++ server on {}:{}...\n", config_.host, config_.port);
    std::fflush(stdout);
    server_->listen(config_.host, config_.port);
}

void Server::stop() {
    server_->stop();
}

void Server::health(const httplib::Request&, httplib::Response& res) {
    auto tags = ollama().list_models();
    bool ollama_ok = !tags.empty();
    nlohmann::json models = nlohmann::json::array();
    if (ollama_ok && tags.contains("models")) {
        for (const auto& m : tags["models"]) models.push_back(m.value("name", ""));
    }
    nlohmann::json j = {
        {"status", "ok"},
        {"ollama", ollama_ok},
        {"models", models}
    };
    res.set_content(j.dump(), "application/json");
}

void Server::healthz(const httplib::Request&, httplib::Response& res) {
    auto& st = data_store();
    nlohmann::json models = nlohmann::json::array();
    bool ollama_ok = false;
    try {
        auto tags = ollama().list_models();
        ollama_ok = !tags.empty();
        if (ollama_ok && tags.contains("models")) {
            for (const auto& m : tags["models"]) models.push_back(m.value("name", ""));
        }
    } catch (...) {}
    long long uptime_s = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - kServerStartedAt).count();
    nlohmann::json j = {
        {"status", st.ok() ? "ok" : "degraded"},
        {"store", st.ok()},
        {"ollama", ollama_ok},
        {"models", models},
        {"maintenance", st.maintenance()},
        {"uptime_s", uptime_s},
        {"stats", {
            {"sessions_total", st.total_sessions()},
            {"coins_granted", st.total_coins_granted()},
            {"coins_spent", st.total_coins_spent()},
            {"questions_24h", st.questions_last_hours(24)},
            {"revenue_rub", st.approved_revenue_rub()},
            {"pending_manual", st.pending_manual_count()},
        }},
    };
    res.status = st.ok() ? 200 : 503;
    res.set_content(j.dump(), "application/json");
}

void Server::session(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    std::string client_id = req.has_param("client_id")
        ? req.get_param_value("client_id")
        : body.value("client_id", std::string());
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    data_store().get_or_create_session(client_id);
    nlohmann::json j = balance_json(client_id);
    j["client_id"] = client_id;
    j["wallet_code"] = data_store().session(client_id).wallet_code;
    j["maintenance"] = data_store().maintenance();
    j["pack"] = {{"coins", settings().coins_per_pack},
                 {"rub", settings().coin_pack_price_rub}};
    j["topup"] = {{"phone", settings().topup_phone},
                  {"url", settings().topup_url},
                  {"coins", settings().coins_per_pack},
                  {"rub", settings().coin_pack_price_rub}};
    res.set_content(j.dump(), "application/json");
}

void Server::wallet_my(const httplib::Request& req, httplib::Response& res) {
    std::string client_id = req.get_param_value("client_id");
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    auto info = data_store().get_or_create_session(client_id);
    res.set_content(nlohmann::json{{"client_id", client_id}, {"wallet_code", info.wallet_code}}.dump(), "application/json");
}

void Server::wallet_restore(const httplib::Request& req, httplib::Response& res) {
    if (wallet_restore_blocked(client_ip(req))) {
        res.status = 429;
        res.set_content(nlohmann::json{{"detail", "Слишком много попыток, попробуйте позже"}, {"code", "too_many_attempts"}}.dump(), "application/json");
        return;
    }
    auto body = parse_json_body(req);
    const std::string code = body.value("code", std::string());
    if (code.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "code required"}, {"code", "missing_code"}}.dump(), "application/json");
        return;
    }
    auto info = data_store().find_by_wallet(code);
    if (info.client_id.empty()) {
        res.status = 404;
        res.set_content(nlohmann::json{{"detail", "Код не найден или уже недоступен"}, {"code", "wallet_not_found"}}.dump(), "application/json");
        return;
    }
    nlohmann::json j = balance_json(info.client_id);
    j["client_id"] = info.client_id;
    j["wallet_code"] = info.wallet_code;
    res.set_content(j.dump(), "application/json");
}

void Server::feedback(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    const std::string client_id = body.value("client_id", std::string());
    const int rating = body.value("rating", 0);
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    if (rating != 1 && rating != -1) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "rating must be 1 or -1"}, {"code", "bad_rating"}}.dump(), "application/json");
        return;
    }
    auto turn = data_store().last_oracle_turn(client_id);
    if (!turn) {
        res.status = 404;
        res.set_content(nlohmann::json{{"detail", "no reading to rate"}, {"code", "no_turn"}}.dump(), "application/json");
        return;
    }
    bool grounded = false;
    {
        std::lock_guard<std::mutex> lk(mu_grounded_);
        auto it = grounded_.find(client_id);
        if (it != grounded_.end()) grounded = it->second;
    }
    jyotish::feedback::record(
        jyotish::settings().feedback_dir,
        (has_cyrillic(turn->reply) || has_cyrillic(turn->user_text)) ? "ru" : "en",
        turn->user_text, turn->reply, rating > 0, jyotish::settings().chat_model, grounded);
    res.set_content(nlohmann::json{{"ok", true}}.dump(), "application/json");
}

void Server::balance(const httplib::Request& req, httplib::Response& res) {
    std::string client_id = req.get_param_value("client_id");
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    nlohmann::json j = balance_json(client_id);
    j["client_id"] = client_id;
    res.set_content(j.dump(), "application/json");
}

void Server::payments_create(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    std::string client_id = req.has_param("client_id")
        ? req.get_param_value("client_id")
        : body.value("client_id", std::string());
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    int coins = body.value("coins", settings().coins_per_pack);
    if (coins <= 0) coins = settings().coins_per_pack;
    int rub = coins * settings().coin_pack_price_rub / settings().coins_per_pack;
    long long id = data_store().create_payment(client_id, coins, rub);
    if (id == 0) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "cannot create payment"}, {"code", "payment_error"}}.dump(), "application/json");
        return;
    }
    nlohmann::json j = {
        {"payment_id", id},
        {"client_id", client_id},
        {"coins", coins},
        {"rub", rub},
        {"state", "pending"},
    };
    res.set_content(j.dump(), "application/json");
}

void Server::payments_list(const httplib::Request& req, httplib::Response& res) {
    std::string client_id = req.get_param_value("client_id");
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : data_store().payments(client_id)) arr.push_back(payment_json(p));
    res.set_content(arr.dump(), "application/json");
}

void Server::manual_poll(const httplib::Request& req, httplib::Response& res) {
    std::string client_id = req.get_param_value("client_id");
    if (client_id.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id required"}, {"code", "missing_client_id"}}.dump(), "application/json");
        return;
    }
    nlohmann::json answers = nlohmann::json::array();
    for (const auto& m : data_store().take_deliverable(client_id)) {
        answers.push_back({{"id", m.id}, {"reply", m.reply}});
    }
    res.set_content(nlohmann::json{{"answers", answers}}.dump(), "application/json");
}

void Server::admin_login(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    std::string user = body.value("username", std::string());
    std::string pass = body.value("password", std::string());
    if (settings().admin_token.empty()) {
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", "admin token not configured on server"},
                                       {"code", "admin_disabled"}}.dump(), "application/json");
        return;
    }
    if (user != settings().admin_user || pass != settings().admin_token) {
        res.status = 403;
        res.set_content(nlohmann::json{{"detail", "wrong credentials"}, {"code", "admin_forbidden"}}.dump(), "application/json");
        return;
    }
    res.set_content(nlohmann::json{{"ok", true}}.dump(), "application/json");
}

void Server::admin_dashboard(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    auto& st = data_store();
    nlohmann::json pending_payments = nlohmann::json::array();
    for (const auto& p : st.pending_payments()) pending_payments.push_back(payment_json(p));
    nlohmann::json manual_items = nlohmann::json::array();
    for (const auto& m : st.manual_queue("pending")) manual_items.push_back(manual_json(m));
    nlohmann::json logs = nlohmann::json::array();
    for (const auto& l : st.recent_logs(30)) logs.push_back(log_json(l));
    nlohmann::json overridden = nlohmann::json::array();
    for (const auto& s : st.overridden_sessions()) overridden.push_back(s);
    nlohmann::json j = {
        {"maintenance", st.maintenance()},
        {"store", st.ok()},
        {"admin_configured", !settings().admin_token.empty()},
        {"sessions_total", st.total_sessions()},
        {"coins_granted", st.total_coins_granted()},
        {"coins_spent", st.total_coins_spent()},
        {"questions_24h", st.questions_last_hours(24)},
        {"revenue_rub", st.approved_revenue_rub()},
        {"pending_manual", st.pending_manual_count()},
        {"pending_payments", pending_payments},
        {"manual_queue", manual_items},
        {"overridden_sessions", overridden},
        {"recent_logs", logs},
    };
    res.set_content(j.dump(), "application/json");
}

void Server::admin_maintenance(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    auto body = parse_json_body(req);
    bool on = body.value("on", false);
    data_store().set_maintenance(on);
    res.set_content(nlohmann::json{{"maintenance", data_store().maintenance()}}.dump(), "application/json");
}

void Server::admin_topup(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    auto body = parse_json_body(req);
    std::string client_id = body.value("client_id", std::string());
    int coins = body.value("coins", 0);
    if (client_id.empty() || coins <= 0) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "client_id and coins > 0 required"}, {"code", "bad_request"}}.dump(), "application/json");
        return;
    }
    data_store().credit(client_id, coins, body.value("note", "admin topup"));
    res.set_content(nlohmann::json{{"client_id", client_id}, {"balance", balance_json(client_id)}}.dump(), "application/json");
}

void Server::admin_override(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    if (req.matches.size() < 2 || !req.matches[1].matched) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "session id required"}, {"code", "bad_request"}}.dump(), "application/json");
        return;
    }
    auto body = parse_json_body(req);
    const std::string client_id = req.matches[1];
    data_store().set_manual_override(client_id, body.value("active", true));
    res.set_content(nlohmann::json{{"client_id", client_id}, {"override", data_store().manual_override(client_id)}}.dump(), "application/json");
}

void Server::admin_payments_approve(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    if (req.matches.size() < 2 || !req.matches[1].matched) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "payment id required"}, {"code", "bad_request"}}.dump(), "application/json");
        return;
    }
    long long id = 0;
    try { id = std::stoll(req.matches[1]); } catch (...) {}
    if (id <= 0 || !data_store().approve_payment(id)) {
        res.status = 404;
        res.set_content(nlohmann::json{{"detail", "pending payment not found"}, {"code", "not_found"}}.dump(), "application/json");
        return;
    }
    res.set_content(nlohmann::json{{"payment_id", id}, {"approved", true}}.dump(), "application/json");
}

void Server::admin_manual_answer(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    if (req.matches.size() < 2 || !req.matches[1].matched) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "question id required"}, {"code", "bad_request"}}.dump(), "application/json");
        return;
    }
    auto body = parse_json_body(req);
    std::string reply = body.value("reply", std::string());
    long long id = 0;
    try { id = std::stoll(req.matches[1]); } catch (...) {}
    if (id <= 0 || reply.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "question id and non-empty reply required"}, {"code", "bad_request"}}.dump(), "application/json");
        return;
    }
    data_store().answer_manual(id, reply);
    res.set_content(nlohmann::json{{"question_id", id}, {"answered", true}}.dump(), "application/json");
}

void Server::admin_sessions(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    int limit = 50;
    if (req.has_param("limit")) {
        try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : data_store().sessions(limit)) arr.push_back(session_json(s));
    res.set_content(arr.dump(), "application/json");
}

void Server::admin_logs(const httplib::Request& req, httplib::Response& res) {
    if (!admin_authorized(req)) { admin_denied(res); return; }
    int limit = 100;
    if (req.has_param("limit")) {
        try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& l : data_store().recent_logs(limit)) arr.push_back(log_json(l));
    res.set_content(arr.dump(), "application/json");
}

void Server::geocode(const httplib::Request& req, httplib::Response& res) {
    std::string q = req.get_param_value("q");
    int limit = 8;
    if (auto it = req.params.find("limit"); it != req.params.end()) {
        try { limit = std::stoi(it->second); } catch (...) {}
    }
    
    nlohmann::json results = nlohmann::json::array();
    if (!q.empty()) {
        for (const auto& c : jyotish::geocode::find_cities(q, limit)) {
            results.push_back({
                {"name", c.name},
                {"latitude", c.latitude},
                {"longitude", c.longitude},
                {"tz_offset", c.tz_offset},
                {"country", c.country}
            });
        }
    }
    res.set_content(results.dump(), "application/json");
}

void Server::chart(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    if (body.is_discarded()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "Invalid JSON"}, {"code", "invalid_json"}}.dump(), "application/json");
        return;
    }
    
    try {
        Chart::BirthData birth = parse_birth_data(body);
        Chart chart = compute_chart(birth);
        
        nlohmann::json j;
        to_json(j, chart);
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        res.status = 422;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "chart_calculation_error"}}.dump(), "application/json");
    }
}

void Server::dasha(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    if (body.is_discarded()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "Invalid JSON"}, {"code", "invalid_json"}}.dump(), "application/json");
        return;
    }
    
    try {
        Chart::BirthData birth = parse_birth_data(body);
        Chart chart = compute_chart(birth);
        DashaTimeline timeline = build_dasha_timeline(birth.birth_date, birth.birth_time, birth.tz_offset, chart);
        
        nlohmann::json j;
        to_json(j["timeline"], timeline);
        to_json(j["chart"], chart);
        
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        res.status = 422;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "chart_calculation_error"}}.dump(), "application/json");
    }
}

void Server::predictions(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    if (body.is_discarded()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "Invalid JSON"}, {"code", "invalid_json"}}.dump(), "application/json");
        return;
    }
    
    try {
        Chart::BirthData birth = parse_birth_data(body);
        std::vector<std::string> areas = body.value("areas", std::vector<std::string>{"general"});
        std::string lang = body.value("lang", "ru");
        
        Chart chart = compute_chart(birth);
        std::string summary = chart_summary(chart, lang);
        
        std::string system;
        if (lang == "ru") {
            system = "Ты — профессиональный ведический астролог (джйотиш). Ты получаешь только фактический расчёт, сделанный точным серверным движком. Анализируй строго по этим данным, не выдумывай положений планет и дат. Пиши на русском, тепло, уважительно, без мистификации: это для личного развития, а не абсолютная судьба. Давай конкретику: какие сферы усилены, в какие периоды (махадаши) что проявляется, какие риски снижать. Используй термины спокойно с расшифровкой. Избегай категоричных негативных утверждений про здоровье и отношения — мягко обозначай тенденции и давай практические советы.";
        } else {
            system = "You are a professional Vedic astrologer (Jyotish). You only receive actual calculations made by a precise server-side engine. Analyse strictly from this data; never invent planetary positions or dates. Write in English, warmly and respectfully, without mystification: this is for personal development, not absolute destiny. Give specifics: which areas are strong, what manifests in which periods (maha-dashas), which risks to mitigate. Use terms calmly with explanations. Avoid categorical negative statements about health and relationships — indicate tendencies softly and give practical advice.";
        }
        system += lang == "ru" ? jyotish::oracle::RISK_BLOCK_RU : jyotish::oracle::RISK_BLOCK_EN;
        
        nlohmann::json user_msg = {
            {"role", "user"}, {"content", fmt::format("НАТАЛЬНАЯ КАРТА:\n{}\n\nТемы:\n- {}\n\nДай развёрнутый прогноз.", summary, areas.empty() ? "общий" : areas[0])}
        };
        nlohmann::json messages_arr = nlohmann::json::array();
        messages_arr.push_back({{"role", "system"}, {"content", system}});
        messages_arr.push_back(user_msg);
        
        std::string prediction = ollama().chat(settings().chat_model, messages_arr, 0.7);
        if (has_cjk(prediction) || prediction.empty() || (lang == "en" && has_cyrillic(prediction))) {
            prediction = chat_retry("", lang);
        }
        
        nlohmann::json j;
        to_json(j["chart"], chart);
        jyotish::localize_chart_json(j["chart"], lang);
        j["prediction"] = prediction;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "ollama_unavailable"}}.dump(), "application/json");
    }
}

void Server::photo(const httplib::Request& req, httplib::Response& res) {
    if (data_store().maintenance()) {
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", maintenance_msg(true)}, {"code", "maintenance"}}.dump(), "application/json");
        return;
    }
    if (!req.has_file("file")) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "No file uploaded"}, {"code", "no_file"}}.dump(), "application/json");
        return;
    }
    
    const auto& file = req.get_file_value("file");
    if (file.content.size() > config_.max_body_size) {
        res.status = 413;
        res.set_content(nlohmann::json{{"detail", "File too large"}, {"code", "upload_too_large"}}.dump(), "application/json");
        return;
    }
    auto field = [&req](const std::string& key) -> std::string {
        if (req.has_file(key)) {
            const auto& f = req.get_file_value(key);
            if (f.filename.empty()) return f.content;
        }
        return req.get_param_value(key);
    };
    std::string lang = field("lang");
    if (lang.empty()) lang = "ru";
    std::string note = field("note");

    // Optional birth data -> natal chart summary used as extending context.
    std::string chart_ctx;
    {
        auto num = [&](const char* k) { try { return std::stod(field(k)); } catch (...) { return 0.0; } };
        Chart::BirthData birth;
        birth.birth_date = field("birth_date");
        birth.birth_time = field("birth_time");
        birth.city = field("city");
        birth.latitude = num("latitude");
        birth.longitude = num("longitude");
        birth.tz_offset = num("tz_offset");
        if (!birth.birth_date.empty() && !birth.birth_time.empty()) {
            try {
                parse_birth_data_helper(birth);
                Chart chart = compute_chart(birth);
                chart_ctx = chart_summary(chart, lang);
            } catch (...) { chart_ctx.clear(); }
        }
    }
    
    // Convert to base64
    std::string b64;
    b64.resize((file.content.size() + 2) / 3 * 4);
    
    static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_i = 0;
    size_t b64_j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(file.content.data());
    size_t len = file.content.size();
    
    while (len--) {
        char_array_3[b64_i++] = *(bytes++);
        if (b64_i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (size_t k = 0; k < 4; k++) b64[b64_j++] = b64_chars[char_array_4[k]];
            b64_i = 0;
        }
    }
    
    if (b64_i) {
        for (size_t k = b64_i; k < 3; k++) char_array_3[k] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (size_t k = 0; k < b64_i + 1; k++) b64[b64_j++] = b64_chars[char_array_4[k]];
        while (b64_j % 4) b64[b64_j++] = '=';
    }
    
    std::string analysis;
    try {
        analysis = analyze_photo(b64, lang, note, chart_ctx);
    } catch (...) {}
    if (analysis.empty()) {
        analysis = lang == "ru"
            ? "(Vision-модель временно недоступна. Попробуйте позже.)"
            : "(Vision model temporarily unavailable. Please try again later.)";
    }
    
    nlohmann::json j = {
        {"analysis", analysis},
        {"filename", file.filename}
    };
    res.set_content(j.dump(), "application/json");
}

void Server::transits(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    if (body.is_discarded()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "Invalid JSON"}, {"code", "invalid_json"}}.dump(), "application/json");
        return;
    }
    
    try {
        Chart::BirthData birth = parse_birth_data(body);
        std::string target_date = body.at("target_date");
        std::string target_time = body.value("target_time", "12:00");
        double target_tz = body.value("target_tz_offset", birth.tz_offset);
        std::vector<std::string> situations = body.value("situations", std::vector<std::string>{"general"});
        std::string question = body.value("question", "");
        std::string lang = body.value("lang", "ru");
        
        Chart chart = compute_chart(birth);
        auto timeline = build_dasha_timeline(birth.birth_date, birth.birth_time, birth.tz_offset, chart);
        auto period = dasha_at_date(timeline, target_date);
        TransitSnapshot snap = compute_transit_snapshot(chart, target_date, target_time, target_tz);
        auto localized = localize_transits(snap, lang);
        std::string tb = transit_block(localized, period, target_tz, lang);
        
        std::string summary = chart_summary(chart, lang);
        std::string system;
        if (lang == "ru") {
            system = "Ты — профессиональный ведический астролог (джйотиш), делающий прогноз на конкретный момент времени. Ты получаешь только фактический расчёт (нат. карта и транзиты на дату). Анализируй строго по этим данным, не выдумывай. Пиши на русском, тепло и конкретно: благоприятна ли тенденция, что усилить и чего избегать. Мягко обозначай риски. Говори вероятностями и направлениями.";
        } else {
            system = "You are a professional Vedic astrologer (Jyotish) making a forecast for a specific moment. You only receive actual calculations (natal chart + transits for the date). Analyse strictly from this data, never invent. Write in English, warmly and concretely: is the tendency favourable, what to strengthen and what to avoid. Softly mark risks. Speak in probabilities and directions.";
        }
        system += lang == "ru" ? jyotish::oracle::RISK_BLOCK_RU : jyotish::oracle::RISK_BLOCK_EN;
        
        auto news_items = jyotish::news::load_news(settings().news_cache_file, 8);
        std::string news_str = jyotish::news::news_block(news_items, lang);
        if (!news_str.empty()) system += "\n" + news_str;
        
        std::string user_prompt = fmt::format(
            "НАТАЛЬНАЯ КАРТА:\n{}\n\n{}\n\nТемы:\n- {}\n{}",
            summary, tb,
            situations.empty() ? "общий" : situations[0],
            question.empty() ? "" : ("Вопрос: " + question));
        nlohmann::json pred_msgs = nlohmann::json::array();
        pred_msgs.push_back({{"role", "system"}, {"content", system}});
        pred_msgs.push_back({{"role", "user"}, {"content", user_prompt}});
        std::string prediction = ollama().chat(settings().chat_model, pred_msgs, 0.7);
        if (has_cjk(prediction) || prediction.empty() || (lang == "en" && has_cyrillic(prediction))) prediction = chat_retry(question, lang);
        
        nlohmann::json j;
        to_json(j["chart"], chart);
        to_json(j["transits"], localized);
        if (period) j["period"] = {{"mahadasha", planet_name(period->mahadasha)},
                                   {"maha_start", period->maha_start},
                                   {"maha_end", period->maha_end},
                                   {"antardasha", period->antardasha ? planet_name(*period->antardasha) : ""},
                                   {"ad_start", period->ad_start.value_or("")},
                                   {"ad_end", period->ad_end.value_or("")}};
        j["prediction"] = prediction;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "ollama_unavailable"}}.dump(), "application/json");
    }
}

void Server::period(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    if (body.is_discarded()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "Invalid JSON"}, {"code", "invalid_json"}}.dump(), "application/json");
        return;
    }

    try {
        std::string date = body.value("date", std::string());
        if (date.empty()) date = body.value("target_date", std::string());
        std::string time = body.value("time", "12:00");
        double tz_offset = body.value("tz_offset", 0.0);
        std::string lang = body.value("lang", "ru");
        std::string lagna = body.value("lagna", std::string());

        if (date.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"detail", "date is required"}, {"code", "missing_date"}}.dump(), "application/json");
            return;
        }

        jyotish::forecast::PeriodForecast forecast =
            jyotish::forecast::compute_period_forecast(date, time, tz_offset, lang);

        nlohmann::json j;
        jyotish::forecast::to_json(j, forecast);

        if (!lagna.empty()) {
            const std::string want = jyotish::oracle::lower_name(lagna);
            nlohmann::json filtered = nlohmann::json::array();
            for (const auto& item : j["signs"]) {
                const std::string en = jyotish::oracle::lower_name(item["lagna"].get<std::string>());
                const std::string loc = jyotish::oracle::lower_name(item["lagna_local"].get<std::string>());
                if (en == want || loc == want) { filtered.push_back(item); }
            }
            if (filtered.empty()) {
                res.status = 404;
                res.set_content(nlohmann::json{{"detail", "unknown lagna sign"}, {"code", "unknown_lagna"}}.dump(), "application/json");
                return;
            }
            j["signs"] = std::move(filtered);
        }

        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "period_calculation_error"}}.dump(), "application/json");
    }
}

void Server::chat(const httplib::Request& req, httplib::Response& res) {
    auto body = parse_json_body(req);
    if (body.is_discarded()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "Invalid JSON"}, {"code", "invalid_json"}}.dump(), "application/json");
        return;
    }
    
    std::string client_id = body.value("client_id", std::string());
    jyotish::store::ConsumeResult billed = jyotish::store::ConsumeResult::NoFunds;
    const auto t0 = std::chrono::steady_clock::now();
    auto ms_since = [&t0]() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    };
    
    try {
        if (data_store().maintenance()) {
            res.status = 503;
            res.set_content(nlohmann::json{{"detail", maintenance_msg(true)}, {"code", "maintenance"}}.dump(), "application/json");
            return;
        }
        billed = data_store().consume_question(client_id, settings().free_questions);
        if (billed == jyotish::store::ConsumeResult::NoFunds) {
            res.status = 403;
            res.set_content(nlohmann::json{{"detail", out_of_coins_msg(true)}, {"code", "out_of_coins"}}.dump(), "application/json");
            return;
        }
        auto birth = parse_birth_data(body["birth"]);
        auto messages = body["messages"];
        std::string lang = body.value("lang", "ru");
        
        Chart chart = compute_chart(birth);
        std::string summary = chart_summary(chart, lang);
        
        std::string system;
        if (lang == "ru") {
            system = "Ты — ведический астролог-оракул (джйотиш). У тебя есть точная натальная карта человека (ниже). Отвечай тепло, живо и по существу, как опытный мудрый консультант. Ссылайся на реальные положения из карты, но не выдумывай новых. Дели вероятности и направления: «склоняйтесь к…», «это время поощряет…». Учитывай историю диалога. Санскритские термины сразу расшифровывай.";
        } else {
            system = "You are a Vedic astrologer-oracle (Jyotish). You have a precise natal chart of the person (below). Answer warmly, vividly and to the point, like a wise consultant. Reference the actual chart positions but never invent new ones. Speak in probabilities and directions: \"tend to…\", \"this time encourages…\". Take the dialogue history into account. Immediately explain Sanskrit terms.";
        }
        system += lang == "ru" ? jyotish::oracle::RISK_BLOCK_RU : jyotish::oracle::RISK_BLOCK_EN;
        system += (lang == "ru" ? "\n\nНАТАЛЬНАЯ КАРТА:\n" : "\n\nNATAL CHART:\n") + summary;
        
        nlohmann::json full = nlohmann::json::array();
        full.push_back({{"role", "system"}, {"content", system}});
        for (const auto& m : messages) {
            full.push_back({{"role", m.value("role", "user")}, {"content", m.value("content", "")}});
        }
        
        std::string reply = ollama().chat(settings().chat_model, full, 0.7);
        if (has_cjk(reply) || reply.empty() || (lang == "en" && has_cyrillic(reply))) {
            nlohmann::json last = full.back();
            nlohmann::json retry_msg = {{"role", "system"}, {"content", system + (lang == "ru" ? " Отвечай ТОЛЬКО на русском." : " Reply ONLY in English.")}};
        nlohmann::json last_user = {{"role", "user"}, {"content", last.value("content", "")}};
            reply = predictions::ollama_chat({retry_msg, last_user}, 0.55);
        }
        
        nlohmann::json j = {{"reply", reply}};
        j["balance"] = balance_json(client_id);
        res.set_content(j.dump(), "application/json");
        {
            std::string user_text;
            for (const auto& m : messages) {
                if (m.value("role", "user") == "user") user_text = m.value("content", "");
            }
            data_store().log(client_id, "chat", 200, ms_since(),
                             billed == jyotish::store::ConsumeResult::Coin ? 1 : 0,
                             "", user_text, reply);
        }
    } catch (const std::exception& e) {
        if (billed == jyotish::store::ConsumeResult::Coin && !client_id.empty()) {
            data_store().credit(client_id, 1, "refund (chat error)");
        }
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "ollama_unavailable"}}.dump(), "application/json");
    }
}

void Server::oracle(const httplib::Request& req, httplib::Response& res) {
    auto field = [&req](const std::string& key) -> std::string {
        if (req.has_file(key)) {
            const auto& f = req.get_file_value(key);
            if (f.filename.empty()) return f.content;
        }
        return req.get_param_value(key);
    };
    
    std::string question = field("question");
    std::string lang = field("lang");
    if (lang.empty()) lang = "ru";
    std::string history = field("history");
    std::string photo_context = field("photo_context");
    
    if (question.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"detail", "question required"}, {"code", "missing_question"}}.dump(), "application/json");
        return;
    }
    
    std::string client_id = field("client_id");
    jyotish::store::ConsumeResult billed = jyotish::store::ConsumeResult::NoFunds;
    const auto t0 = std::chrono::steady_clock::now();
    auto ms_since = [&t0]() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    };
    
    try {
        if (data_store().maintenance()) {
            res.status = 503;
            res.set_content(nlohmann::json{{"detail", maintenance_msg(lang == "ru")}, {"code", "maintenance"}}.dump(), "application/json");
            return;
        }
        // Decode history into messages
        auto history_messages = jyotish::oracle::split_history(history);
        std::vector<std::string> user_texts;
        for (const auto& m : history_messages) {
            auto split = m.find(':');
            if (split != std::string::npos && m.compare(0, 4, "user") == 0) {
                user_texts.push_back(m.substr(split + 2));
            }
        }
        user_texts.push_back(question);
        
        auto info = jyotish::oracle::gather_birth(user_texts);
        bool ready = jyotish::oracle::birth_is_ready(info);

        // Feedback hook: if the last assistant message carried a "[FEEDBACK..]"
        // ask and the user answered with a *pure* rating ("да", "нет, не помог"),
        // record the (question, answer) pair to the dataset and answer with a
        // short ack. A message that only *starts* like a rating but continues
        // into a real question ("да, а расскажи кого мне лучше полюбить?") is
        // NOT a rating — it falls through and gets a full answer below, so the
        // feedback hook can never eat a question. This runs before the
        // confirmation gate, so a rating "да" is never mistaken for a
        // birth-data confirmation.
        {
            bool grounded = false;
            if (jyotish::feedback::pending(history_messages, &grounded)) {
                int rating = jyotish::feedback::classify(question, lang);
                if (rating != 0 && jyotish::feedback::is_pure_rating(question, lang)) {
                    auto pair = jyotish::feedback::pair_for_last_ask(history_messages);
                    if (pair) {
                        jyotish::feedback::record(
                            jyotish::settings().feedback_dir, lang,
                            pair->first, pair->second,
                            rating > 0, jyotish::settings().chat_model, grounded);
                    }
                    nlohmann::json j;
                    j["reply"] = (rating > 0)
                        ? (lang == "ru"
                               ? "Записал — спасибо за оценку! Это поможет мне отвечать точнее."
                               : "Noted — thanks for the rating! It will help me answer more precisely.")
                        : (lang == "ru"
                               ? "Понял, учту. Спасибо, что сказали!"
                               : "Got it, I'll keep that in mind. Thanks for telling me!");
                    j["ready"] = true;
                    j["confirm_pending"] = false;
                    j["birth_status"] = {
                        {"name", info.name},
                        {"birth_date", info.birth_date},
                        {"birth_time", info.birth_time},
                        {"city", info.city}
                    };
                    j["photo_context"] = photo_context;
                    res.set_content(j.dump(), "application/json");
                    return;
                }
            }
        }
        
        // Handle photo upload within this request
        if (req.has_file("photo") && !req.get_file_value("photo").content.empty()) {
            const auto& file = req.get_file_value("photo");
            std::string b64;
            b64.resize((file.content.size() + 2) / 3 * 4);
            static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            size_t b64_i = 0, b64_j = 0;
            unsigned char a3[3], a4[4];
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(file.content.data());
            size_t len = file.content.size();
            while (len--) {
                a3[b64_i++] = *(bytes++);
                if (b64_i == 3) {
                    a4[0] = (a3[0] & 0xfc) >> 2;
                    a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
                    a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
                    a4[3] = a3[2] & 0x3f;
                    for (size_t k = 0; k < 4; k++) b64[b64_j++] = b64_chars[a4[k]];
                    b64_i = 0;
                }
            }
            if (b64_i) {
                for (size_t k = b64_i; k < 3; k++) a3[k] = '\0';
                a4[0] = (a3[0] & 0xfc) >> 2;
                a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
                a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
                a4[3] = a3[2] & 0x3f;
                for (size_t k = 0; k < b64_i + 1; k++) b64[b64_j++] = b64_chars[a4[k]];
                while (b64_j % 4) b64[b64_j++] = '=';
            }
            try {
                std::string reply = analyze_photo(b64, lang);
                if (!reply.empty()) photo_context = make_valid_utf8(reply);
            } catch (...) {}
        }
        
        if (!ready) {
            auto st0 = jyotish::oracle::birth_status(info);
            nlohmann::json bs = {
                {"name", st0.name},
                {"birth_date", st0.birth_date},
                {"birth_time", st0.birth_time},
                {"city", st0.city}
            };
            // A birth-date attempt in the future ("35.04.2380") can't be used
            // for a chart — answer the "посланник из будущего" message instead
            // of the generic "теперь подскажите дату" prompt.
            if (auto future_year = jyotish::oracle::future_year_in_text(question)) {
                nlohmann::json j;
                j["reply"] = make_valid_utf8(
                    jyotish::oracle::future_date_reply(info, lang, *future_year));
                j["ready"] = false;
                j["birth_status"] = bs;
                j["photo_context"] = photo_context;
                res.set_content(j.dump(), "application/json");
                return;
            }
            if (jyotish::oracle::is_casual_question(question, lang, &info)) {
                std::string reply = jyotish::oracle::casual_chat(question, lang);
                nlohmann::json j;
                j["reply"] = make_valid_utf8(reply);
                j["ready"] = false;
                j["birth_status"] = bs;
                j["photo_context"] = photo_context;
                res.set_content(j.dump(), "application/json");
            } else {
                nlohmann::json j;
                j["reply"] = jyotish::oracle::onboarding_reply(info, lang);
                j["ready"] = false;
                j["birth_status"] = bs;
                j["photo_context"] = photo_context;
                res.set_content(j.dump(), "application/json");
            }
            return;
        }

        // Confirmation gate: echo the gathered birth data back and wait for an
        // explicit "yes" before running the analysis, so a data-entry mistake
        // can be caught before a (potentially wrong) horoscope is computed.
        auto confirm = jyotish::oracle::confirm_birth(question, history_messages, info, lang);
        if (confirm.state == jyotish::oracle::ConfirmState::NeedConfirm) {
            auto st0 = jyotish::oracle::birth_status(info);
            nlohmann::json j;
            j["reply"] = confirm.reply;
            j["ready"] = false;
            j["confirm_pending"] = true;
            j["birth_status"] = {
                {"name", st0.name},
                {"birth_date", st0.birth_date},
                {"birth_time", st0.birth_time},
                {"city", st0.city}
            };
            j["photo_context"] = photo_context;
            res.set_content(j.dump(), "application/json");
            return;
        }
        
        auto birth = jyotish::oracle::build_birth_data(info);
        parse_birth_data_helper(birth);
        Chart chart = compute_chart(birth);

        // ---- Billing: a real horoscope turn consumes one free slot or a coin.
        // Setup, onboarding, confirmation and feedback ack turns are free, so
        // users never burn credits before their first actual reading. So is an
        // answer to a question the oracle itself asked ("Как вас зовут?",
        // "Был ли ответ полезен?") — only the user's own new questions bill.
        const bool free_oracle_answer =
            assistant_last_question(history_messages) && question.size() < 200;
        if (data_store().manual_override(client_id)) {
            data_store().enqueue_manual(client_id, question);
            data_store().log(client_id, "oracle.manual", 200, ms_since(), 0, "", question, "");
            nlohmann::json j;
            j["reply"] = lang == "ru" ? "Немного подождите — я отвечу вам сама."
                                      : "One moment — I will answer you personally.";
            j["ready"] = true;
            j["confirm_pending"] = false;
            j["manual_pending"] = true;
            j["balance"] = balance_json(client_id);
            res.set_content(j.dump(), "application/json");
            return;
        }
        billed = free_oracle_answer
            ? jyotish::store::ConsumeResult::Free
            : data_store().consume_question(client_id, settings().free_questions);
        if (billed == jyotish::store::ConsumeResult::NoFunds) {
            res.status = 403;
            data_store().log(client_id, "oracle", 403, ms_since(), 0, "out_of_coins", question, "");
            res.set_content(nlohmann::json{
                                {"detail", out_of_coins_msg(lang == "ru")},
                                {"code", "out_of_coins"},
                                {"balance", balance_json(client_id)}}.dump(),
                            "application/json");
            return;
        }
        
        std::vector<nlohmann::json> history_json;
        for (const auto& m : history_messages) {
            auto colon = m.find(':');
            std::string role = colon == std::string::npos ? "user" : m.substr(0, colon);
            std::string content = colon == std::string::npos ? m : m.substr(colon + 2);
            auto mpos = content.find("\n[CONFIRM|");   // do not leak confirmation markers to the LLM
            if (mpos != std::string::npos) content = content.substr(0, mpos);
            content = jyotish::feedback::strip(content);  // do not leak feedback markers either
            history_json.push_back({{"role", role}, {"content", make_valid_utf8(content)}});
        }
        
        // A confirmation echo ("Я понял так: ... [CONFIRM|...]") is not a real
        // assistant reply: the first actual horoscope answer is still first.
        bool first_reply = true;
        for (const auto& m : history_messages) {
            auto colon = m.find(':');
            std::string role = colon == std::string::npos ? "user" : m.substr(0, colon);
            std::string content = colon == std::string::npos ? m : m.substr(colon + 2);
            if (role == "assistant" && content.find("[CONFIRM|") == std::string::npos) {
                first_reply = false;
                break;
            }
        }
        // A freshly confirmed birth means the horoscope intro starts NOW, even
        // if stale browser history already contained an unmarked reply.
        if (confirm.just_confirmed) first_reply = true;
        
        nlohmann::json profile = nlohmann::json::object();
        if (!info.name.empty()) profile["name"] = info.name;
        if (!info.gender.empty()) {
            profile["gender"] = (lang == "ru")
                ? (info.gender == "male" ? "мужской" : "женский")
                : info.gender;
        }
        // A bare confirmation ("да", "верно", "подтверждаю") is not a real
        // question — make it explicit for the LLM so it starts the horoscope
        // intro instead of answering the word "да".
        std::string llm_question = question;
        // A bare confirmation ("да", "верно", "подтверждаю") is not a real
        // question — make it explicit for the LLM so it starts the horoscope
        // intro instead of answering the word "да". This must hold even when a
        // stale browser history (a real assistant reply without the [CONFIRM|
        // marker) made first_reply=false — just_confirmed detects the "да"
        // directly from THIS message.
        if (first_reply || confirm.just_confirmed) {
            static const char* ok_tokens[] = {"да", "верно", "подтверждаю", "правильно",
                                              "именно", "точно", "согласен", "согласна", "yes", "ok", "okay", "угу", "ага"};
            std::string low_q = question;
            std::transform(low_q.begin(), low_q.end(), low_q.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            bool affirmative = false;
            for (const char* tok : ok_tokens) if (low_q.find(tok) != std::string::npos) { affirmative = true; break; }
            if (affirmative && low_q.find("нет") == std::string::npos) {
                llm_question = lang == "ru"
                    ? "Данные подтверждены. Начни мой гороскоп: представься знакомством — восходящий знак, Луна, ключевые планеты."
                    : "Data confirmed. Start my horoscope: introduce me with a first reading — rising sign, Moon, key planets.";
            }
        }
        auto [reply, chat_info, raw_prompt] = jyotish::oracle::oracle_chat(
            birth, chart, chart, history_json, llm_question, photo_context, first_reply, lang, profile);
        
        nlohmann::json j;
        j["reply"] = make_valid_utf8(reply)
            + jyotish::feedback::ask(lang, chat_info.grounded);
        j["ready"] = true;
        j["confirm_pending"] = false;
        auto st = jyotish::oracle::birth_status(info);
        j["birth_status"] = {{"name", st.name}, {"birth_date", st.birth_date}, {"birth_time", st.birth_time}, {"city", st.city}};
        // When the user is (re)stating birth facts, the "Гороскоп на …" header
        // must reflect the CONFIRMED birth data, never a question-parse date.
        bool birth_talk =
            question.find("родил") != std::string::npos || question.find("рожд") != std::string::npos ||
            question.find("born") != std::string::npos || question.find("birth") != std::string::npos;
        std::string det_date = birth_talk ? birth.birth_date : chat_info.found_date.value_or("");
        std::string det_time = birth_talk ? birth.birth_time : chat_info.found_time.value_or("");
        j["detected"] = {{"date", det_date},
                         {"time", det_time},
                         {"situations", chat_info.situations}};
        to_json(j["chart"], chart);
        jyotish::localize_chart_json(j["chart"], lang);
        if (chat_info.period) {
            j["period"] = nlohmann::json{{"mahadasha", planet_name(chat_info.period->mahadasha)},
                                         {"maha_start", chat_info.period->maha_start},
                                         {"maha_end", chat_info.period->maha_end}};
        }
        if (chat_info.transits) {
            nlohmann::json tj;
            to_json(tj, *chat_info.transits);
            jyotish::localize_transit_json(tj, lang);
            j["transits"] = tj;
        } else {
            // No date in the question: still surface today's real transit sky
            // (planets over the natal chart) so the frontend can show it.
            auto now = std::chrono::system_clock::now();
            std::time_t now_t = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm = *std::gmtime(&now_t);
            char today_buf[11];
            std::strftime(today_buf, sizeof(today_buf), "%Y-%m-%d", &now_tm);
            std::string today_str = today_buf;
            try {
                auto snap = jyotish::compute_transit_snapshot(chart, today_str, "12:00", birth.tz_offset);
                nlohmann::json tj;
                to_json(tj, snap);
                jyotish::localize_transit_json(tj, lang);
                j["transits"] = tj;
            } catch (...) {}
        }
        j["photo_context"] = photo_context;
        j["analogs"] = chat_info.analogs;
        j["balance"] = balance_json(client_id);
        
        // A stranded invalid UTF-8 byte in ANY field (reply, situations,
        // analogs, transits, profile …) would otherwise abort the whole
        // response with nlohmann type_error.316 — the exact error that killed
        // the Duma-2026 answer. Sanitize every string deep in the JSON before
        // serialization so a bad byte never destroys the reply.
        jyotish::utf8::sanitize(j);
        res.set_content(j.dump(), "application/json");
        data_store().log(client_id, "oracle", 200, ms_since(),
                         billed == jyotish::store::ConsumeResult::Coin ? 1 : 0,
                         "", question, make_valid_utf8(reply));
        {
            std::lock_guard<std::mutex> lk(mu_grounded_);
            grounded_[client_id] = chat_info.grounded;
        }
    } catch (const std::exception& e) {
        if (billed == jyotish::store::ConsumeResult::Coin && !client_id.empty()) {
            data_store().credit(client_id, 1, "refund (oracle error)");
        }
        data_store().log(client_id, "oracle", 503, ms_since(), 0, e.what(), question, "");
        res.status = 503;
        res.set_content(nlohmann::json{{"detail", e.what()}, {"code", "ollama_unavailable"}}.dump(), "application/json");
    }
}

void Server::parse_birth_data_helper(Chart::BirthData& birth) {
    // Resolve coordinates if only a city name is present.
    if (birth.latitude == 0.0 && birth.longitude == 0.0 && !birth.city.empty()) {
        if (auto city = jyotish::geocode::resolve_city(birth.city)) {
            birth.latitude = city->latitude;
            birth.longitude = city->longitude;
            birth.tz_offset = city->tz_offset;
        }
    }
}

nlohmann::json Server::parse_json_body(const httplib::Request& req) {
    try {
        return nlohmann::json::parse(req.body);
    } catch (...) {
        return nlohmann::json::parse("{}", nullptr, false);
    }
}

Chart::BirthData Server::parse_birth_data(const nlohmann::json& j) {
    Chart::BirthData birth;
    birth.name = j.value("name", "");
    birth.birth_date = j.value("birth_date", "");
    birth.birth_time = j.value("birth_time", "");
    birth.latitude = j.value("latitude", 0.0);
    birth.longitude = j.value("longitude", 0.0);
    birth.tz_offset = j.value("tz_offset", 0.0);
    birth.city = j.value("city", "");
    return birth;
}

void Server::set_cors_headers(httplib::Response& res, const std::string& origin) {
    res.set_header("Access-Control-Allow-Origin", origin);
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.set_header("Access-Control-Allow-Credentials", "true");
}

} // namespace jyotish::http