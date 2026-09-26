#pragma once

#include <jyotish/types.hpp>
#include <jyotish/oracle.hpp>
#include <jyotish/parsing.hpp>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace jyotish::famous {

struct FamousEvent {
    int year = 0;
    std::string category;   // see CATEGORIES below
    std::string outcome;    // success | fail | mixed
    std::string action_ru;
    std::string action_en;
};

struct Person {
    std::string name;
    std::string lang;       // "ru" | "en"
    std::string birth_date; // YYYY-MM-DD
    std::string birth_time; // HH:MM or empty
    std::string birth_place;
    std::vector<FamousEvent> events;
};

// Dataset, cached after the first load from settings().famous_file.
const std::vector<Person>& people();

extern const std::vector<std::string> CATEGORIES;

// Best matching category for the user's question text ("" if none).
std::string categorize(const std::string& text, const std::string& lang);

// Most similar famous people by natal-chart profile (lagna/moon/sun/nakshatra
// + current mahadasha). Requires the user's computed chart; when the birth
// time is unknown Lagna comparisons are skipped.
struct PersonMatch {
    std::string name;
    int score;
    std::vector<std::string> reasons_ru;
    std::vector<std::string> reasons_en;
};
std::vector<PersonMatch> famous_matches(const Chart& chart, bool has_birth_time, int top = 3);

// 4-digit year present in the text (or the current year).
int target_year(const std::string& text);

// Law of large numbers over the dataset: what happened to famous people who
// were running the SAME mahadasha planet (as the user will be in the target
// year) in the year of their event of this category.
struct EventStat {
    bool has_data = false;
    std::string category;
    std::string planet;            // canonical planet name (en)
    std::string planet_ru;
    int target_year = 0;
    int success = 0, fail = 0, mixed = 0;
    std::vector<std::string> examples_success;   // "Name — action"
    std::vector<std::string> examples_fail;
    std::vector<std::string> examples_mixed;
    int total() const { return success + fail + mixed; }
};
std::optional<EventStat> event_stat(const std::string& category, int target_year,
                                    const Chart::BirthData& birth, const Chart& chart,
                                    const std::string& lang = "ru");

// Combined narrative block (people matches + event statistic) in the requested
// language, appended to the LLM context. Empty when nothing is available.
std::string analog_block(const std::string& question, const Chart::BirthData& birth,
                         const Chart& chart, const std::string& lang,
                         nlohmann::json& out_analogs);

// Planet display name in a language.
std::string planet_display(Planet p, const std::string& lang);

} // namespace jyotish::famous