#include <jyotish/config.hpp>

#include <cstdlib>

namespace jyotish {

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

} // namespace

Settings& settings() {
    static Settings s = [] {
        Settings d;
        d.ollama_base_url = env_or("OLLAMA_BASE_URL", d.ollama_base_url);
        d.chat_model = env_or("JYOTISH_CHAT_MODEL", d.chat_model);
        d.photo_model = env_or("JYOTISH_PHOTO_MODEL", d.photo_model);
        d.factcheck_model = env_or("JYOTISH_FACTCHECK_MODEL", d.factcheck_model);
        if (const char* fc = std::getenv("JYOTISH_FACT_CHECK")) d.fact_check = std::string(fc) != "0";
        d.search_provider = env_or("JYOTISH_SEARCH_PROVIDER", d.search_provider);
        d.searx_base_url = env_or("JYOTISH_SEARX_URL", d.searx_base_url);
        if (const char* ws = std::getenv("JYOTISH_WEB_SEARCH")) d.web_search = std::string(ws) != "0";
        if (const char* qs = std::getenv("JYOTISH_VERIFY_QUOTES")) d.verify_quote_sources = std::string(qs) != "0";
        d.feedback_dir = env_or("JYOTISH_FEEDBACK_DIR", d.feedback_dir);
        d.store_path = env_or("JYOTISH_STORE_PATH", d.store_path);
        d.admin_token = env_or("JYOTISH_ADMIN_TOKEN", d.admin_token);
        d.admin_user = env_or("JYOTISH_ADMIN_USER", d.admin_user);
        d.topup_phone = env_or("JYOTISH_TOPUP_PHONE", d.topup_phone);
        d.topup_url = env_or("JYOTISH_TOPUP_URL", d.topup_url);
        if (const char* fq = std::getenv("JYOTISH_FREE_QUESTIONS")) {
            int n = std::atoi(fq);
            if (n > 0) d.free_questions = n;
        }
        if (const char* cpp = std::getenv("JYOTISH_COINS_PER_PACK")) {
            int n = std::atoi(cpp);
            if (n > 0) d.coins_per_pack = n;
        }
        if (const char* pr = std::getenv("JYOTISH_COIN_PACK_PRICE_RUB")) {
            int n = std::atoi(pr);
            if (n > 0) d.coin_pack_price_rub = n;
        }
        if (const char* ttl = std::getenv("JYOTISH_SESSION_TTL_DAYS")) {
            int n = std::atoi(ttl);
            if (n > 0) d.session_ttl_days = n;
        }
        if (const char* mrc = std::getenv("JYOTISH_MAX_REPLY_CHARS")) {
            int n = std::atoi(mrc);
            if (n >= 100) d.max_reply_chars = n;
        }
        return d;
    }();
    return s;
}

} // namespace jyotish
