#pragma once

#include <jyotish/types.hpp>
#include <jyotish/geocode.hpp>
#include <jyotish/chart.hpp>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace jyotish::oracle {

// Birth data collected from conversation
struct BirthInfo {
    std::string name;
    std::string birth_date;   // YYYY-MM-DD
    std::string birth_time;   // HH:MM
    std::string city;
    std::string gender;       // "male" / "female" / "" (unknown)
    int age = 0;              // explicit age stated by the user ("мне 36 лет")
    double latitude = 0.0;
    double longitude = 0.0;
    double tz_offset = 0.0;
    bool city_found = false;
};

struct BirthStatus {
    bool name = false;
    bool birth_date = false;
    bool birth_time = false;
    bool city = false;
};

// Parse birth data from conversation history
BirthInfo gather_birth(const std::vector<std::string>& history_texts);
bool birth_is_ready(const BirthInfo& info);
BirthStatus birth_status(const BirthInfo& info);
std::string onboarding_reply(const BirthInfo& info, const std::string& lang = "ru");
Chart::BirthData build_birth_data(const BirthInfo& info);

// A year visibly in the future (relative to today) mentioned in the text, e.g.
// "35.04.2380" or "15.08.2030". Returns the offending year if any. Used to
// answer a birth-date attempt that points past "today" without feeding it into
// a confirmation echo or a chart.
std::optional<int> future_year_in_text(const std::string& text);
// Onboarding reply for a birth date that lies in the future ("посланник из
// будущего"): the oracle cannot use data from after the day you two met.
std::string future_date_reply(const BirthInfo& info, const std::string& lang, int year);

// Date/time parsing
std::optional<std::pair<std::chrono::sys_days, std::string>> parse_relative_date(const std::string& text, std::chrono::sys_days base);
std::optional<std::pair<std::chrono::sys_days, std::string>> parse_iso_date(const std::string& text);
std::optional<std::string> extract_time(const std::string& text);
std::string extract_name(const std::string& text);
std::optional<int> extract_age(const std::string& text);
bool has_explicit_year(const std::string& text);

// Gender inference: "male"/"female"/"" — from explicit statements
// ("я мальчик/девочка", "я мужчина/женщина", "родился/родилась") or from the
// morphology of the given name. Returns "" when nothing reliable is found.
std::string guess_gender(const std::string& name_or_text);

// Situation detection
std::vector<std::string> detect_situations(const std::string& text, const std::string& lang = "ru");
std::string format_situations(const std::vector<std::string>& situations, const std::string& lang);

// Casual conversation (no birth data needed)
bool is_casual_question(const std::string& text, const std::string& lang = "ru", const BirthInfo* info = nullptr);
std::optional<std::string> casual_answer(const std::string& text, const std::string& lang = "ru", std::chrono::sys_days day = {});
std::string casual_chat(const std::string& question, const std::string& lang);

// True when an otherwise-Cyrillic reply carries an untranslated Latin run
// longer than `latin_min` letters ("…черезIncreased self-discipline…") — the
// whole-text "no Cyrillic" retry never catches such mixtures. Exposed for tests.
bool has_english_leak(const std::string& text, size_t latin_min = 30);

// History helpers
std::vector<std::string> split_history(const std::string& raw);

// Name enforcement: replace wrong made-up names in an LLM reply with the user's
// confirmed name (nominative address form only). Exposed for tests.
std::string enforce_user_name(const std::string& reply, const std::string& real_name);
std::string lower_name(const std::string& s);

// Replace literal LLM placeholder tokens for the name ("[Ваше имя]", "[имя
// пользователя]", "[Your Name]") with the confirmed name, or drop them when
// the name is unknown. Exposed for tests.
std::string substitute_name_placeholders(const std::string& reply, const std::string& real_name);

// Confirmation gate: when complete birth data is first available the oracle
// echoes it back ("You were born ... , correct?") and waits for an explicit
// "yes" before computing the chart, so a data-entry mistake can be caught.
enum class ConfirmState { Confirmed, NeedConfirm };
struct ConfirmResult {
    ConfirmState state = ConfirmState::NeedConfirm;
    std::string reply;
    // True when THIS message explicitly confirmed previously echoed birth data
    // (a bare "да"/"верно"), regardless of how old/stale the dialogue history is.
    // Used to start the horoscope intro instead of answering the word "да".
    bool just_confirmed = false;
};
ConfirmResult confirm_birth(const std::string& question,
                            const std::vector<std::string>& history_messages,
                            const BirthInfo& info,
                            const std::string& lang = "ru");

// True when the user explicitly says they don't know the exact birth time
// ("да только время рождения я не знаю", "не помню точное время", "примерно…").
// The confirmation gate must then NOT treat a leading "да" as a full consent
// for the chart — the time is replaced by the noon default and re-confirmed.
bool signals_unknown_time(const std::string& question);

// The birth data the oracle echoed in its last "[CONFIRM|date|time|city|name]"
// confirmation message. Onboarding state is re-derived from user messages on
// every request, so data the user never typed (the noon default for an unknown
// birth time) lives only in this echo and must be re-read from it.
struct ConfirmEcho {
    std::string birth_date, birth_time, city, name;
};
std::optional<ConfirmEcho> last_confirm_echo(const std::vector<std::string>& history_messages);

// Context building for LLM
struct OracleContext {
    std::string context;
    std::optional<std::string> found_date;
    std::optional<std::string> found_time;
    std::optional<jyotish::DashaPeriod> period;
    std::optional<jyotish::TransitSnapshot> transits;
    std::vector<std::string> situations;
    nlohmann::json analogs;   // famous-people analog engine output (matches + event stats)
    bool grounded = false;    // live research corroborated the question (>=2 independent sources)
};

OracleContext build_context(const Chart::BirthData& birth, const Chart& charter, const Chart& canonical,
                           const std::string& question, const std::string& photo_context = "",
                           const std::string& lang = "ru");

// Quote pool
std::vector<std::string> sample_quotes(const std::string& lang, int n = 24);
extern const std::vector<std::string> QUOTES_RU;
extern const std::vector<std::string> QUOTES_EN;

// Hard cap on a generated reply: cut at the last sentence end at or before the
// byte budget, never splitting a UTF-8 character (JYOTISH_MAX_REPLY_CHARS).
std::string clip_reply(const std::string& reply, size_t budget);

// Prompt templates
extern const std::string ORACLE_PROMPT_RU;
extern const std::string ORACLE_PROMPT_EN;
extern const std::string CASUAL_SYSTEM_RU;
extern const std::string CASUAL_SYSTEM_EN;

std::tuple<std::string, OracleContext, nlohmann::json> oracle_chat(
    const Chart::BirthData& birth,
    const Chart& charter,
    const Chart& canonical,
    const std::vector<nlohmann::json>& history,
    const std::string& question,
    const std::string& photo_context = "",
    bool first_reply = false,
    const std::string& lang = "ru",
    const nlohmann::json& profile = nlohmann::json::object());

} // namespace jyotish::oracle