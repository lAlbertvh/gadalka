#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace jyotish::store {

// Default economy knobs (overridable via env in Settings).
constexpr int kDefaultFreeLimit = 5;
constexpr int kDefaultCoinsPerPack = 10;
constexpr int kDefaultPackPriceRub = 100;

enum class ConsumeResult { Free, Coin, NoFunds };

struct SessionInfo {
    std::string client_id;
    long long coins = 0;
    int free_used = 0;
    int free_limit = kDefaultFreeLimit;
    long long created_at = 0;
    long long last_active = 0;
    // Human-memorable short code for balance recovery on another device,
    // e.g. "K4M-RT7". Guaranteed unique, non-empty after get_or_create_session.
    std::string wallet_code;
};

struct PaymentInfo {
    long long id = 0;
    std::string client_id;
    int coins = 0;
    int rub = 0;
    std::string state;  // pending | approved
    long long created_at = 0;
    long long approved_at = 0;
};

struct ManualItem {
    long long id = 0;
    std::string client_id;
    std::string user_text;
    std::string state;  // pending | answered | delivered
    std::string reply;
    long long created_at = 0;
    long long answered_at = 0;
};

struct LogRow {
    long long id = 0;
    long long ts = 0;
    std::string session_id;
    std::string endpoint;
    int status = 0;
    int ms = 0;
    int coins_spent = 0;
    std::string detail;
    std::string user_text;
    std::string reply;
};

// SQLite-backed persistence: sessions, coin ledger, settings, manual-answer
// queue, payments and a request log. Single-connection, mutex-guarded,
// WAL mode. Designed for one server process.
class Store {
public:
    explicit Store(const std::string& path);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    bool ok() const { return db_ != nullptr; }

    // Sessions & coins.
    SessionInfo session(const std::string& client_id);
    SessionInfo get_or_create_session(const std::string& client_id);
    ConsumeResult consume_question(const std::string& client_id, int free_limit);
    long long credit(const std::string& client_id, int coins, const std::string& note);

    // Wallet ("recovery code") lookup by its short code. Returns an empty
    // SessionInfo when no session owns the code.
    SessionInfo find_by_wallet(const std::string& code);

    // Housekeeping: drop sessions inactive for at least ttl_seconds along with
    // their manual queue / overrides / dialog log. Returns deleted sessions.
    // Coins and payment history are preserved... coins live in sessions, so
    // purged sessions take their coins with them (by design).
    long long purge_stale(long long ttl_seconds);

    // Admin settings (maintenance flag etc.).
    void set_setting(const std::string& key, const std::string& value);
    std::string setting(const std::string& key) const;
    bool maintenance() const;
    void set_maintenance(bool on);

    // Manual moderation: disable the LLM for a session and answer by hand.
    void set_manual_override(const std::string& client_id, bool active);
    bool manual_override(const std::string& client_id) const;
    std::vector<std::string> overridden_sessions();
    long long enqueue_manual(const std::string& client_id, const std::string& user_text);
    void answer_manual(long long id, const std::string& reply);
    std::vector<ManualItem> manual_queue(const std::string& state) const;
    std::vector<ManualItem> take_deliverable(const std::string& client_id);

    // Payments (manual top-up placeholder: admin approves).
    long long create_payment(const std::string& client_id, int coins, int rub);
    bool approve_payment(long long id);
    std::vector<PaymentInfo> payments(const std::string& client_id) const;
    std::vector<PaymentInfo> pending_payments() const;

    // Request log.
    void log(const std::string& session_id, const std::string& endpoint,
             int status, int ms, int coins_spent, const std::string& detail,
             const std::string& user_text, const std::string& reply);
    std::vector<LogRow> recent_logs(int limit) const;
    // The most recent successful oracle turn for a client (used by the
    // 👍/👎 feedback endpoint to re-attach a rating to the last reading).
    std::optional<LogRow> last_oracle_turn(const std::string& client_id) const;

    // Dashboard aggregates.
    std::vector<SessionInfo> sessions(int limit) const;
    long long total_sessions() const;
    long long total_coins_granted() const;
    long long total_coins_spent() const;
    long long questions_last_hours(int hours) const;
    long long approved_revenue_rub() const;
    long long pending_manual_count() const;

    static long long now();

private:
    void init();
    void ensure_connection() const;
    void exec(const std::string& sql) const;
    SessionInfo read_session(const std::string& client_id);

    bool exists_;
    mutable sqlite3* db_ = nullptr;
    std::string path_;
    mutable std::mutex mu_;
};

} // namespace jyotish::store