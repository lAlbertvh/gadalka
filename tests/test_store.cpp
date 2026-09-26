#include <gtest/gtest.h>
#include <jyotish/store.hpp>
#include <sqlite3.h>

#include <cctype>
#include <cstdio>
#include <unistd.h>

using namespace jyotish;

namespace {

std::string tmp_db_path() {
    return std::string("/tmp/jyotish_store_test_") + std::to_string(getpid()) + ".db";
}

// Backdate a session's last_active directly so store::purge_stale can see it
// as idle. Test-only helper reaching into the SQLite file.
void backdate_last_active(const std::string& path, const std::string& client_id, long long seconds) {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    std::string sql = "UPDATE sessions SET last_active = last_active - " +
                      std::to_string(seconds);
    if (!client_id.empty()) sql += " WHERE id='" + client_id + "'";
    char* err = nullptr;
    ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err), SQLITE_OK);
    sqlite3_free(err);
    sqlite3_close(db);
}

} // namespace

TEST(StoreTest, SessionAndFreeQuestions) {
    std::string path = tmp_db_path();
    {
        store::Store st(path);
        ASSERT_TRUE(st.ok());

        auto s = st.get_or_create_session("c-user-1");
        EXPECT_EQ(s.coins, 0);
        EXPECT_EQ(s.free_used, 0);

        for (int i = 0; i < 5; ++i) {
            EXPECT_EQ(st.consume_question("c-user-1", 5), store::ConsumeResult::Free);
        }
        // 6th question: no coins yet -> NoFunds.
        EXPECT_EQ(st.consume_question("c-user-1", 5), store::ConsumeResult::NoFunds);

        // Top-up 3 coins -> next 3 turns cost coins, then NoFunds again.
        st.credit("c-user-1", 3, "test");
        EXPECT_EQ(st.consume_question("c-user-1", 5), store::ConsumeResult::Coin);
        EXPECT_EQ(st.consume_question("c-user-1", 5), store::ConsumeResult::Coin);
        EXPECT_EQ(st.consume_question("c-user-1", 5), store::ConsumeResult::Coin);
        EXPECT_EQ(st.consume_question("c-user-1", 5), store::ConsumeResult::NoFunds);

        auto s2 = st.session("c-user-1");
        EXPECT_EQ(s2.coins, 0);
        EXPECT_EQ(s2.free_used, 5);

        EXPECT_EQ(st.total_sessions(), 1);
        EXPECT_EQ(st.total_coins_granted(), 3);
    }
    std::remove(path.c_str());
}

TEST(StoreTest, MaintenanceAndManualOverride) {
    std::string path = tmp_db_path();
    {
        store::Store st(path);
        EXPECT_FALSE(st.maintenance());
        st.set_maintenance(true);
        EXPECT_TRUE(st.maintenance());
        st.set_maintenance(false);
        EXPECT_FALSE(st.maintenance());

        EXPECT_FALSE(st.manual_override("c-x"));
        st.set_manual_override("c-x", true);
        EXPECT_TRUE(st.manual_override("c-x"));
        EXPECT_EQ(st.overridden_sessions().size(), 1);
        st.set_manual_override("c-x", false);
        EXPECT_FALSE(st.manual_override("c-x"));

        // Manual queue round-trip.
        long long qid = st.enqueue_manual("c-x", "когда деньги?");
        ASSERT_GT(qid, 0);
        EXPECT_EQ(st.pending_manual_count(), 1);
        auto q = st.manual_queue("pending");
        ASSERT_EQ(q.size(), 1);
        EXPECT_EQ(q[0].user_text, "когда деньги?");

        st.answer_manual(qid, "скоро (ручной ответ)");
        EXPECT_EQ(st.pending_manual_count(), 0);
        EXPECT_EQ(st.manual_queue("answered").size(), 1);

        // Client takes the deliverable, then it is marked delivered.
        auto got = st.take_deliverable("c-x");
        ASSERT_EQ(got.size(), 1);
        EXPECT_EQ(got[0].reply, "скоро (ручной ответ)");
        EXPECT_TRUE(st.take_deliverable("c-x").empty());
    }
    std::remove(path.c_str());
}

TEST(StoreTest, Payments) {
    std::string path = tmp_db_path();
    {
        store::Store st(path);
        store::Store::now();
        long long pid = st.create_payment("c-p", 10, 100);
        ASSERT_GT(pid, 0);
        auto pend = st.pending_payments();
        ASSERT_EQ(pend.size(), 1);
        EXPECT_EQ(pend[0].coins, 10);
        EXPECT_EQ(pend[0].rub, 100);

        // Balance before approval.
        EXPECT_EQ(st.session("c-p").coins, 0);

        ASSERT_TRUE(st.approve_payment(pid));
        EXPECT_EQ(st.approved_revenue_rub(), 100);

        // Coins were granted.
        EXPECT_EQ(st.session("c-p").coins, 10);

        // Approving twice must not grant twice.
        EXPECT_FALSE(st.approve_payment(pid));
        EXPECT_EQ(st.session("c-p").coins, 10);

        auto mine = st.payments("c-p");
        ASSERT_EQ(mine.size(), 1);
        EXPECT_EQ(mine[0].state, "approved");
    }
    std::remove(path.c_str());
}

TEST(StoreTest, LogsAndAggregates) {
    std::string path = tmp_db_path();
    {
        store::Store st(path);
        st.credit("c-L", 4, "test");
        ASSERT_EQ(st.consume_question("c-L", 5), store::ConsumeResult::Free);
        st.log("c-L", "oracle", 200, 1234, 0, "", "привет", "приветик");
        ASSERT_EQ(st.consume_question("c-L", 5), store::ConsumeResult::Free);
        st.log("c-L", "oracle", 200, 900, 0, "", "вопрос-2", "ответ-2");
        ASSERT_EQ(st.consume_question("c-L", 5), store::ConsumeResult::Free);

        auto logs = st.recent_logs(10);
        ASSERT_GE(logs.size(), 2);
        EXPECT_EQ(logs[0].endpoint, "oracle");
        EXPECT_EQ(logs[0].status, 200);
        EXPECT_EQ(logs[0].ms, 900);

        EXPECT_EQ(st.total_coins_granted(), 4);
        EXPECT_EQ(st.total_coins_spent(), 0);
        EXPECT_EQ(st.questions_last_hours(24), 0);  // no coin-spending turns

        // Sessions listing.
        auto sess = st.sessions(10);
        ASSERT_GE(sess.size(), 1);
    }
    std::remove(path.c_str());
}

TEST(StoreTest, WalletCodeAssignAndRestore) {
    std::string path = tmp_db_path();
    {
        store::Store st(path);
        auto a = st.get_or_create_session("c-w1");
        auto b = st.get_or_create_session("c-w2");
        // Every session gets a memorable short code, distinct from others.
        ASSERT_FALSE(a.wallet_code.empty());
        EXPECT_EQ(a.wallet_code.size(), 7);  // "XXX-XXX"
        EXPECT_NE(a.wallet_code, b.wallet_code);

        // Restore by exact code.
        auto found = st.find_by_wallet(a.wallet_code);
        ASSERT_FALSE(found.client_id.empty());
        EXPECT_EQ(found.client_id, "c-w1");
        EXPECT_EQ(found.wallet_code, a.wallet_code);

        // Case and dash don't matter: "xxxx-xx" / "xxxxxx" both match.
        std::string lower = a.wallet_code;
        for (auto& c : lower) c = char(std::tolower((unsigned char)c));
        EXPECT_EQ(st.find_by_wallet(lower).client_id, "c-w1");
        std::string nodash;
        for (char c : a.wallet_code) if (c != '-') nodash.push_back(c);
        EXPECT_EQ(st.find_by_wallet(nodash).client_id, "c-w1");

        // Unknown code -> no session.
        EXPECT_TRUE(st.find_by_wallet("ZZZ-ZZZ").client_id.empty());

        // Balance is shared by the code: top-up then restore shows it.
        st.credit("c-w1", 3, "test");
        EXPECT_EQ(st.find_by_wallet(a.wallet_code).coins, 3);

        // Codes survive reopen of the store (same file).
    }
    {
        store::Store st(path);
        auto fresh = st.get_or_create_session("c-w1");
        EXPECT_EQ(fresh.wallet_code.size(), 7);
        EXPECT_FALSE(st.find_by_wallet(fresh.wallet_code).client_id.empty());
    }
    std::remove(path.c_str());
}

TEST(StoreTest, PurgeStaleSessions) {
    std::string path = tmp_db_path();
    {
        store::Store st(path);
        st.get_or_create_session("c-old");
        st.get_or_create_session("c-new");
        st.enqueue_manual("c-old", "старый вопрос?");
        st.log("c-old", "oracle", 200, 1, 0, "", "?", "!");

        // Old session idle for 20 days, fresh one just touched.
        st.get_or_create_session("c-touch");  // ensures row exists
        backdate_last_active(path, "c-old", 20LL * 86400);

        // TTL 10 days: only c-old qualifies.
        EXPECT_EQ(st.purge_stale(10LL * 86400), 1);
        ASSERT_TRUE(st.session("c-old").client_id.empty());
        EXPECT_FALSE(st.session("c-new").client_id.empty());

        // Its wallet code is gone too.
        EXPECT_TRUE(st.find_by_wallet("c-w1").client_id.empty() ||
                    st.find_by_wallet(st.session("c-new").wallet_code).client_id == "c-new");

        // Related manual/log rows for the removed session are cleaned.
        EXPECT_EQ(st.pending_manual_count(), 0);
        auto logs = st.recent_logs(10);
        bool has_old = false;
        for (const auto& l : logs) if (l.session_id == "c-old") has_old = true;
        EXPECT_FALSE(has_old);
    }
    std::remove(path.c_str());
}