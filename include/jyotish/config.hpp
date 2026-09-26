#pragma once

#include <string>

#include <jyotish/store.hpp>

namespace jyotish {

struct Settings {
    std::string ollama_base_url = "http://localhost:11434";
    std::string chat_model = "qwen2.5:7b-instruct";
    double chat_temperature = 0.7;
    size_t max_upload_mb = 10;
    std::string news_cache_file = "news_cache.json";
    std::string photo_model = "qwen2.5vl:3b";
    int photo_num_gpu = 0;  // 0 = CPU (safer on small GPUs); -1 = let Ollama decide
    std::string cities_file = "cities.tsv";
    std::string famous_file = "famous.json";  // dataset of famous people for the analog engine
    bool fact_check = true;               // run a fact-check pass over each reply
    std::string factcheck_model = "qwen2.5:7b-instruct"; // must be factually reliable
    double factcheck_temperature = 0.0;

    // Live web search ("research" step). Used to ground current-events / factual
    // questions before the law-of-large-numbers analog engine is applied.
    bool web_search = true;               // JYOTISH_WEB_SEARCH=0 disables
    std::string search_provider = "duckduckgo";  // "duckduckgo" | "searx" | "none"
    std::string searx_base_url = "";      // e.g. "https://searx.be" (when provider=searx)
    int search_timeout_ms = 8000;
    int search_max_results = 5;

    // Quotes: only present a source that was verified (curated + Wikipedia check);
    // fabricated attributions in the model reply are stripped.
    bool verify_quote_sources = true;

    // Feedback dataset: "was this useful?" answers paired with (question, answer)
    // are appended here as YYYY-MM-DD.jsonl for future fine-tuning.
    std::string feedback_dir = "/mnt/oracle-data/feedback";

    // Coins/economy & admin (JYOTISH_STORE_PATH, JYOTISH_ADMIN_TOKEN,
    // JYOTISH_ADMIN_USER, JYOTISH_TOPUP_PHONE, JYOTISH_TOPUP_URL).
    std::string store_path = "oracle_store.db";   // SQLite database file
    std::string admin_token = "";                 // authorized for /api/admin/* (empty = disabled)
    std::string admin_user = "albadmin";          // username for /api/admin/login
    std::string topup_phone = "";   // СБП phone — только через env JYOTISH_TOPUP_PHONE (на сервере)
    std::string topup_url = "";                   // optional payment link opened by "put a coin" button
    int free_questions = store::kDefaultFreeLimit;
    int coins_per_pack = store::kDefaultCoinsPerPack;
    int coin_pack_price_rub = store::kDefaultPackPriceRub;
    // Idle sessions (and their wallet codes) are purged once a week when
    // inactive for this many days (JYOTISH_SESSION_TTL_DAYS).
    int session_ttl_days = 60;
    // Hard cap on a single oracle/casual answer, so the model never rambles
    // on longer than a horoscope (~1500 chars RU). JYOTISH_MAX_REPLY_CHARS.
    int max_reply_chars = 1500;
};

Settings& settings();

} // namespace jyotish