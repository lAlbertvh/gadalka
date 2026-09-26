#include <jyotish/store.hpp>

#include <sqlite3.h>

#include <chrono>
#include <cstring>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace jyotish::store {

namespace {

[[noreturn]] void throw_sql(sqlite3* db, const std::string& what) {
    throw std::runtime_error(what + ": " + (db ? sqlite3_errmsg(db) : "no connection"));
}

class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st_, nullptr) != SQLITE_OK)
            throw_sql(db, "prepare: " + sql);
    }
    ~Stmt() { if (st_) sqlite3_finalize(st_); }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    sqlite3_stmt* raw() const { return st_; }

    void bind(int idx, const std::string& v) {
        if (sqlite3_bind_text(st_, idx, v.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throw_sql(db_, "bind text");
    }
    void bind(int idx, long long v) {
        if (sqlite3_bind_int64(st_, idx, v) != SQLITE_OK) throw_sql(db_, "bind int64");
    }
    void bind_null(int idx) {
        if (sqlite3_bind_null(st_, idx) != SQLITE_OK) throw_sql(db_, "bind null");
    }

    bool step() {
        int rc = sqlite3_step(st_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw_sql(db_, "step");
    }
    void run() {
        if (sqlite3_step(st_) != SQLITE_DONE) throw_sql(db_, "run");
    }

    long long col_int(int c) const { return sqlite3_column_int64(st_, c); }
    long long col_int64(int c) const { return sqlite3_column_int64(st_, c); }
    std::string col_text(int c) const {
        const unsigned char* t = sqlite3_column_text(st_, c);
        if (!t) return {};
        return reinterpret_cast<const char*>(t);
    }

private:
    sqlite3* db_;
    sqlite3_stmt* st_ = nullptr;
};

int parse_int_env_or(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (!v || !*v) return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

// Short recovery-code alphabet: unambiguous (no I/O/0/1), uppercased.
// Codes are 6 chars stored without separators and shown as "XXX-XXX".
constexpr const char kWcAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
constexpr size_t kWcLen = 6;

std::string normalize_wallet(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out.push_back(c);
    }
    return out;
}

// Pretty "K4M-RT7" form of a stored 6-char code.
std::string format_wallet(const std::string& raw) {
    if (raw.size() != kWcLen) return raw;
    return raw.substr(0, 3) + "-" + raw.substr(3);
}

std::string gen_wallet_code() {
    static const char* digits = "23456789";
    std::mt19937 rng{static_cast<unsigned>(::getpid()) ^
                     static_cast<unsigned>(std::random_device{}())};
    std::string code(kWcLen, 'X');
    for (size_t i = 0; i < kWcLen; ++i) {
        const char* set = (i == kWcLen - 1) ? digits : kWcAlphabet;
        code[i] = set[rng() % (set == digits ? 8 : 31)];
    }
    return code;
}

} // namespace

long long Store::now() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

Store::Store(const std::string& path) : path_(path) {
    ensure_connection();
    init();
}

Store::~Store() {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

void Store::ensure_connection() const {
    if (db_) return;
    if (sqlite3_open_v2(path_.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "cannot open db";
        db_ = nullptr;
        throw std::runtime_error("store: cannot open " + path_ + ": " + err);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
}

void Store::exec(const std::string& sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        throw_sql(db_, msg);
    }
}

void Store::init() {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    exec("CREATE TABLE IF NOT EXISTS sessions ("
         " id TEXT PRIMARY KEY,"
         " coins INTEGER NOT NULL DEFAULT 0,"
         " free_used INTEGER NOT NULL DEFAULT 0,"
         " created_at INTEGER NOT NULL,"
         " last_active INTEGER NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS settings ("
         " key TEXT PRIMARY KEY,"
         " value TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS credit_log ("
         " id INTEGER PRIMARY KEY AUTOINCREMENT,"
         " ts INTEGER NOT NULL,"
         " client_id TEXT NOT NULL,"
         " coins INTEGER NOT NULL,"
         " note TEXT)");
    exec("CREATE TABLE IF NOT EXISTS manual_override ("
         " client_id TEXT PRIMARY KEY,"
         " active INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS manual_queue ("
         " id INTEGER PRIMARY KEY AUTOINCREMENT,"
         " client_id TEXT NOT NULL,"
         " user_text TEXT NOT NULL,"
         " state TEXT NOT NULL DEFAULT 'pending',"
         " reply TEXT,"
         " created_at INTEGER NOT NULL,"
         " answered_at INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS payments ("
         " id INTEGER PRIMARY KEY AUTOINCREMENT,"
         " client_id TEXT NOT NULL,"
         " coins INTEGER NOT NULL,"
         " rub INTEGER NOT NULL,"
         " state TEXT NOT NULL DEFAULT 'pending',"
         " created_at INTEGER NOT NULL,"
         " approved_at INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS dialog_log ("
         " id INTEGER PRIMARY KEY AUTOINCREMENT,"
         " ts INTEGER NOT NULL,"
         " session_id TEXT,"
         " endpoint TEXT,"
         " status INTEGER NOT NULL,"
         " ms INTEGER NOT NULL,"
         " coins_spent INTEGER NOT NULL DEFAULT 0,"
         " detail TEXT,"
         " user_text TEXT,"
         " reply TEXT)");

    // Wallet recovery codes: warm the column in on schema upgrades and
    // guarantee uniqueness (NULLs from legacy rows are fine — they are filled
    // lazily by get_or_create_session).
    {
        Stmt probe(db_, "SELECT COUNT(*) FROM pragma_table_info('sessions') WHERE name='wallet_code'");
        if (!probe.step() || probe.col_int(0) == 0)
            exec("ALTER TABLE sessions ADD COLUMN wallet_code TEXT");
    }
    exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_sessions_wallet ON sessions(wallet_code) WHERE wallet_code IS NOT NULL");
}

SessionInfo Store::read_session(const std::string& client_id) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, coins, free_used, created_at, last_active, wallet_code FROM sessions WHERE id=?1");
    s.bind(1, client_id);
    if (!s.step()) return {};
    SessionInfo info;
    info.client_id = s.col_text(0);
    info.coins = s.col_int(1);
    info.free_used = static_cast<int>(s.col_int(2));
    info.created_at = s.col_int(3);
    info.last_active = s.col_int(4);
    info.wallet_code = s.col_text(5);
    return info;
}

SessionInfo Store::session(const std::string& client_id) {
    SessionInfo info = read_session(client_id);
    if (!info.client_id.empty()) info.free_limit = parse_int_env_or("JYOTISH_FREE_QUESTIONS", kDefaultFreeLimit);
    if (!info.wallet_code.empty()) info.wallet_code = format_wallet(info.wallet_code);
    return info;
}

SessionInfo Store::find_by_wallet(const std::string& code) {
    const std::string wanted = normalize_wallet(code);
    if (wanted.size() != kWcLen) return {};
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, coins, free_used, created_at, last_active FROM sessions WHERE wallet_code=?1");
    s.bind(1, wanted);
    if (!s.step()) return {};
    SessionInfo info;
    info.client_id = s.col_text(0);
    info.coins = s.col_int(1);
    info.free_used = static_cast<int>(s.col_int(2));
    info.created_at = s.col_int(3);
    info.last_active = s.col_int(4);
    info.wallet_code = format_wallet(wanted);
    info.free_limit = parse_int_env_or("JYOTISH_FREE_QUESTIONS", kDefaultFreeLimit);
    return info;
}

long long Store::purge_stale(long long ttl_seconds) {
    if (ttl_seconds <= 0) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    long long cutoff = now() - ttl_seconds;
    Stmt del(db_, "DELETE FROM sessions WHERE last_active < ?1");
    del.bind(1, cutoff);
    del.run();
    long long gone = sqlite3_changes(db_);
    exec("DELETE FROM manual_queue WHERE client_id NOT IN (SELECT id FROM sessions)");
    exec("DELETE FROM manual_override WHERE client_id NOT IN (SELECT id FROM sessions)");
    exec("DELETE FROM dialog_log WHERE session_id NOT IN (SELECT id FROM sessions)");
    return gone;
}

SessionInfo Store::get_or_create_session(const std::string& client_id) {
    if (client_id.empty()) return {};
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    long long ts = now();
    Stmt ins(db_, "INSERT OR IGNORE INTO sessions (id, coins, free_used, created_at, last_active) VALUES (?1, 0, 0, ?2, ?2)");
    ins.bind(1, client_id);
    ins.bind(2, ts);
    ins.run();

    // Assign a fresh wallet code when the session does not have one yet.
    // Loop-and-retry: the unique index aborts a starving UPDATE on collision.
    std::string wc;
    bool claimed = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        wc = gen_wallet_code();
        Stmt assign(db_, "UPDATE sessions SET wallet_code=?2 WHERE id=?1 AND wallet_code IS NULL");
        assign.bind(1, client_id);
        assign.bind(2, wc);
        try {
            assign.run();
            if (sqlite3_changes(db_) == 1) {
                claimed = true;
                break;
            }
            wc.clear();
            break;  // someone assigned a code concurrently; it will be read below
        } catch (const std::runtime_error&) {
            continue;  // unique collision, try another code
        }
    }

    Stmt upd(db_, "UPDATE sessions SET last_active=?2 WHERE id=?1");
    upd.bind(1, client_id);
    upd.bind(2, ts);
    upd.run();

    SessionInfo info;
    info.client_id = client_id;
    info.created_at = ts;
    info.last_active = ts;
    info.free_limit = parse_int_env_or("JYOTISH_FREE_QUESTIONS", kDefaultFreeLimit);
    // A concurrently-assigned code can beat our own: trust what is stored now.
    {
        Stmt rd(db_, "SELECT coins, free_used, wallet_code FROM sessions WHERE id=?1");
        rd.bind(1, client_id);
        if (rd.step()) {
            info.coins = rd.col_int(0);
            info.free_used = static_cast<int>(rd.col_int(1));
            info.wallet_code = rd.col_text(2);
        }
    }
    if (info.wallet_code.empty() && claimed) info.wallet_code = wc;
    if (!info.wallet_code.empty()) info.wallet_code = format_wallet(info.wallet_code);
    return info;
}

ConsumeResult Store::consume_question(const std::string& client_id, int free_limit) {
    if (client_id.empty()) return ConsumeResult::NoFunds;
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    long long ts = now();
    {
        Stmt s(db_, "INSERT OR IGNORE INTO sessions (id, coins, free_used, created_at, last_active) VALUES (?1, 0, 0, ?2, ?2)");
        s.bind(1, client_id);
        s.bind(2, ts);
        s.run();
    }
    Stmt get(db_, "SELECT free_used, coins FROM sessions WHERE id=?1");
    get.bind(1, client_id);
    if (!get.step()) return ConsumeResult::NoFunds;
    const long long free_used = get.col_int(0);
    const long long coins = get.col_int(1);

    if (free_used < free_limit) {
        Stmt u(db_, "UPDATE sessions SET free_used=free_used+1, last_active=?2 WHERE id=?1");
        u.bind(1, client_id);
        u.bind(2, ts);
        u.run();
        return ConsumeResult::Free;
    }
    if (coins > 0) {
        Stmt u(db_, "UPDATE sessions SET coins=coins-1, last_active=?2 WHERE id=?1");
        u.bind(1, client_id);
        u.bind(2, ts);
        u.run();
        return ConsumeResult::Coin;
    }
    {
        Stmt u(db_, "UPDATE sessions SET last_active=?2 WHERE id=?1");
        u.bind(1, client_id);
        u.bind(2, ts);
        u.run();
    }
    return ConsumeResult::NoFunds;
}

long long Store::credit(const std::string& client_id, int coins, const std::string& note) {
    if (client_id.empty() || coins <= 0) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    long long ts = now();
    {
        Stmt ins(db_, "INSERT OR IGNORE INTO sessions (id, coins, free_used, created_at, last_active) VALUES (?1, 0, 0, ?2, ?2)");
        ins.bind(1, client_id);
        ins.bind(2, ts);
        ins.run();
    }
    Stmt upd(db_, "UPDATE sessions SET coins=coins+?2, last_active=?3 WHERE id=?1");
    upd.bind(1, client_id);
    upd.bind(2, static_cast<long long>(coins));
    upd.bind(3, ts);
    upd.run();
    Stmt lg(db_, "INSERT INTO credit_log (ts, client_id, coins, note) VALUES (?1, ?2, ?3, ?4)");
    lg.bind(1, ts);
    lg.bind(2, client_id);
    lg.bind(3, static_cast<long long>(coins));
    if (note.empty()) lg.bind_null(4); else lg.bind(4, note);
    lg.run();
    return coins;
}

void Store::set_setting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "INSERT INTO settings (key, value) VALUES (?1, ?2) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    s.bind(1, key);
    s.bind(2, value);
    s.run();
}

std::string Store::setting(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT value FROM settings WHERE key=?1");
    s.bind(1, key);
    if (!s.step()) return {};
    return s.col_text(0);
}

bool Store::maintenance() const { return setting("maintenance") == "1"; }

void Store::set_maintenance(bool on) { set_setting("maintenance", on ? "1" : "0"); }

void Store::set_manual_override(const std::string& client_id, bool active) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "INSERT INTO manual_override (client_id, active) VALUES (?1, ?2) "
                "ON CONFLICT(client_id) DO UPDATE SET active=excluded.active");
    s.bind(1, client_id);
    s.bind(2, active ? 1LL : 0LL);
    s.run();
}

bool Store::manual_override(const std::string& client_id) const {
    if (client_id.empty()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT active FROM manual_override WHERE client_id=?1");
    s.bind(1, client_id);
    if (!s.step()) return false;
    return s.col_int(0) != 0;
}

std::vector<std::string> Store::overridden_sessions() {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT client_id FROM manual_override WHERE active=1");
    std::vector<std::string> out;
    while (s.step()) out.push_back(s.col_text(0));
    return out;
}

long long Store::enqueue_manual(const std::string& client_id, const std::string& user_text) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "INSERT INTO manual_queue (client_id, user_text, state, created_at) VALUES (?1, ?2, 'pending', ?3)");
    s.bind(1, client_id);
    s.bind(2, user_text);
    s.bind(3, now());
    s.run();
    return sqlite3_last_insert_rowid(db_);
}

void Store::answer_manual(long long id, const std::string& reply) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "UPDATE manual_queue SET state='answered', reply=?2, answered_at=?3 WHERE id=?1 AND state='pending'");
    s.bind(1, id);
    s.bind(2, reply);
    s.bind(3, now());
    s.run();
}

std::vector<ManualItem> Store::manual_queue(const std::string& state) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, client_id, user_text, state, reply, created_at, answered_at "
                "FROM manual_queue WHERE state=?1 ORDER BY id DESC LIMIT 200");
    s.bind(1, state);
    std::vector<ManualItem> out;
    while (s.step()) {
        ManualItem it;
        it.id = s.col_int(0);
        it.client_id = s.col_text(1);
        it.user_text = s.col_text(2);
        it.state = s.col_text(3);
        it.reply = s.col_text(4);
        it.created_at = s.col_int(5);
        if (s.col_int(6) != 0) it.answered_at = s.col_int(6);
        out.push_back(std::move(it));
    }
    return out;
}

std::vector<ManualItem> Store::take_deliverable(const std::string& client_id) {
    if (client_id.empty()) return {};
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, user_text, reply FROM manual_queue "
                "WHERE client_id=?1 AND state='answered' ORDER BY id ASC");
    s.bind(1, client_id);
    std::vector<ManualItem> out;
    while (s.step()) {
        ManualItem it;
        it.id = s.col_int(0);
        it.client_id = client_id;
        it.user_text = s.col_text(1);
        it.reply = s.col_text(2);
        it.state = "delivered";
        out.push_back(std::move(it));
    }
    if (!out.empty()) {
        Stmt d(db_, "UPDATE manual_queue SET state='delivered' WHERE client_id=?1 AND state='answered'");
        d.bind(1, client_id);
        d.run();
    }
    return out;
}

long long Store::create_payment(const std::string& client_id, int coins, int rub) {
    if (client_id.empty() || coins <= 0) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    long long ts = now();
    {
        Stmt ins(db_, "INSERT OR IGNORE INTO sessions (id, coins, free_used, created_at, last_active) VALUES (?1, 0, 0, ?2, ?2)");
        ins.bind(1, client_id);
        ins.bind(2, ts);
        ins.run();
    }
    Stmt p(db_, "INSERT INTO payments (client_id, coins, rub, state, created_at) VALUES (?1, ?2, ?3, 'pending', ?4)");
    p.bind(1, client_id);
    p.bind(2, static_cast<long long>(coins));
    p.bind(3, static_cast<long long>(rub));
    p.bind(4, ts);
    p.run();
    return sqlite3_last_insert_rowid(db_);
}

bool Store::approve_payment(long long id) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT client_id, coins FROM payments WHERE id=?1 AND state='pending'");
    s.bind(1, id);
    if (!s.step()) return false;
    const std::string client_id = s.col_text(0);
    const long long coins = s.col_int(1);
    Stmt u(db_, "UPDATE payments SET state='approved', approved_at=?2 WHERE id=?1 AND state='pending'");
    u.bind(1, id);
    u.bind(2, now());
    u.run();
    Stmt c(db_, "UPDATE sessions SET coins=coins+?2, last_active=?3 WHERE id=?1");
    c.bind(1, client_id);
    c.bind(2, coins);
    c.bind(3, now());
    c.run();
    Stmt lg(db_, "INSERT INTO credit_log (ts, client_id, coins, note) VALUES (?1, ?2, ?3, ?4)");
    lg.bind(1, now());
    lg.bind(2, client_id);
    lg.bind(3, coins);
    lg.bind(4, std::string("payment approved"));
    lg.run();
    return true;
}

std::vector<PaymentInfo> Store::payments(const std::string& client_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, client_id, coins, rub, state, created_at, approved_at FROM payments WHERE client_id=?1 ORDER BY id DESC LIMIT 50");
    s.bind(1, client_id);
    std::vector<PaymentInfo> out;
    while (s.step()) {
        PaymentInfo p;
        p.id = s.col_int(0);
        p.client_id = s.col_text(1);
        p.coins = static_cast<int>(s.col_int(2));
        p.rub = static_cast<int>(s.col_int(3));
        p.state = s.col_text(4);
        p.created_at = s.col_int(5);
        if (s.col_int(6) != 0) p.approved_at = s.col_int(6);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<PaymentInfo> Store::pending_payments() const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, client_id, coins, rub, state, created_at, approved_at FROM payments WHERE state='pending' ORDER BY id DESC LIMIT 200");
    std::vector<PaymentInfo> out;
    while (s.step()) {
        PaymentInfo p;
        p.id = s.col_int(0);
        p.client_id = s.col_text(1);
        p.coins = static_cast<int>(s.col_int(2));
        p.rub = static_cast<int>(s.col_int(3));
        p.state = s.col_text(4);
        p.created_at = s.col_int(5);
        if (s.col_int(6) != 0) p.approved_at = s.col_int(6);
        out.push_back(std::move(p));
    }
    return out;
}

void Store::log(const std::string& session_id, const std::string& endpoint,
                int status, int ms, int coins_spent, const std::string& detail,
                const std::string& user_text, const std::string& reply) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "INSERT INTO dialog_log (ts, session_id, endpoint, status, ms, coins_spent, detail, user_text, reply) "
                "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    s.bind(1, now());
    if (session_id.empty()) s.bind_null(2); else s.bind(2, session_id);
    s.bind(3, endpoint);
    s.bind(4, static_cast<long long>(status));
    s.bind(5, static_cast<long long>(ms));
    s.bind(6, static_cast<long long>(coins_spent));
    if (detail.empty()) s.bind_null(7); else s.bind(7, detail);
    if (user_text.empty()) s.bind_null(8); else s.bind(8, user_text);
    if (reply.empty()) s.bind_null(9); else s.bind(9, reply);
    s.run();
}

std::optional<LogRow> Store::last_oracle_turn(const std::string& client_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, ts, session_id, endpoint, status, ms, coins_spent, detail, user_text, reply "
                "FROM dialog_log WHERE session_id=?1 AND endpoint='oracle' AND status=200 "
                "ORDER BY id DESC LIMIT 1");
    s.bind(1, client_id);
    if (!s.step()) return std::nullopt;
    LogRow r;
    r.id = s.col_int(0);
    r.ts = s.col_int(1);
    r.session_id = s.col_text(2);
    r.endpoint = s.col_text(3);
    r.status = static_cast<int>(s.col_int(4));
    r.ms = static_cast<int>(s.col_int(5));
    r.coins_spent = static_cast<int>(s.col_int(6));
    r.detail = s.col_text(7);
    r.user_text = s.col_text(8);
    r.reply = s.col_text(9);
    return r;
}

std::vector<LogRow> Store::recent_logs(int limit) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, ts, session_id, endpoint, status, ms, coins_spent, detail, user_text, reply "
                "FROM dialog_log ORDER BY id DESC LIMIT ?1");
    s.bind(1, static_cast<long long>(limit));
    std::vector<LogRow> out;
    while (s.step()) {
        LogRow r;
        r.id = s.col_int(0);
        r.ts = s.col_int(1);
        r.session_id = s.col_text(2);
        r.endpoint = s.col_text(3);
        r.status = static_cast<int>(s.col_int(4));
        r.ms = static_cast<int>(s.col_int(5));
        r.coins_spent = static_cast<int>(s.col_int(6));
        r.detail = s.col_text(7);
        r.user_text = s.col_text(8);
        r.reply = s.col_text(9);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<SessionInfo> Store::sessions(int limit) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT id, coins, free_used, created_at, last_active FROM sessions ORDER BY last_active DESC LIMIT ?1");
    s.bind(1, static_cast<long long>(limit));
    std::vector<SessionInfo> out;
    while (s.step()) {
        SessionInfo i;
        i.client_id = s.col_text(0);
        i.coins = s.col_int(1);
        i.free_used = static_cast<int>(s.col_int(2));
        i.created_at = s.col_int(3);
        i.last_active = s.col_int(4);
        i.free_limit = parse_int_env_or("JYOTISH_FREE_QUESTIONS", kDefaultFreeLimit);
        out.push_back(std::move(i));
    }
    return out;
}

long long Store::total_sessions() const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT COUNT(*) FROM sessions");
    return s.step() ? s.col_int(0) : 0;
}

long long Store::total_coins_granted() const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT COALESCE(SUM(coins), 0) FROM credit_log");
    return s.step() ? s.col_int(0) : 0;
}

long long Store::total_coins_spent() const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT COALESCE(SUM(coins_spent), 0) FROM dialog_log");
    return s.step() ? s.col_int(0) : 0;
}

long long Store::questions_last_hours(int hours) const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT COUNT(*) FROM dialog_log WHERE coins_spent > 0 AND ts > ?1");
    s.bind(1, now() - static_cast<long long>(hours) * 3600LL);
    return s.step() ? s.col_int(0) : 0;
}

long long Store::approved_revenue_rub() const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT COALESCE(SUM(rub), 0) FROM payments WHERE state='approved'");
    return s.step() ? s.col_int(0) : 0;
}

long long Store::pending_manual_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_connection();
    Stmt s(db_, "SELECT COUNT(*) FROM manual_queue WHERE state='pending'");
    return s.step() ? s.col_int(0) : 0;
}

} // namespace jyotish::store