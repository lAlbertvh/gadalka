#include <jyotish/famous.hpp>

#include <jyotish/config.hpp>
#include <jyotish/core.hpp>
#include <jyotish/dasha.hpp>
#include <jyotish/chart.hpp>
#include <jyotish/geocode.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_set>

namespace jyotish::famous {

namespace {

using namespace jyotish;

// ---------------------------------------------------------------------------
// Dataset loading
// ---------------------------------------------------------------------------
std::vector<Person> load_people() {
    std::vector<Person> out;
    std::string name = settings().famous_file;
    std::ifstream f(name);
    if (!f && !name.empty()) {
        f.open("data/" + name);   // fall back to the standard data directory
    }
    if (!f) return out;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    try {
        auto j = nlohmann::json::parse(content);
        if (!j.is_array()) return out;
        for (const auto& p : j) {
            if (!p.is_object()) continue;
            Person person;
            person.name = p.value("name", "");
            person.lang = p.value("lang", "ru");
            person.birth_date = p.value("birth", "");
            person.birth_time = p.value("time", "");
            person.birth_place = p.value("place", "");
            if (p.contains("events") && p["events"].is_array()) {
                for (const auto& e : p["events"]) {
                    if (!e.is_object()) continue;
                    FamousEvent ev;
                    ev.year = e.value("year", 0);
                    ev.category = e.value("category", "");
                    ev.outcome = e.value("outcome", "");
                    ev.action_ru = e.value("ru", "");
                    ev.action_en = e.value("en", "");
                    person.events.push_back(ev);
                }
            }
            if (!person.name.empty() && !person.birth_date.empty()) out.push_back(std::move(person));
        }
    } catch (...) {}
    return out;
}

// ---------------------------------------------------------------------------
// Per-person Chart / DashaTimeline cache
// ---------------------------------------------------------------------------
struct PersonChart {
    Chart::BirthData birth;
    Chart chart;
    DashaTimeline timeline;
    bool ready = false;
};

std::mutex g_cache_mutex;
std::map<std::string, PersonChart> g_person_charts;

const PersonChart* person_chart(const Person& person) {
    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        auto it = g_person_charts.find(person.name);
        if (it != g_person_charts.end()) return &it->second;
    }
    PersonChart pc;
    pc.birth.name = person.name;
    pc.birth.birth_date = person.birth_date;
    pc.birth.birth_time = person.birth_time;
    pc.birth.city = person.birth_place;
    if (!person.birth_place.empty()) {
        if (auto city = geocode::resolve_city(person.birth_place)) {
            pc.birth.latitude = city->latitude;
            pc.birth.longitude = city->longitude;
            pc.birth.tz_offset = city->tz_offset;
        }
    }
    try {
        pc.chart = compute_chart(pc.birth);
        pc.timeline = build_dasha_timeline(pc.birth, pc.chart);
        pc.ready = !pc.timeline.empty();
    } catch (...) {}
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_person_charts[person.name] = std::move(pc);
    return &g_person_charts[person.name];
}

// ---------------------------------------------------------------------------
// Category keyword matching
// ---------------------------------------------------------------------------
std::string lowercase_folded(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(text[i]);
        if (b < 0x80) {
            out += static_cast<char>(std::tolower(b));
            continue;
        }
        if (i + 1 < text.size()) {
            unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
            if ((b == 0xD0 || b == 0xD1) && b1 == 0x91) {  // Ё/ё -> е
                out += '\xD0';
                out += '\xB5';
                ++i;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

bool kw(const std::string& text, const std::vector<const char*>& words) {
    std::string low = lowercase_folded(text);
    for (const char* w : words) {
        std::string key;
        for (const char* c = w; *c; ++c) {
            if (*c == 'ё') key += std::string("\xD0\xB5");
            else key += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
        }
        if (key.size() >= 3 && low.find(key) != std::string::npos) return true;
    }
    return false;
}

std::optional<Planet> md_planet_at(const DashaTimeline& timeline, int year) {
    std::string date = std::to_string(year) + "-07-01";
    auto period = dasha_at_date(timeline, date);
    if (!period) return std::nullopt;
    return static_cast<Planet>(period->mahadasha);
}

} // namespace

const std::vector<std::string> CATEGORIES = {
    "business", "career", "money", "relationship", "move",
    "study", "creative", "politics", "health", "family"
};

const std::vector<Person>& people() {
    static std::once_flag flag;
    static std::vector<Person> dataset;
    std::call_once(flag, [] { dataset = load_people(); });
    return dataset;
}

std::string categorize(const std::string& text, const std::string& lang) {
    (void)lang;
    // NOTE: words must be OWNED storage. std::initializer_list<const char*>
    // would hang on to a temporary backing array that dies as soon as the
    // static `rules` initializer finishes, leaving dangling pointers that
    // segfault later — exactly what happened in production on the first
    // follow-up question after a horoscope.
    struct Rule { std::string cat; std::vector<const char*> words; };
    static const std::vector<Rule> rules = {
        {"business", {"бизнес", "старт", "компан", "предприят", "фирм", "дельн", "завод", "магазин",
                       "business", "startup", "company", "ventur", "shop", "factory", "start"}},
        {"career", {"карьер", "работ", "професс", "повышен", "должност", "назначен", "увольнен",
                     "career", "job", "promotion", "position", "interview", "fired", "resign"}},
        {"money", {"денег", "деньг", "финанс", "богат", "доход", "прибыл", "заработ", "капитал",
                    "инвестиц", "валюта", "зарплат", "money", "wealth", "income", "invest", "profit",
                    "salary", "capital", "rich"}},
        {"relationship", {"любов", "отношен", "брак", "партнер", "свадьб", "женить", "замуж", "роман",
                           "love", "relationship", "marriage", "partner", "wedding", "romance", "date"}},
        {"move", {"переезд", "переедь", "иммигр", "эмигр", "релок", "квартир", "жиль", "дом", "перебрат",
                   "move", "reloc", "immigr", "abroad", "apartment", "house", "realtor"}},
        {"study", {"учеб", "обучен", "школ", "институт", "универ", "экзамен", "студент", "курс", "диплом",
                    "study", "school", "university", "exam", "student", "degree", "learn"}},
        {"creative", {"творч", "искусств", "музык", "фильм", "книг", "писал", "худож", "картин", "стих",
                       "art", "music", "film", "book", "write", "painting", "create", "poem", "song"}},
        {"politics", {"политик", "президент", "выбор", "власт", "государст", "правитель", "министр",
                       "politics", "president", "election", "government", "minister", "power"}},
        {"health", {"здоров", "болезн", "операц", "лечен", "спорт", "врач", "больниц", "травм", "излеч",
                     "health", "illness", "disease", "surgery", "treatment", "injur", "hospital"}},
        {"family", {"семь", "детей", "ребен", "ребён", "рождени", "матер", "отец", "родствен", "мальчик",
                     "девочк", "family", "child", "children", "kid", "baby", "born", "mother", "father"}},
    };
    for (const auto& r : rules) {
        if (kw(text, r.words)) return r.cat;
    }
    return "";
}

std::string planet_display(Planet p, const std::string& lang) {
    static const char* ru[] = {"Солнце", "Луна", "Марс", "Меркурий", "Юпитер", "Венера", "Сатурн", "Раху", "Кету"};
    static const char* en[] = {"Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn", "Rahu", "Ketu"};
    size_t i = static_cast<size_t>(p);
    if (i >= 9) i = 0;
    return lang == "ru" ? ru[i] : en[i];
}

int target_year(const std::string& text) {
    int found = 0;
    for (size_t i = 0; i + 3 < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
        if (i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1]))) continue;
        bool all_digit = true;
        for (size_t k = 0; k < 4; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(text[i + k]))) { all_digit = false; break; }
        }
        if (!all_digit) continue;
        if ((text[i] == '1' && (text[i + 1] == '8' || text[i + 1] == '9')) || (text[i] == '2' && text[i + 1] == '0')) {
            // the requested/upcoming year usually appears last; prefer the latest year mentioned
            found = std::max(found, std::stoi(text.substr(i, 4)));
        }
    }
    if (found > 0) return found;
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    return tm.tm_year + 1900;
}

std::vector<PersonMatch> famous_matches(const Chart& chart, bool has_birth_time, int top) {
    std::vector<PersonMatch> out;
    std::optional<Planet> user_md;
    try {
        Chart::BirthData b = chart.birth;
        auto tl = build_dasha_timeline(b, chart);
        user_md = md_planet_at(tl, target_year(""));
    } catch (...) {}

    const auto& pMoon = chart.planets[static_cast<size_t>(Planet::Moon)];
    const auto& pSun = chart.planets[static_cast<size_t>(Planet::Sun)];
    Sign user_lagna = chart.ascendant.sign;

    for (const auto& person : people()) {
        if (person.birth_date.empty()) continue;
        const PersonChart* pc = person_chart(person);
        if (!pc || !pc->ready) continue;

        int score = 0;
        std::vector<std::string> rru, ren;
        const auto& fMoon = pc->chart.planets[static_cast<size_t>(Planet::Moon)];
        const auto& fSun = pc->chart.planets[static_cast<size_t>(Planet::Sun)];

        if (has_birth_time && !person.birth_time.empty()) {
            if (pc->chart.ascendant.sign == user_lagna) {
                score += 20;
                rru.push_back("восходящий знак совпадает");
                ren.push_back("same rising sign");
            }
        }
        if (fMoon.sign == pMoon.sign) {
            score += 18;
            rru.push_back("та же Луна в знаке");
            ren.push_back("same Moon sign");
        }
        if (fMoon.nakshatra.nakshatra == pMoon.nakshatra.nakshatra) {
            score += 10;
            rru.push_back("та же накшатра Луны");
            ren.push_back("same Moon nakshatra");
        }
        if (fSun.sign == pSun.sign) {
            score += 6;
            rru.push_back("Солнце в том же знаке");
            ren.push_back("same Sun sign");
        }
        if (user_md) {
            auto p_md = md_planet_at(pc->timeline, target_year(""));
            if (p_md && *p_md == *user_md) {
                score += 8;
                rru.push_back("текущая махадаша совпадает");
                ren.push_back("same current mahadasha");
            }
        }
        if (score >= 10) {
            PersonMatch m;
            m.name = person.name;
            m.score = score;
            m.reasons_ru = rru;
            m.reasons_en = ren;
            out.push_back(std::move(m));
        }
    }
    std::sort(out.begin(), out.end(), [](const PersonMatch& a, const PersonMatch& b) {
        return a.score > b.score;
    });
    if (static_cast<int>(out.size()) > std::max(1, top)) out.resize(static_cast<size_t>(std::max(1, top)));
    return out;
}

std::optional<EventStat> event_stat(const std::string& category, int target_year,
                                    const Chart::BirthData& birth, const Chart& chart,
                                    const std::string& lang) {
    EventStat stat;
    stat.category = category;
    stat.target_year = target_year;
    std::optional<Planet> user_md;
    try {
        auto tl = build_dasha_timeline(birth, chart);
        user_md = md_planet_at(tl, target_year);
    } catch (...) {}
    if (!user_md) return std::nullopt;
    stat.planet = planet_display(*user_md, "en");
    stat.planet_ru = planet_display(*user_md, "ru");

    for (const auto& person : people()) {
        const PersonChart* pc = person_chart(person);
        if (!pc || !pc->ready) continue;
        for (const auto& ev : person.events) {
            if (ev.year <= 0 || ev.category != category) continue;
            auto p_planet = md_planet_at(pc->timeline, ev.year);
            if (!p_planet || *p_planet != *user_md) continue;
            stat.has_data = true;
            std::string action = (lang == "en" && !ev.action_en.empty()) ? ev.action_en : ev.action_ru;
            if (action.empty()) action = ev.action_en.empty() ? ev.action_ru : ev.action_en;
            std::string example = person.name + " — " + action;
            if (ev.outcome == "success") {
                stat.success++;
                if (stat.examples_success.size() < 3) stat.examples_success.push_back(example);
            } else if (ev.outcome == "fail") {
                stat.fail++;
                if (stat.examples_fail.size() < 3) stat.examples_fail.push_back(example);
            } else {
                stat.mixed++;
                if (stat.examples_mixed.size() < 3) stat.examples_mixed.push_back(example);
            }
        }
    }
    return stat.has_data ? std::optional<EventStat>(stat) : std::nullopt;
}

std::string analog_block(const std::string& question, const Chart::BirthData& birth,
                         const Chart& chart, const std::string& lang,
                         nlohmann::json& out_analogs) {
    std::string category = categorize(question, lang);
    int ty = target_year(question);
    bool has_time = !birth.birth_time.empty();

    auto matches = famous_matches(chart, has_time, 3);
    auto stat = category.empty() ? std::optional<EventStat>(std::nullopt)
                                 : event_stat(category, ty, birth, chart, lang);

    out_analogs["target_year"] = ty;
    out_analogs["category"] = category;
    nlohmann::json mj = nlohmann::json::array();
    for (const auto& m : matches) {
        nlohmann::json o;
        o["name"] = m.name;
        o["score"] = m.score;
        o["reasons"] = lang == "ru" ? m.reasons_ru : m.reasons_en;
        mj.push_back(std::move(o));
    }
    out_analogs["matches"] = mj;
    if (stat) {
        out_analogs["stat"] = {
            {"category", stat->category},
            {"planet", stat->planet},
            {"planet_ru", stat->planet_ru},
            {"target_year", stat->target_year},
            {"success", stat->success},
            {"fail", stat->fail},
            {"mixed", stat->mixed},
            {"examples_success", stat->examples_success},
            {"examples_fail", stat->examples_fail},
            {"examples_mixed", stat->examples_mixed},
        };
    }

    if (matches.empty() && !stat) return "";

    std::string block;
    if (lang == "ru") {
        block = "\n--- АНАЛИЗ ПО ЗАКОНУ БОЛЬШИХ ЧИСЕЛ (известные люди) ---\n";
        if (!matches.empty()) {
            block += "Карта похожа на карты: ";
            for (size_t i = 0; i < matches.size(); ++i) {
                if (i) block += ", ";
                block += matches[i].name;
            }
            block += ".\n";
        }
        if (stat) {
            block += "В " + std::to_string(ty) + " году у человека идёт махадаша планеты " + stat->planet_ru +
                     ". У известных людей, у которых в год события была та же махадаша, события категории «" +
                     category + "» складывались так: успех — " + std::to_string(stat->success) +
                     ", провал — " + std::to_string(stat->fail) +
                     ", смешанно — " + std::to_string(stat->mixed) + ".\n";
            if (!stat->examples_success.empty()) {
                block += "Примеры успеха: " + oracle::join(stat->examples_success, "; ") + ".\n";
            }
            if (!stat->examples_fail.empty()) {
                block += "Примеры провалов: " + oracle::join(stat->examples_fail, "; ") + ".\n";
            }
            if (!stat->examples_mixed.empty()) {
                block += "Смешанные исходы: " + oracle::join(stat->examples_mixed, "; ") + ".\n";
            }
        }
        block += "---\n";
    } else {
        block = "\n--- LAW OF LARGE NUMBERS ANALYSIS (famous people) ---\n";
        if (!matches.empty()) {
            block += "Your chart resembles the charts of: ";
            for (size_t i = 0; i < matches.size(); ++i) {
                if (i) block += ", ";
                block += matches[i].name;
            }
            block += ".\n";
        }
        if (stat) {
            block += "In " + std::to_string(ty) + " the person runs the mahadasha of " + stat->planet +
                     ". Among famous people who were running the same mahadasha in the year of their event, "
                     "outcomes in the '" + category + "' category were: success — " + std::to_string(stat->success) +
                     ", fail — " + std::to_string(stat->fail) +
                     ", mixed — " + std::to_string(stat->mixed) + ".\n";
            if (!stat->examples_success.empty()) {
                block += "Success examples: " + oracle::join(stat->examples_success, "; ") + ".\n";
            }
            if (!stat->examples_fail.empty()) {
                block += "Failure examples: " + oracle::join(stat->examples_fail, "; ") + ".\n";
            }
            if (!stat->examples_mixed.empty()) {
                block += "Mixed outcomes: " + oracle::join(stat->examples_mixed, "; ") + ".\n";
            }
        }
        block += "---\n";
    }
    return block;
}

} // namespace jyotish::famous