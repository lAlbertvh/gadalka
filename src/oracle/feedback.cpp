#include <jyotish/feedback.hpp>
#include <jyotish/config.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace jyotish::feedback {

namespace {

// Lowercase ASCII + the fixed Cyrillic-only-uppercase rule (same as the
// research.cpp lower_u8 fix) so RU "да"/"нет" match regardless of case.
std::string lower(const std::string& s) {
    std::string out(s);
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(out[i]);
        if (b0 == 0xD0) {
            unsigned char b1 = static_cast<unsigned char>(out[i + 1]);
            if (b1 >= 0x90 && b1 <= 0x9F) {
                out[i] = static_cast<char>(0xD1);
                out[i + 1] = static_cast<char>(b1 + 0x20);
                ++i;
            } else if (b1 == 0x81) {
                out[i] = static_cast<char>(0xD1);
                out[i + 1] = static_cast<char>(0x91);
                ++i;
            }
        } else if (b0 < 0x80) {
            out[i] = static_cast<char>(std::tolower(b0));
        }
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

constexpr const char* YES_TOKENS[] = {"да", "был", "была", "было", "полез", "помог", "спасибо",
                                      "отлично", "хорошо", "верно", "точно", "так и", "yes",
                                      "yep", "yup", "helpful", "useful", "was helpful", "good"};
constexpr const char* NO_TOKENS[] = {"нет", "не был", "не было", "не помог", "не полез", "плохо",
                                     "ужас", "не нрав", "no", "not helpful", "not useful"};
constexpr const char* QUESTION_WORDS[] = {"расскажи", "посоветуй", "скажи", "объясни", "покажи",
                                          "сколько", "почему", "зачем", "какой", "какая", "какое",
                                          "какие", "кто", "что", "где", "когда", "куда", "откуда",
                                          "как", "можно", "лучше", "стоит", "буду ли"};

} // namespace

std::string marker(bool grounded) {
    return std::string("[FEEDBACK|g") + (grounded ? "1" : "0") + "]";
}

std::string ask(const std::string& lang, bool grounded) {
    (void)lang;  // the frontend widget is language-agnostic now
    // Marker on its own line; the frontend renders it as a 👍/👎 widget and
    // hides the marker itself. No visible "answer yes/no" sentence is sent:
    // a real question typed right after a reading must never be swallowed.
    return "\n\n" + marker(grounded);
}

bool pending(const std::vector<std::string>& history_messages, bool* grounded_out) {
    for (auto it = history_messages.rbegin(); it != history_messages.rend(); ++it) {
        auto colon = it->find(':');
        std::string role = colon == std::string::npos ? "" : it->substr(0, colon);
        if (role == "assistant") {
            // Find the last feedback marker, if any.
            std::string::size_type last = std::string::npos;
            std::string::size_type pos = it->find("[FEEDBACK|");
            while (pos != std::string::npos) { last = pos; pos = it->find("[FEEDBACK|", pos + 1); }
            if (last == std::string::npos) return false;
            if (grounded_out) *grounded_out = it->find("[FEEDBACK|g1]") != std::string::npos;
            return true;
        }
        if (role == "user") continue;  // wait for the assistant message
    }
    return false;
}

int classify(const std::string& text, const std::string& lang) {
    const std::string low = lower(text);
    // Bare "да" / "нет" (whole word, or leading word before a comma/dot).
    if (lang == "ru") {
        const std::string q = trim(low);
        if (q == "да" || q == "да." || q.starts_with("да ") || q.starts_with("да,")) return 1;
        if (q == "нет" || q == "нет." || q.starts_with("нет ") || q.starts_with("нет,")) return -1;
        if (q.find("да нет") != std::string::npos) return 0;
    } else {
        const std::string q = trim(low);
        if (q == "yes" || q.starts_with("yes ") || q.starts_with("yes,")) return 1;
        if (q == "no" || q.starts_with("no ") || q.starts_with("no,")) return -1;
    }
    bool yes = false, no = false;
    for (const char* t : YES_TOKENS) { if (low.find(t) != std::string::npos) { yes = true; break; } }
    for (const char* t : NO_TOKENS) { if (low.find(t) != std::string::npos) { no = true; break; } }
    if (yes && !no) return 1;
    if (no && !yes) return -1;
    return 0;
}

bool is_pure_rating(const std::string& text, const std::string& lang) {
    const std::string low = lower(text);
    const std::string q = trim(low);
    if (q.empty()) return false;
    // Longer than a minute "yes, and by the way..." is a question, not a rating.
    if (q.size() > 60) return false;
    // A bare question mark instantly makes it a question.
    if (q.find('?') != std::string::npos) return false;
    // Any question-ish word ("расскажи", "сколько", "кто", "как"...) means the
    // user expects a real answer, so the feedback hook must not eat it.
    for (const char* w : QUESTION_WORDS) {
        if (q.find(w) != std::string::npos) return false;
    }
    // Safety net: it still has to look like an actual rating, otherwise we
    // would ack a random short sentence ("пятница", "ок") that was never a
    // feedback answer.
    return classify(low, lang) != 0;
}

std::string strip(std::string reply) {
    std::string::size_type last = std::string::npos;
    std::string::size_type pos = reply.find("[FEEDBACK|");
    while (pos != std::string::npos) { last = pos; pos = reply.find("[FEEDBACK|", pos + 1); }
    if (last == std::string::npos) return reply;
    return trim(reply.substr(0, last));
}

std::optional<std::pair<std::string, std::string>> pair_for_last_ask(
    const std::vector<std::string>& history_messages) {
    // Locate the last assistant message carrying a feedback marker.
    ssize_t ask_idx = -1;
    for (ssize_t i = static_cast<ssize_t>(history_messages.size()) - 1; i >= 0; --i) {
        auto colon = history_messages[static_cast<size_t>(i)].find(':');
        std::string role = colon == std::string::npos ? "" : history_messages[static_cast<size_t>(i)].substr(0, colon);
        if (role == "assistant" && history_messages[static_cast<size_t>(i)].find("[FEEDBACK|") != std::string::npos) {
            ask_idx = i;
            break;
        }
    }
    if (ask_idx < 0) return std::nullopt;

    auto colon_a = history_messages[static_cast<size_t>(ask_idx)].find(':');
    std::string answer = colon_a == std::string::npos
        ? history_messages[static_cast<size_t>(ask_idx)]
        : history_messages[static_cast<size_t>(ask_idx)].substr(colon_a + 2);
    answer = strip(answer);

    for (ssize_t i = ask_idx - 1; i >= 0; --i) {
        auto colon = history_messages[static_cast<size_t>(i)].find(':');
        std::string role = colon == std::string::npos ? "" : history_messages[static_cast<size_t>(i)].substr(0, colon);
        if (role == "user") {
            std::string q = colon == std::string::npos
                ? history_messages[static_cast<size_t>(i)]
                : history_messages[static_cast<size_t>(i)].substr(colon + 2);
            return std::make_pair(q, answer);
        }
    }
    return std::nullopt;
}

void record(const std::string& feedback_dir,
            const std::string& lang,
            const std::string& question,
            const std::string& answer,
            bool useful,
            const std::string& model,
            bool grounded) {
    try {
        std::error_code ec;
        std::filesystem::create_directories(feedback_dir, ec);
        if (ec) return;

        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&now, &tmv);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);

        const std::string path = feedback_dir + "/" + buf + ".jsonl";
        nlohmann::json j = {
            {"ts", static_cast<long long>(now)},
            {"lang", lang},
            {"question", question},
            {"answer", answer},
            {"useful", useful},
            {"model", model},
            {"grounded", grounded},
        };
        std::ofstream f(path, std::ios::app);
        if (!f) return;
        f << j.dump() << "\n";
    } catch (...) { /* best-effort */ }
}

} // namespace jyotish::feedback