#include <jyotish/oracle.hpp>
#include <jyotish/parsing.hpp>
#include <jyotish/i18n.hpp>
#include <jyotish/geocode.hpp>
#include <regex>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <set>
#include <cstdlib>

namespace jyotish::oracle {

namespace {

std::string to_lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        if (b0 == 0xD0 && i + 1 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if (b1 == 0x81) { out += '\xD1'; out += '\x91'; i += 2; continue; } // Ё -> ё
            if (b1 >= 0x90 && b1 <= 0x9F) { out += '\xD0'; out += static_cast<char>(b1 + 0x20); i += 2; continue; } // А-П -> а-п
            if (b1 >= 0xA0 && b1 <= 0xAF) { out += '\xD1'; out += static_cast<char>(b1 - 0x20); i += 2; continue; } // Р-Я -> р-я
            out += static_cast<char>(b0); out += static_cast<char>(b1); i += 2; continue;
        }
        out += static_cast<char>(std::tolower(b0));
        ++i;
    }
    return out;
}

std::size_t utf8_len(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string upper_first_utf8(const std::string& s) {
    if (s.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(s[0]);
        unsigned char b1 = static_cast<unsigned char>(s[1]);
        if (b0 == 0xD0 && b1 >= 0xB0 && b1 <= 0xBF) { // а-п -> А-П
            std::string r = s; r[1] = static_cast<char>(b1 - 0x20); return r;
        }
        if (b0 == 0xD1 && b1 >= 0x80 && b1 <= 0x8F) { // р-я -> Р-Я
            std::string r = s; r[0] = '\xD0'; r[1] = static_cast<char>(b1 + 0x20); return r;
        }
        if (b0 == 0xD1 && b1 == 0x91) { std::string r = s; r[0] = '\xD0'; r[1] = '\x81'; return r; } // ё -> Ё
    }
    if (!s.empty()) { std::string r = s; r[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r[0]))); return r; }
    return s;
}

std::string norm(const std::string& s) {
    std::string result;
    bool in_space = false;
    for (size_t i = 0; i < s.size();) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        // Fullwidth digits U+FF10..U+FF19 -> ASCII '0'..'9'
        if (b0 == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBC) {
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            if (b2 >= 0x90 && b2 <= 0x99) {
                result += static_cast<char>('0' + (b2 - 0x90));
                i += 3;
                in_space = false;
                continue;
            }
        }
        if (std::isspace(b0)) {
            if (!in_space) {
                result += ' ';
                in_space = true;
            }
        } else {
            result += static_cast<char>(b0);
            in_space = false;
        }
        ++i;
    }
    result = to_lower_utf8(result);
    size_t start = result.find_first_not_of(' ');
    size_t end = result.find_last_not_of(' ');
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

int shift_hour(int hour, const std::string& tod) {
    std::string t = tod;
    t = to_lower_utf8(t);
    if (t == "pm" || t == "вечера" || t == "вечером" || t == "вечер" || t == "evening") {
        return hour < 12 ? hour + 12 : hour;
    }
    if (t == "дня" || t == "днём" || t == "днем" || t == "afternoon") {
        return hour < 12 ? hour + 12 : hour;
    }
    if (t == "ночи" || t == "ночью" || t == "night" || t == "ночь") {
        if (hour == 12) return 0;
        return (4 <= hour && hour < 12) ? hour + 12 : hour;
    }
    if (t == "утра" || t == "утром" || t == "утро" || t == "am" || t == "morning") {
        if (hour == 12) return 0;
        return hour;
    }
    return hour;
}

struct MonthEntry { const char* name; int idx; };

// Month names in many languages/locales (nominative, genitive and common
// abbreviations). idx is 0-based (january = 0).
const std::vector<MonthEntry> MONTH_NAMES = {
    // Russian
    {"январь",0},{"января",0},{"янв",0},
    {"февраль",1},{"февраля",1},{"фев",1},{"февр",1},
    {"март",2},{"марта",2},{"мар",2},
    {"апрель",3},{"апреля",3},{"апр",3},
    {"май",4},{"мая",4},{"маю",4},
    {"июнь",5},{"июня",5},{"июн",5},
    {"июль",6},{"июля",6},{"июл",6},
    {"август",7},{"августа",7},{"авг",7},
    {"сентябрь",8},{"сентября",8},{"сен",8},{"сент",8},
    {"октябрь",9},{"октября",9},{"окт",9},
    {"ноябрь",10},{"ноября",10},{"ноя",10},{"нояб",10},
    {"декабрь",11},{"декабря",11},{"дек",11},
    // English
    {"january",0},{"jan",0},
    {"february",1},{"feb",1},
    {"march",2},{"mar",2},
    {"april",3},{"apr",3},
    {"may",4},
    {"june",5},{"jun",5},
    {"july",6},{"jul",6},
    {"august",7},{"aug",7},
    {"september",8},{"sep",8},{"sept",8},
    {"october",9},{"oct",9},
    {"november",10},{"nov",10},
    {"december",11},{"dec",11},
    // Ukrainian
    {"січень",0},{"січня",0},{"січ",0},
    {"лютий",1},{"лютого",1},{"лют",1},
    {"березень",2},{"березня",2},{"бер",2},
    {"квітень",3},{"квітня",3},{"кві",3},
    {"травень",4},{"травня",4},{"тра",4},
    {"червень",5},{"червня",5},{"чер",5},
    {"липень",6},{"липня",6},{"лип",6},
    {"серпень",7},{"серпня",7},{"сер",7},
    {"вересень",8},{"вересня",8},{"вер",8},
    {"жовтень",9},{"жовтня",9},{"жов",9},
    {"листопад",10},{"листопада",10},{"лис",10},
    {"грудень",11},{"грудня",11},{"гру",11},
    // Belarusian
    {"студзень",0},{"студзеня",0},{"сту",0},
    {"люты",1},{"лют",1},
    {"сакавік",2},{"сакавіка",2},{"сак",2},
    {"красавік",3},{"красавіка",3},{"кра",3},
    {"май",4},{"мая",4},{"май",4},
    {"чэрвень",5},{"чэрвеня",5},{"чэр",5},
    {"ліпень",6},{"ліпеня",6},{"ліп",6},
    {"жнівень",7},{"жніўня",7},{"жні",7},
    {"верасень",8},{"верасня",8},{"вер",8},
    {"кастрычнік",9},{"кастрычніка",9},{"кас",9},
    {"лістапад",10},{"лістапада",10},{"ліс",10},
    {"снежань",11},{"снежня",11},{"сне",11},
    // German
    {"januar",0},{"jan",0},
    {"februar",1},{"feb",1},
    {"märz",2},{"mär",2},
    {"april",3},{"apr",3},
    {"mai",4},
    {"juni",5},{"jun",5},
    {"juli",6},{"jul",6},
    {"august",7},{"aug",7},
    {"september",8},{"sep",8},
    {"oktober",9},{"okt",9},
    {"november",10},{"nov",10},
    {"dezember",11},{"dez",11},
    // French
    {"janvier",0},{"janv",0},
    {"février",1},{"fevrier",1},{"févr",1},{"fev",1},
    {"mars",2},
    {"avril",3},{"avr",3},
    {"mai",4},
    {"juin",5},
    {"juillet",6},{"juil",6},
    {"août",7},{"aout",7},
    {"septembre",8},{"sept",8},
    {"octobre",9},{"oct",9},
    {"novembre",10},{"nov",10},
    {"décembre",11},{"decembre",11},{"déc",11},{"dec",11},
    // Spanish
    {"enero",0},{"ene",0},
    {"febrero",1},{"feb",1},
    {"marzo",2},{"mar",2},
    {"abril",3},{"abr",3},
    {"mayo",4},{"may",4},
    {"junio",5},{"jun",5},
    {"julio",6},{"jul",6},
    {"agosto",7},{"ago",7},
    {"septiembre",8},{"setiembre",8},{"sep",8},
    {"octubre",9},{"oct",9},
    {"noviembre",10},{"nov",10},
    {"diciembre",11},{"dic",11},
    // Italian
    {"gennaio",0},{"gen",0},
    {"febbraio",1},{"feb",1},
    {"marzo",2},{"mar",2},
    {"aprile",3},{"apr",3},
    {"maggio",4},{"mag",4},
    {"giugno",5},{"giu",5},
    {"luglio",6},{"lug",6},
    {"agosto",7},{"ago",7},
    {"settembre",8},{"set",8},
    {"ottobre",9},{"ott",9},
    {"novembre",10},{"nov",10},
    {"dicembre",11},{"dic",11},
    // Portuguese
    {"janeiro",0},{"jan",0},
    {"fevereiro",1},{"fev",1},
    {"março",2},{"marco",2},{"mar",2},
    {"abril",3},{"abr",3},
    {"maio",4},{"mai",4},
    {"junho",5},{"jun",5},
    {"julho",6},{"jul",6},
    {"agosto",7},{"ago",7},
    {"setembro",8},{"set",8},
    {"outubro",9},{"out",9},
    {"novembro",10},{"nov",10},
    {"dezembro",11},{"dez",11},
    // Dutch
    {"januari",0},{"jan",0},
    {"februari",1},{"feb",1},
    {"maart",2},{"maa",2},
    {"april",3},{"apr",3},
    {"mei",4},
    {"juni",5},{"jun",5},
    {"juli",6},{"jul",6},
    {"augustus",7},{"aug",7},
    {"september",8},{"sep",8},
    {"oktober",9},{"okt",9},
    {"november",10},{"nov",10},
    {"december",11},{"dec",11},
    // Polish
    {"styczeń",0},{"stycznia",0},{"sty",0},
    {"luty",1},{"lutego",1},{"lut",1},
    {"marzec",2},{"marca",2},{"mar",2},
    {"kwiecień",3},{"kwietnia",3},{"kwi",3},
    {"maj",4},
    {"czerwiec",5},{"czerwca",5},{"cze",5},
    {"lipiec",6},{"lipca",6},{"lip",6},
    {"sierpień",7},{"sierpnia",7},{"sie",7},
    {"wrzesień",8},{"września",8},{"wrz",8},
    {"październik",9},{"października",9},{"paź",9},
    {"listopad",10},{"listopada",10},{"lis",10},
    {"grudzień",11},{"grudnia",11},{"gru",11},
    // Turkish
    {"ocak",0},{"şubat",1},{"subat",1},{"mart",2},{"nisan",3},{"mayıs",4},{"mayis",4},
    {"haziran",5},{"temmuz",6},{"ağustos",7},{"agustos",7},{"eylül",8},{"eylul",8},
    {"ekim",9},{"kasım",10},{"kasim",10},{"aralık",11},{"aralik",11}
};

std::string utf8_prefix(const std::string& s, size_t n) {
    size_t i = 0, cnt = 0;
    while (i < s.size() && cnt < n) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        ++cnt;
    }
    return s.substr(0, i);
}

std::optional<int> match_month(const std::string& mon_word) {
    std::string mw = mon_word;
    if (mw.empty()) return std::nullopt;
    mw = mw.substr(0, mw.find_first_of(".,;/"));
    mw = to_lower_utf8(mw);
    if (mw.empty()) return std::nullopt;

    // Pass 1: exact match (avoids 3-char prefix collisions like juin/juillet).
    for (const auto& e : MONTH_NAMES)
        if (mw == e.name) return e.idx;

    // Pass 2: UTF-8 aware 3-character prefix (handles declensions/abbrevs:
    // август/августа, января/январь, sept/septembre ...).
    std::string p3 = utf8_prefix(mw, 3);
    if (p3.size() >= 2)
        for (const auto& e : MONTH_NAMES)
            if (utf8_prefix(e.name, 3) == p3) return e.idx;

    return std::nullopt;
}

} // namespace

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> words;
    std::istringstream iss(s);
    std::string word;
    while (iss >> word) words.push_back(word);
    return words;
}

std::optional<std::string> extract_time(const std::string& text) {
    std::string t = norm(text);
    std::smatch m;

    auto pad2 = [](int h) { return (h < 10 ? "0" : "") + std::to_string(h); };

    // HH:MM (raw text keeps the colon)
    if (std::regex_search(text, m, std::regex(R"((\d{1,2}):(\d{2}))"))) {
        int h = std::stoi(m[1].str());
        if (h >= 0 && h <= 23) return pad2(h) + ":" + m[2].str();
    }

    // HH.MM / HH,MM ("в 12.00", "в 2.30", "примерно в 12,45") — people type a
    // dot instead of a colon. Must not mistake a date (DD.MM.YYYY) for time:
    // a candidate is only a time when it is not glued to another number via a
    // dot/comma on the left (day part) and not followed by ".YYYY" (year).
    {
        static const std::regex time_dot(R"((\d{1,2})[.,](\d{2}))");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), time_dot), end = std::sregex_iterator();
             it != end; ++it) {
            int h = std::stoi(it->str(1));
            if (h < 0 || h > 23) continue;
            int min = std::stoi(it->str(2));
            if (min < 0 || min > 59) continue;
            size_t b = static_cast<size_t>(it->position());
            size_t e = b + it->length();
            // Left neighbour: "...5.1<2.00>..." — the digit before ".12" belongs
            // to a day, so "12.00" here is part of "05.12.2000", not a time.
            if (b >= 2 && (text[b - 1] == '.' || text[b - 1] == ',') && std::isdigit(static_cast<unsigned char>(text[b - 2])))
                continue;
            // Right neighbour: "<12.00>.2000" — a year follows, it's a date.
            if (text.size() >= e + 1 && text[e] == '.' && text.size() >= e + 5 &&
                std::isdigit(static_cast<unsigned char>(text[e + 1])) &&
                std::isdigit(static_cast<unsigned char>(text[e + 2])) &&
                std::isdigit(static_cast<unsigned char>(text[e + 3])) &&
                std::isdigit(static_cast<unsigned char>(text[e + 4])))
                continue;
            return pad2(h) + ":" + it->str(2);
        }
    }

    // Digit + "час/ч/hours" + optional time-of-day token
    static const std::regex digit_hour(R"((\d{1,2})\s*(?:час(?:а|ов|ах)?|hours?|ч\.?)\s*([^\s,]+)?)");
    if (std::regex_search(t, m, digit_hour)) {
        int h = std::stoi(m[1].str());
        std::string tod = m[2].matched ? m[2].str() : "";
        return pad2(shift_hour(h, tod)) + ":00";
    }

    // Tokenize (lowercased by norm) for word-based matching, then strip
    // punctuation. Regex \b does not treat Cyrillic as word characters, so
    // tokens are the reliable way to match Russian words.
    std::vector<std::string> toks;
    {
        std::istringstream is(t);
        std::string w;
        while (is >> w) {
            std::string c;
            for (unsigned char ch : w)
                if (std::isalnum(ch) || ch >= 0x80) c += static_cast<char>(ch);
            if (!c.empty()) toks.push_back(c);
        }
    }
    auto is_tod = [](const std::string& w) {
        static const std::set<std::string> t = {
            "pm", "am", "вечера", "вечером", "вечер", "evening",
            "дня", "днём", "днем", "afternoon",
            "ночи", "ночью", "night", "ночь",
            "утра", "утром", "утро", "morning"
        };
        return t.count(w) > 0;
    };

    // "<hour> <time-of-day>", e.g. "5 утра", "5 вечера"
    for (size_t i = 0; i + 1 < toks.size(); ++i) {
        const std::string& w = toks[i];
        if (w.empty() || !std::isdigit(static_cast<unsigned char>(w[0]))) continue;
        int h = std::atoi(w.c_str());
        if (h < 0 || h > 23) continue;
        if (is_tod(toks[i + 1])) return pad2(shift_hour(h, toks[i + 1])) + ":00";
    }

    // Word hour ("полночь", "noon", "один", ...)
    static const std::map<std::string, int> hour_words = {
        {"полночь", 0}, {"midnight", 0}, {"полдень", 12}, {"noon", 12}, {"midday", 12},
        {"час", 1}, {"одна", 1}, {"один", 1}, {"one", 1}, {"два", 2}, {"две", 2}, {"two", 2},
        {"три", 3}, {"three", 3}, {"четыре", 4}, {"four", 4}, {"пять", 5}, {"five", 5},
        {"шесть", 6}, {"six", 6}, {"семь", 7}, {"seven", 7}, {"восемь", 8}, {"eight", 8},
        {"девять", 9}, {"nine", 9}, {"десять", 10}, {"ten", 10}, {"одиннадцать", 11}, {"eleven", 11},
        {"двенадцать", 12}, {"twelve", 12}
    };
    for (const auto& w : toks)
        if (auto it = hour_words.find(w); it != hour_words.end())
            return pad2(it->second) + ":00";

    // AM/PM (e.g. "5am", "5 pm")
    if (std::regex_search(text, m, std::regex(R"((\d{1,2})\s*(am|pm))", std::regex::icase))) {
        int h = std::stoi(m[1].str());
        std::string ap = to_lower_utf8(m[2].str());
        if (ap == "pm" && h < 12) h += 12;
        if (ap == "am" && h == 12) h = 0;
        return pad2(h) + ":00";
    }

    // Time-of-day defaults
    for (const auto& w : toks) {
        if (w == "полдень" || w == "noon" || w == "midday") return "12:00";
        if (w == "полночь" || w == "midnight") return "00:00";
        if (w == "утром" || w == "утро" || w == "утра" || w == "morning") return "09:00";
        if (w == "вечером" || w == "вечер" || w == "вечера" || w == "evening") return "19:00";
        if (w == "ночью" || w == "ночь" || w == "ночи" || w == "night") return "23:00";
        if (w == "днём" || w == "днем" || w == "дня" || w == "afternoon") return "14:00";
    }

    return std::nullopt;
}

int shift_hour(int hour, const std::string& tod) {
    std::string t = tod;
    t = to_lower_utf8(t);
    if (t == "pm" || t == "вечера" || t == "вечером" || t == "вечер" || t == "evening") {
        return hour < 12 ? hour + 12 : hour;
    }
    if (t == "дня" || t == "днём" || t == "днем" || t == "afternoon") {
        return hour < 12 ? hour + 12 : hour;
    }
    if (t == "ночи" || t == "ночью" || t == "night" || t == "ночь") {
        if (hour == 12) return 0;
        return (4 <= hour && hour < 12) ? hour + 12 : hour;
    }
    if (t == "утра" || t == "утром" || t == "утро" || t == "am" || t == "morning") {
        if (hour == 12) return 0;
        return hour;
    }
    return hour;
}

std::optional<std::pair<std::chrono::sys_days, std::string>> parse_relative_date(const std::string& text, std::chrono::sys_days base) {
    std::string t = norm(text);
    std::map<std::string, int> deltas = {
        {"послезавтра", 2}, {"day after tomorrow", 2}, {"завтра", 1}, {"tomorrow", 1},
        {"сегодня", 0}, {"сейчас", 0}, {"today", 0}, {"now", 0}
    };
    for (const auto& [word, d] : deltas) {
        if (std::regex_search(t, std::regex("\\b" + word + "\\b"))) {
            auto time_str = extract_time(t);
            return std::make_pair(base + std::chrono::days(d), time_str.value_or(""));
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::chrono::sys_days, std::string>> parse_iso_date(const std::string& text) {
    std::string t = norm(text);
    std::smatch m;

    auto now_days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    int cur_year = static_cast<int>(std::chrono::year_month_day{now_days}.year());
    auto expand_year = [cur_year](int y) {
        if (y >= 100) return y;
        int pivot = cur_year % 100;
        return y <= pivot ? 2000 + y : 1900 + y;
    };
    auto make = [&](int y, int mon, int d) -> std::optional<std::pair<std::chrono::sys_days, std::string>> {
        if (mon < 1 || mon > 12 || d < 1 || d > 31) return std::nullopt;
        auto time_str = extract_time(t);
        return std::make_pair(std::chrono::sys_days{std::chrono::year{y}/mon/d}, time_str.value_or(""));
    };

    // CJK ideographic dates: 1995年8月15日 / 1995년 8월 15일 / 8月15日.
    // Note: multi-byte characters must be alternated literally, not put in a
    // character class, because std::regex treats a class byte-wise.
    if (std::regex_search(t, m, std::regex(R"((?:(\d{4})\s*(?:年|년))?\s*(\d{1,2})\s*(?:月|월)\s*(\d{1,2})\s*(?:日|일)?)"))) {
        int y = m[1].matched ? std::stoi(m[1]) : cur_year;
        if (auto r = make(y, std::stoi(m[2]), std::stoi(m[3]))) return r;
    }

    // Compact digits: YYYYMMDD or DDMMYYYY
    if (std::regex_search(t, m, std::regex(R"(\b(\d{8})\b)"))) {
        const std::string s = m[1];
        int a = std::stoi(s.substr(0, 4));
        if (a >= 1900 && a <= 2100) {
            if (auto r = make(a, std::stoi(s.substr(4, 2)), std::stoi(s.substr(6, 2)))) return r;
        }
        int d = std::stoi(s.substr(0, 2)), mon = std::stoi(s.substr(2, 2));
        if (mon > 12 && d <= 12) std::swap(d, mon);   // MMDDYYYY
        if (auto r = make(std::stoi(s.substr(4, 4)), mon, d)) return r;
    }

    // Compact digits: DDMMYY
    if (std::regex_search(t, m, std::regex(R"(\b(\d{6})\b)"))) {
        const std::string s = m[1];
        if (auto r = make(expand_year(std::stoi(s.substr(4, 2))), std::stoi(s.substr(2, 2)), std::stoi(s.substr(0, 2))))
            return r;
    }

    // YYYY-MM-DD (also YYYY.MM.DD / YYYY/MM/DD; the trailing check avoids \b,
    // which fails before letters, e.g. in an ISO "1995-08-15T14:30")
    if (std::regex_search(t, m, std::regex(R"(\b(\d{4})[./-](\d{1,2})[./-](\d{1,2})(?![0-9]))")))
        return make(std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]));

    // DD<sep>MM<sep>YYYY, sep = . / - , 2- or 4-digit year
    if (std::regex_search(t, m, std::regex(R"(\b(\d{1,2})\s*[./-]\s*(\d{1,2})\s*[./-]\s*(\d{2,4})\b)"))) {
        int a = std::stoi(m[1]), b = std::stoi(m[2]), y = expand_year(std::stoi(m[3]));
        int d = a, mon = b;
        if (mon > 12 && d <= 12) { std::swap(d, mon); }   // US MM/DD/YYYY
        if (auto r = make(y, mon, d)) return r;
    }

    // DD MM YYYY separated by spaces
    if (std::regex_search(t, m, std::regex(R"(\b(\d{1,2})\s+(\d{1,2})\s+(\d{2,4})\b)"))) {
        int a = std::stoi(m[1]), b = std::stoi(m[2]), y = expand_year(std::stoi(m[3]));
        int d = a, mon = b;
        if (mon > 12 && d <= 12) { std::swap(d, mon); }
        if (auto r = make(y, mon, d)) return r;
    }

    // DD month YYYY
    {
        std::regex re(R"((\d{1,2})\s+([^\s,]+)[\s,]+(\d{2,4})\b)");
        for (auto it = std::sregex_iterator(t.begin(), t.end(), re), end = std::sregex_iterator(); it != end; ++it) {
            auto mi = match_month((*it)[2]);
            if (!mi) continue;   // e.g. "36 лет родился …" must not block "24 июля"
            int y = expand_year(std::stoi((*it)[3]));
            if (auto r = make(y, *mi + 1, std::stoi((*it)[1]))) return r;
        }
    }

    // DD month (year omitted). Note: no trailing \b, because Cyrillic bytes
    // are not regex "word" characters, so \b after them never matches.
    {
        std::regex re(R"((\d{1,2})\s+([^\s,]+))");
        for (auto it = std::sregex_iterator(t.begin(), t.end(), re), end = std::sregex_iterator(); it != end; ++it) {
            auto mi = match_month((*it)[2]);
            if (!mi) continue;   // keep scanning past e.g. "мне 36 лет"
            int mon = *mi + 1, d = std::stoi((*it)[1]);
            int y = cur_year;
            auto dt = std::chrono::sys_days{std::chrono::year{y}/mon/d};
            if (dt < now_days - std::chrono::days(60)) y++;
            if (auto r = make(y, mon, d)) return r;
        }
    }

    // month DD YYYY (e.g. "august 15 1995", "августа 15 1995")
    {
        std::regex re(R"(([^\s,]+)\s+(\d{1,2})[\s,]+(\d{2,4})\b)");
        for (auto it = std::sregex_iterator(t.begin(), t.end(), re), end = std::sregex_iterator(); it != end; ++it) {
            auto mi = match_month((*it)[1]);
            if (!mi) continue;
            int y = expand_year(std::stoi((*it)[3]));
            if (auto r = make(y, *mi + 1, std::stoi((*it)[2]))) return r;
        }
    }

    // DD.MM / DD-MM / DD/MM with the year omitted (e.g. "15.08", "24-07") —
    // used when the user gives age and only day+month. The second component
    // must be a plausible month (1..12) so a time like "2.30" is NOT taken as
    // a date, and a full date-with-year (matched above) is not re-consumed.
    {
        std::regex re(R"(\b(\d{1,2})[./-](\d{2})(?![./-]\d{2}))");
        for (auto it = std::sregex_iterator(t.begin(), t.end(), re), end = std::sregex_iterator(); it != end; ++it) {
            int a = std::stoi((*it)[1]), b = std::stoi((*it)[2]);
            if (b < 1 || b > 12) continue;          // ".30" is minutes, not a month
            int d = a, mon = b;
            if (mon > 12 && d <= 12) { std::swap(d, mon); }
            if (d > 31) continue;
            if (mon == 2 && d > 29) continue;
            if (auto r = make(cur_year, mon, d)) return r;
        }
    }
    return std::nullopt;
}

std::string extract_name(const std::string& text) {
    std::string lowered = text;
    lowered = to_lower_utf8(lowered);
    
    static const std::vector<std::string> patterns = {
        R"((?:меня\s+зовут|мо[её]\s+имя|зовут)\s+([a-z\xD0\xD1\x80-\xBF]+))",
        R"((?:my\s+name\s+is|i'?m\s+called)\s+([a-z]+))",
        R"((?:имя|name)\s*[:\-]\s*([a-z\xD0\xD1\x80-\xBF]+))",
        R"(^\s*([\xD0\xD1\x80-\xBF]+)[,.\s]\s*(?:родил|род[\xD0\xD1\x80-\xBF]*\s|появил))",
        R"(^\s*([a-z]+)[,.\s]\s*(?:was\s+born|born))",
        R"(^\s*(?:(?:привет|здравств|добрый|драсти|салют|дарова|hello|hi|hey|howdy|good\s+(?:morning|afternoon|evening))[,!.:\s]+)?(?:я|i)\s+(?:это|этой|этого)?\s*([a-z\xD0\xD1\x80-\xBF]+))"
    };
    
    static const std::set<std::string> stop_words = {
        "я", "ты", "он", "она", "мы", "вы", "они", "это", "i", "you", "we", "he", "she", "they",
        "здравств", "привет", "добрый", "сегодня", "меня", "меняс", "мая", "сегодня",
        "родился", "родилась", "родились", "рожден", "рождена", "живу", "из", "сейчас",
        "был", "была", "были", "хочу", "могу", "умею", "родила", "родил", "родом",
        "думаю", "знаю", "надеюсь", "замужем", "женат", "готов", "согласен", "здесь", "там",
        "am", "from", "born"
    };
    
    for (const auto& pat : patterns) {
        std::smatch m;
        if (std::regex_search(lowered, m, std::regex(pat, std::regex::icase))) {
            std::string name = m[1].str();
            if (utf8_len(name) >= 2 && stop_words.find(name) == stop_words.end()) {
                return upper_first_utf8(name);
            }
        }
    }
    return "";
}

std::string guess_gender(const std::string& name_or_text) {
    std::string t = to_lower_utf8(name_or_text);
    // Explicit self-descriptions win over any name-based guess.
    if (t.find("мужчин") != std::string::npos || t.find("парн") != std::string::npos ||
        t.find("мальчик") != std::string::npos || t.find("юнош") != std::string::npos)
        return "male";
    if (t.find("женщин") != std::string::npos || t.find("девушк") != std::string::npos ||
        t.find("девочк") != std::string::npos)
        return "female";
    // "родился"/"родилась" agree in gender with the speaker.
    if (t.find("родился") != std::string::npos && t.find("родилась") == std::string::npos) return "male";
    if (t.find("родилась") != std::string::npos && t.find("родился") == std::string::npos) return "female";
    if (t.find("рожден") != std::string::npos && t.find("рождена") == std::string::npos) return "male";
    if (t.find("рождена") != std::string::npos && t.find("рожден ") == std::string::npos) return "female";

    // Name morphology fallback — only when the whole text is roughly a single
    // word (a name), to avoid guessing from arbitrary sentences.
    std::string name = norm(t);
    while (!name.empty() && !std::isalpha(static_cast<unsigned char>(name.back()))) name.pop_back();
    if (name.find(' ') != std::string::npos || name.size() < 3) return "";

    // Russian male names/diminutives ending in -а/-я/-ей that would otherwise
    // look feminine: Илья, Никита, Ваня, Даня, Вася, Паша, Саша, Женя, ...
    static const std::set<std::string> male_exceptions = {
        "илья", "никита", "ваня", "даня", "вася", "паша", "саша", "женя", "валя", "толя",
        "коля", "дима", "тёма", "лёва", "лёша", "вова", "кузьма", "фома", "тоша", "демьян"
    };
    if (male_exceptions.count(name)) return "male";

    const std::string last2 = name.size() >= 2 ? name.substr(name.size() - 2) : name;
    static const std::set<std::string> fem_last2 = {
        "ая", "ья", "ия", "на", "ра", "ла", "ша", "та", "ка", "ля",
        "ня", "са", "ва", "да", "за", "ма", "па", "фа", "ца", "ча", "ща", "ея", "юя"
    };
    if (fem_last2.count(last2)) return "female";

    const char last = name.back();
    static const std::string cons = "бвгджзклмнпрстфхцчшщ";
    if (last == 'й' || cons.find(last) != std::string::npos) return "male";
    if (last == 'а' || last == 'я' || last == 'и') return "female";  // оканчивается на -а/-я
    return "";
}

std::optional<int> extract_age(const std::string& text) {
    std::string t = norm(text);
    std::smatch m;
    // "мне 36", "мне 36 лет / года / год" (no \b: Cyrillic bytes are not word chars)
    static const std::regex pat_me(R"((?:^|[^0-9])мне\s+(\d{1,3})\b)");
    // "36 лет", "25 years old", "30 y/o" (standalone 1-3 digit number; the
    // trailing \b is omitted — Cyrillic bytes are not word chars in std::regex,
    // so a \b after "лет" fails when followed by a comma or text end)
    static const std::regex pat_age(R"(\b(\d{1,3})\b\s*(?:лет|года|год|годов|years?\s*(?:old)?|y\s*\/\s*o|yo))");
    if (std::regex_search(t, m, pat_me)) {
        int a = std::stoi(m[1]);
        if (a >= 1 && a <= 120) return a;
    }
    if (std::regex_search(t, m, pat_age)) {
        int a = std::stoi(m[1]);
        if (a >= 1 && a <= 120) return a;
    }
    return std::nullopt;
}

bool has_explicit_year(const std::string& text) {
    std::string t = norm(text);
    // Four-digit year, e.g. 1990, 2024.
    if (std::regex_search(t, std::regex(R"(\b(?:19|20)\d{2}\b)"))) return true;
    // Compact DD.MM.YY / DD-MM-YY with a separator.
    if (std::regex_search(t, std::regex(R"(\b\d{1,2}[./-]\d{1,2}[./-]\d{2}\b)"))) return true;
    // "24 июля 95" — day, month name, two-digit year.
    if (std::regex_search(t, std::regex(R"(\b\d{1,2}\s+[^\s,]+[\s,]\d{2}\b)"))) return true;
    return false;
}

std::optional<jyotish::geocode::CityInfo> extract_city(const std::string& text, bool prefer_last) {
    std::string t = norm(text);
    auto raw = split_words(t);
    // Capitalisation of each token in the original text (a proper noun is a
    // strong hint that a small-town match is really the answer, not a common
    // word like "какая"). Built together with the tokens so indexes stay aligned.
    std::vector<std::string> raw_orig = split_words(text);
    std::vector<std::string> words;
    std::vector<bool> cap;
    words.reserve(raw.size());
    cap.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const std::string& w = raw[i];
        size_t a = 0, b = w.size();
        while (a < b && !(std::isalnum(static_cast<unsigned char>(w[a])) || static_cast<unsigned char>(w[a]) >= 0x80)) ++a;
        while (b > a && !(std::isalnum(static_cast<unsigned char>(w[b - 1])) || static_cast<unsigned char>(w[b - 1]) >= 0x80)) --b;
        if (b <= a) continue;
        std::string tok = w.substr(a, b - a);
        // Drop pure-punctuation tokens ("—", "…", "«»"): normalize_city turns a
        // dash into an empty string, and "йена —" must not read as the city Йена.
        if (jyotish::geocode::normalize_city(tok).empty()) continue;
        bool up = false;
        if (i < raw_orig.size()) {
            const std::string& o = raw_orig[i];
            size_t oa = 0, ob = o.size();
            while (oa < ob && !(std::isalnum(static_cast<unsigned char>(o[oa])) || static_cast<unsigned char>(o[oa]) >= 0x80)) ++oa;
            if (oa < ob) {
                unsigned char c0 = static_cast<unsigned char>(o[oa]);
                unsigned char c1 = (oa + 1 < ob) ? static_cast<unsigned char>(o[oa + 1]) : 0;
                up = (c0 >= 'A' && c0 <= 'Z') ||
                     (c0 == 0xD0 && ((c1 >= 0x90 && c1 <= 0x9F) || c1 == 0x81)) ||
                     (c0 == 0xD1 && c1 >= 0x80 && c1 <= 0x8F);
            }
        }
        words.push_back(std::move(tok));
        cap.push_back(up);
    }
    if (words.empty()) return std::nullopt;

    static const std::set<std::string> stop = {
        "город", "село", "деревня", "станция", "имени", "улица", "проспект", "переулок",
        "бульвар", "шоссе", "площадь", "район", "область", "край", "родилась", "родился",
        "родились", "рождения", "живу", "живёт", "живет", "жила", "жил", "сейчас", "теперь",
        "тогда", "была", "был", "были", "это", "что", "где", "когда", "мне", "меня", "зовут",
        "моё", "мое", "имя", "вечером", "утром", "днём", "днем", "ночью", "время", "года",
        "году", "месяц", "число", "семья", "работа", "жизнь", "здравствуйте", "привет",
        "люблю", "думаю", "знаю", "хочу", "буду", "есть", "очень", "часто", "иногда",
        "всегда", "никогда", "тоже", "также", "учусь", "учился", "училась", "родители",
        "мама", "папа", "сестра", "брат", "сын", "дочь", "дети", "муж", "жена",
        "the", "and", "was", "for", "you", "are", "she", "his", "her", "but", "not",
        "all", "can", "out", "day", "may", "got", "did", "its", "our", "who", "him",
        "how", "now", "see", "two", "way", "too", "any", "new", "old", "boy", "man",
        "born", "live", "lives", "lived", "living", "city", "town", "name", "from", "with",
        "morning", "evening", "night", "time", "year", "month", "family", "work", "life",
        "как", "какая", "какой", "какое", "какие", "кто", "чем", "куда", "откуда",
        "почему", "зачем", "можно", "нужно",
        "дела", "дело", "дел", "лет", "год", "про", "мою", "мой", "моя", "будет",
        "буду", "быть", "августа", "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "сентября", "октября", "ноября", "декабря", "неделя", "месяца", "расскажи",
        "деле", "дел", "самом", "самое", "правда", "просто", "конечно", "вообще",
        "наверное", "наверно", "кажется", "похоже", "честно", "потом", "завтра",
        "вчера", "сегодня", "потому", "поэтому", "сначала", "рядом", "дома",
        "там", "тут", "здесь", "тогда", "сразу", "опять", "вместе", "совсем",
        "вообщем", "вроде", "значит", "например", "спасибо", "пожалуйста",
        "карьеру", "карьера", "финансы", "любовь", "помощь", "спасибо", "пожалуйста",
        "about", "career", "love", "tell", "my", "me", "at", "in", "on", "of", "to",
        "i", "am", "is", "it", "that", "this", "we", "he", "they", "hello", "hi", "please", "thanks",
        "want", "know", "think", "feel", "going", "january", "february", "march", "april",
        "june", "july", "august", "september", "october", "november", "december",
        "лагна", "лаги", "зодиак", "гороскоп", "астролог", "астрологу", "накшатра",
        "аспект", "планета", "планеты", "планет", "тельца", "льва", "овна", "рака",
        "девы", "весов"
    };
    // Common nouns that are also real (usually small) towns. Only ignored when
    // the message clearly contains other content, so a bare "Бор"/"Мир" works.
    static const std::set<std::string> stop_place = {
        "дом", "мир", "бор", "лес", "луг", "поле", "гора", "река", "море", "остров",
        "сад", "лужа", "кран", "ток", "горе", "рок"
    };
    // Locative cues: a nearby preposition means "what follows is a place".
    static const std::set<std::string> cues = {
        "в", "во", "из", "родом", "город", "городе", "города", "с", "от", "под",
        "около", "in", "at", "from", "near"
    };

    // 1) multi-word aliases: exact match, longest n-gram first. An n-gram made
    //    only of function words ("не в" collides with an obscure alias) can never
    //    be a place name — skip it.
    for (int n = 3; n >= 2; --n) {
        for (size_t i = 0; i + static_cast<size_t>(n) <= words.size(); ++i) {
            std::string cand;
            bool has_content = false;
            bool has_short_word = false;   // "albert i" = Hungarian "Albert-irsa and"
            for (int k = 0; k < n; ++k) {
                if (k) cand += ' ';
                cand += words[i + k];
                if (utf8_len(words[i + k]) < 2) has_short_word = true;
                if (utf8_len(words[i + k]) >= 3 && !stop.count(words[i + k]) && !stop_place.count(words[i + k]))
                    has_content = true;
            }
            if (has_short_word || !has_content) continue;
            if (auto c = jyotish::geocode::resolve_exact(cand)) return c;
        }
    }

    // 2) single words: exact alias, then common declension forms ("в Москве").
    //    Prefer the largest city, so a personal name that happens to be a town
    //    does not shadow the real birth city.
    //
    //    Population thresholds are contextual: a short answer like "Геленджик"
    //    or a phrase with a locative cue ("в Геленджике") may be a small town,
    //    while a word in the middle of free text must be a real city, otherwise
    //    common words that collide with tiny hamlets ("как", "про", "was") win.
    size_t content_count = 0;
    for (const auto& w : words)
        if (utf8_len(w) >= 3 && !stop.count(w) && !stop_place.count(w)) ++content_count;

    std::optional<jyotish::geocode::CityInfo> best;
    size_t best_pos = SIZE_MAX;
    auto keep = [&](const jyotish::geocode::CityInfo& c, size_t pos) {
        // Correction messages pick the last-mentioned (the corrected) city;
        // regular messages prefer the biggest one so a personal name that is
        // a town does not shadow the real birth city.
        if (!best || (prefer_last ? pos > best_pos : c.population > best->population)) {
            best = c;
            best_pos = pos;
        }
    };
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        std::size_t clen = utf8_len(w);
        if (stop.count(w)) continue;
        bool has_cue = i > 0 && cues.count(words[i - 1]) > 0;
        bool single_lax = words.size() == 1 || (content_count <= 1 && cap[i]);
        bool word_lax = has_cue || single_lax;
        // In free text a 3-letter word is ambiguous with common words that are
        // exact aliases of cities ("так" -> Такаматсу); require 4+ unless the
        // context clearly marks a place (bare answer or after a preposition).
        if (clen < (word_lax ? 3u : 4u)) continue;
        if (!word_lax && stop_place.count(w)) continue;
        long long min_exact = word_lax ? 3000 : 150000;
        long long min_loose = word_lax ? 5000 : 150000;
        // A bare one-word answer that only matches an obscure alternate name is
        // far more likely a first name ("Мария" -> Мариана, "Иван" -> Eyvān):
        // for such answers require a large city and forbid fuzzy variants.
        bool risky_alias = single_lax && !has_cue;
        if (auto c = jyotish::geocode::resolve_exact(w)) {
            bool primary = jyotish::geocode::normalize_city(c->name) == w ||
                           jyotish::geocode::normalize_city(jyotish::geocode::transliterate(c->name)) == w;
            long long m = min_exact;
            if (risky_alias && !primary) m = std::max(m, 200000LL);
            if (c->population >= m) keep(*c, i);
        }
        if (clen >= 4 && !risky_alias) {
            if (auto c = jyotish::geocode::resolve_loose(w)) {
                if (c->population >= min_loose) keep(*c, i);
            }
        }
    }

    // 3) last resort: fuzzy match the joined content words, to catch compound
    //    names with internal declension ("в Нижнем Новгороде", "Ростове-на-Дону")
    bool lax = content_count <= 1 || words.size() == 1;
    long long min_fallback = lax ? 3000 : 150000;
    struct CW { size_t pos; const std::string* w; };
    std::vector<CW> cw;
    for (size_t i = 0; i < words.size(); ++i)
        if (utf8_len(words[i]) >= 3 && !stop.count(words[i]) && !(stop_place.count(words[i]) && !lax))
            cw.push_back({i, &words[i]});
    bool adjacent = false;
    for (size_t k = 1; k < cw.size(); ++k)
        if (cw[k].pos == cw[k - 1].pos + 1) { adjacent = true; break; }
    bool compound = (cw.size() >= 2 && cw.size() <= 3) ||
                    (cw.size() == 1 && cw[0].w->find('-') != std::string::npos);
    if (!prefer_last && compound) {
        std::string joined;
        for (const auto& c : cw) { if (!joined.empty()) joined += ' '; joined += *c.w; }
        auto res = jyotish::geocode::search(joined, 1);
        if (!res.empty()) {
            const auto& c = res.front().city;
            if (c.population >= min_fallback) {
                if (!best && res.front().score >= 450) return c;
                if (best && adjacent && res.front().score >= 300 && c.population > best->population)
                    return c;
            }
        }
    }
    if (best) return *best;
    return std::nullopt;
}

std::vector<std::string> detect_situations(const std::string& text, const std::string& lang) {
    std::string t = norm(text);
    std::vector<std::string> found;
    
    struct Sit { std::string key; std::vector<std::string> ru; std::vector<std::string> en; };
    static const std::vector<Sit> SITUATIONS = {
        {"career", {"работ", "карьер", "бизнес", "должност", "финанс", "деньг", "зарплат", "увольн", "начальн", "професс"},
                  {"work", "career", "job", "money", "finance", "salary", "boss", "profession"}},
        {"love", {"любов", "отношен", "партн", "жених", "невест", "свадьб", "брак", "встреч", "девушк", "мужчин", "женщин"},
                  {"love", "relationship", "partner", "marriage", "wedding", "date", "crush"}},
        {"health", {"здоров", "болезн", "операц", "лечен", "врач", "энерг", "устал", "сон"},
                   {"health", "sick", "illness", "surgery", "doctor", "energy", "tired"}},
        {"family", {"семь", "родствен", "родител", "дети", "ребенок", "ребёнок", "дом", "квартир", "переезд", "жил"},
                   {"family", "parents", "children", "child", "home", "apartment", "relocation", "move"}},
        {"travel", {"поездк", "переезд", "командир", "отпуск", "путешеств", "полёт", "полет", "дорог"},
                   {"travel", "trip", "flight", "vacation", "journey"}},
        {"study", {"учёб", "учеб", "экзамен", "универ", "институт", "курс", "обучен", "документ", "школ"},
                   {"study", "exam", "university", "course", "training", "document"}},
        {"deal", {"сделк", "контракт", "договор", "переговор", "подпис", "инвест", "покупк", "продаж", "кредит", "ипотек"},
                  {"deal", "contract", "negotiation", "sign", "investment", "purchase", "sale", "credit", "loan"}},
        {"start", {"начинан", "проект", "запуск", "стартап", "воплощ", "иде", "новое дело", "открыти"},
                   {"start", "begin", "project", "launch", "startup", "new"}}
    };
    
    for (const auto& sit : SITUATIONS) {
        const auto& kw = (lang == "ru") ? sit.ru : sit.en;
        for (const auto& w : kw) {
            if (t.find(w) != std::string::npos) {
                found.push_back(sit.key);
                break;
            }
        }
    }
    return found.empty() ? std::vector<std::string>{"general"} : found;
}

std::string format_situations(const std::vector<std::string>& situations, const std::string& lang) {
    std::string result;
    for (const auto& s : situations) {
        if (s == "career") result += (lang == "ru" ? "- Карьера и финансы\n" : "- Career and money\n");
        else if (s == "love") result += (lang == "ru" ? "- Отношения и партнёрство\n" : "- Love and relationships\n");
        else if (s == "health") result += (lang == "ru" ? "- Здоровье и энергия\n" : "- Health and energy\n");
        else if (s == "family") result += (lang == "ru" ? "- Семья и дом\n" : "- Family and home\n");
        else if (s == "travel") result += (lang == "ru" ? "- Поездки и переезды\n" : "- Travel and relocation\n");
        else if (s == "study") result += (lang == "ru" ? "- Обучение и документы\n" : "- Study and documents\n");
        else if (s == "deal") result += (lang == "ru" ? "- Сделки и переговоры\n" : "- Deals and negotiations\n");
        else if (s == "start") result += (lang == "ru" ? "- Начинания и проекты\n" : "- New beginnings\n");
        else result += (lang == "ru" ? "- Общий прогноз\n" : "- General outlook\n");
    }
    return result;
}

bool is_casual_question(const std::string& text, const std::string& lang, const BirthInfo* info) {
    if (casual_answer(text, lang)) return true;
    std::string t = norm(text);
    if (t.empty()) return false;
    if (info && (info->name.size() || info->birth_date.size() || info->birth_time.size() || info->city_found))
        return false;
    if (detect_situations(text, lang) != std::vector<std::string>{"general"}) return false;
    return t.size() <= 120 && (t.find('?') != std::string::npos || t.back() == '.' || t.back() == '!');
}

std::optional<std::string> casual_answer(const std::string& text, const std::string& lang, std::chrono::sys_days day) {
    std::string t = norm(text);
    auto has = [&](const std::vector<std::string>& patterns) {
        return std::any_of(patterns.begin(), patterns.end(), [&](const std::string& p) { return t.find(p) != std::string::npos; });
    };
    
    if (lang == "ru") {
        if (has({"какой сегодня день", "какой день недели", "какая сегодня дата", "какое сегодня число", "какое число сегодня", "какая дата", "какого сегодня числа"})) {
            auto ymd = std::chrono::year_month_day{day};
            static const char* wd_ru[] = {"понедельник", "вторник", "среда", "четверг", "пятница", "суббота", "воскресенье"};
            static const char* mon_ru[] = {"января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"};
            std::string wd = wd_ru[static_cast<int>(std::chrono::weekday{day}.c_encoding()) % 7];
            return "Сегодня " + wd + ", " + std::to_string(static_cast<unsigned>(ymd.day())) + " " + mon_ru[static_cast<unsigned>(ymd.month()) - 1] + " " + std::to_string(static_cast<int>(ymd.year())) + " года.\n\nЕсли хотите персональный гороскоп — напишите данные рождения.";
        }
        if (has({"спасибо", "благодарю"})) return "Пожалуйста! Я всегда рядом — если появятся вопросы о судьбе, карьере или отношениях, расскажите о дате и времени рождения.";
        if (has({"как дела", "как ты", "как настроение", "как жизнь", "как ваши дела"})) return "У меня всё отлично, спасибо! А расскажите о себе: как вас зовут, когда и где вы родились?";
        if (has({"кто ты", "ты кто", "что ты такое", "как тебя зовут", "представься", "расскажи о себе"})) return "Я — ведический оракул: отвечаю на вопросы о судьбе, отношениях, карьере и здоровье по джйотиш. Чтобы сделать точный расчёт, назовите имя, дату и время рождения и город.";
        if (has({"что такое джйотиш", "что такое джайотиш", "что такое джйотишь", "что такое джайотишь",
                 "расскажи про джйотиш", "расскажи про джайотиш", "расскажи про джайотишь", "расскажи про джйотишь",
                 "расскажи о джйотише", "расскажи о джайотише", "про джайотишь", "про джйотиш",
                 "что такое веды", "расскажи про веды", "что такое ведическая астрология",
                 "расскажи про астрологию", "что такое астрология"}))
            return "Джйотиш — это ведическая астрология: древняя индийская система, которая строит натальную карту по точному положению Солнца, Луны и планет в момент вашего рождения. В отличие от газетных «гороскопов», здесь всё считается по эфемеридам — из карты читаются характер, карьера, отношения, здоровье и даша-периоды (долгие ритмы судьбы).\n\nМогу составить лично вашу карту и прогноз на день. Для точного расчёта подскажите: имя, дату и время рождения и город.";
    } else {
        if (has({"what day is today", "what day is it", "what is today", "whats today", "what's today", "what is the date"})) {
            auto ymd = std::chrono::year_month_day{day};
            static const char* wd_en[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
            static const char* mon_en[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
            std::string wd = wd_en[static_cast<int>(std::chrono::weekday{day}.c_encoding()) % 7];
            return "Today is " + wd + ", " + mon_en[static_cast<unsigned>(ymd.month()) - 1] + " " + std::to_string(static_cast<unsigned>(ymd.day())) + ", " + std::to_string(static_cast<int>(ymd.year())) + ".\n\nIf you'd like a personal horoscope, send your birth details.";
        }
        if (has({"thank you", "thanks"})) return "You're welcome! I'm always here — if you have questions about destiny, career or love, share your birth date and time.";
        if (has({"what is jyotish", "what is jyotish astrology", "tell me about jyotish",
                 "what is vedic astrology", "tell me about vedic astrology", "about jyotish",
                 "what are the vedas", "tell me about astrology", "what is astrology"}))
            return "Jyotish is Vedic astrology — an ancient Indian system that builds a natal chart from the exact positions of the Sun, Moon and planets at your moment of birth. Unlike newspaper \"horoscopes\", everything is computed from ephemerides: the chart reveals character, career, relationships, health and dasha periods (the long rhythm of destiny).\n\nI can build your personal chart and a reading for today. For an accurate calculation just tell me your name, birth date, time and city.";
    }
    return std::nullopt;
}

std::string join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) return "";
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        result += delimiter + parts[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Chart-mismatch guard.
//
// Split the folded text into letter-only tokens. ASCII punctuation is a
// separator; Cyrillic (D0/D1 leads) is copied whole so tokens stay valid UTF-8.
static std::vector<std::string> tokenize_letters(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size();) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        if (b0 < 0x80 && !std::isalnum(b0)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            i++;
            continue;
        }
        if ((b0 == 0xD0 || b0 == 0xD1) && i + 1 < s.size()) {
            cur += s[i]; cur += s[i + 1]; i += 2; continue;
        }
        cur += static_cast<char>(b0);
        i++;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Exact match always; prefix match only for long, distinctive stems so that
// common words cannot be mistaken for a sign ("мага" != "магазин").
static bool tok_is(const std::string& tok, const std::string& stem) {
    if (tok == stem) return true;
    if (stem.size() >= 5 && tok.size() > stem.size() && tok.size() - stem.size() <= 4)
        return tok.compare(0, stem.size(), stem) == 0;
    return false;
}

static bool has_token(const std::vector<std::string>& toks, const std::string& stem) {
    for (const auto& t : toks) if (tok_is(t, stem)) return true;
    return false;
}

ChartMismatch detect_chart_mismatch(const Chart& chart, const std::string& text, const std::string& lang) {
    ChartMismatch out;
    const auto& t = jyotish::get(lang);
    const auto toks = tokenize_letters(norm(text));
    if (toks.empty()) return out;

    // ---- nakshatras: mentioned but absent from the chart -------------------
    std::vector<std::string> in_chart;
    for (int i = 0; i < 9; ++i) {
        const auto nk = nakshatra_name(chart.planets[i].nakshatra.nakshatra);
        if (std::find(in_chart.begin(), in_chart.end(), nk) == in_chart.end())
            in_chart.push_back(nk);
    }
    for (int n = 0; n < 27; ++n) {
        const std::string en = nakshatra_name(static_cast<Nakshatra>(n));
        auto it = t.nakshatra.find(en);
        const std::string ru = (it != t.nakshatra.end()) ? it->second : en;
        if (std::find(in_chart.begin(), in_chart.end(), en) != in_chart.end()) continue;
        if (has_token(toks, norm(ru)) || has_token(toks, norm(en)))
            out.absent_nakshatras.push_back(it != t.nakshatra.end() ? ru : en);
    }

    // ---- false "planet in sign" attributions --------------------------------
    static const char* PLANETS[9] = {"Sun","Moon","Mars","Mercury","Jupiter","Venus","Saturn","Rahu","Ketu"};
    // Short/common stems need their case forms listed explicitly.
    static const std::vector<std::string> SIGN_ALIASES = {
        "овен","овне","овна","телец","тельце","тельца","близнец","близнеца","рак","раке","рака",
        "лев","льве","леве","лева","дева","деве","деву","весы","весах","веса","скорпион","скорпионе",
        "скорпионо","стрелец","стрельце","стрельца","козерог","козероге","водолей","водолее","рыбы","рыбах","рыб"
    };
    auto sign_named = [&](size_t idx) -> bool {
        for (const auto& a : SIGN_ALIASES) if (tok_is(toks[idx], a)) return true;
        for (int s = 0; s < 12; ++s) {
            auto it = t.sign.find(sign_name(static_cast<Sign>(s)));
            const std::string nm = (it != t.sign.end()) ? norm(it->second) : norm(sign_name(static_cast<Sign>(s)));
            if (tok_is(toks[idx], nm)) return true;
        }
        return false;
    };

    for (int pi = 0; pi < 9; ++pi) {
        auto pit = t.planet.find(PLANETS[pi]);
        const std::string pstem = norm(pit != t.planet.end() ? pit->second : PLANETS[pi]);
        for (size_t i = 0; i < toks.size(); ++i) {
            if (!tok_is(toks[i], pstem)) continue;
            for (size_t j = i + 1; j < toks.size() && j <= i + 3; ++j) {
                if (!sign_named(j)) continue;
                std::string claimed;
                for (int s = 0; s < 12 && claimed.empty(); ++s) {
                    auto sit = t.sign.find(sign_name(static_cast<Sign>(s)));
                    const std::string disp = (sit != t.sign.end()) ? sit->second : sign_name(static_cast<Sign>(s));
                    if (tok_is(toks[j], norm(disp))) claimed = disp;
                }
                for (const auto& a : SIGN_ALIASES) {
                    if (!claimed.empty()) break;
                    if (tok_is(toks[j], a)) claimed = a;
                }
                if (claimed.empty()) continue;

                const auto actual = chart.planets[pi].sign;
                auto ait = t.sign.find(sign_name(actual));
                const std::string aname = ait != t.sign.end() ? ait->second : sign_name(actual);
                if (norm(claimed) == norm(aname)) break;  // correct as stated
                const std::string pname = pit != t.planet.end() ? pit->second : PLANETS[pi];
                out.wrong_planet_signs.push_back(pname + " — " + claimed);
                out.true_positions.push_back(pname + ": " + aname + ", " +
                                             (lang == "ru" ? "дом " : "house ") + std::to_string(static_cast<int>(chart.planets[pi].house)));
                break;
            }
        }
    }
    return out;
}

std::string mismatch_block(const ChartMismatch& m, const Chart& chart, const std::string& lang) {
    if (m.absent_nakshatras.empty() && m.wrong_planet_signs.empty()) return "";
    const auto& t = jyotish::get(lang);
    std::string have;
    for (int i = 0; i < 9; ++i) {
        const auto en = nakshatra_name(chart.planets[i].nakshatra.nakshatra);
        auto it = t.nakshatra.find(en);
        have += (have.empty() ? "" : ", ");
        have += (it != t.nakshatra.end() ? it->second : en);
    }
    std::string b;
    if (lang == "en") {
        b += "FACT CHECK — THE QUESTION CONTRADICTS THE CHART:\n";
        for (const auto& n : m.absent_nakshatras)
            b += "- Nakshatra \"" + n + "\" is NOT in this chart (chart nakshatras: " + have +
                 "). Explain what it means in general, then say PLAINLY that it is not present in the user's chart. "
                 "Do NOT link it to the user's planets, houses or periods, and do NOT invent its influence for them.\n";
        for (size_t i = 0; i < m.wrong_planet_signs.size(); ++i)
            b += "- The question states: " + m.wrong_planet_signs[i] + ". The chart says: " + m.true_positions[i] +
                 ". State the true position; do NOT agree with the wrong attribution.\n";
    } else {
        b += "ПРОВЕРКА ФАКТОВ — ВОПРОС РАСХОДИТСЯ С КАРТОЙ:\n";
        for (const auto& n : m.absent_nakshatras)
            b += "- Накшатра «" + n + "» в этой карте НЕТ (накшатры в карте: " + have +
                 "). Объясни, что она значит в общих чертах, и ПРЯМО скажи, что в карте пользователя её нет. "
                 "НЕ связывай её с планетами, домами и периодами пользователя и НЕ выдумывай её влияние.\n";
        for (size_t i = 0; i < m.wrong_planet_signs.size(); ++i)
            b += "- В вопросе указано: " + m.wrong_planet_signs[i] + ". По карте: " + m.true_positions[i] +
                 ". Скажи правду; НЕ соглашайся с ошибочной привязкой.\n";
    }
    return b;
}

} // namespace jyotish::oracle