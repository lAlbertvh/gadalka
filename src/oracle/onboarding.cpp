#include <jyotish/oracle.hpp>
#include <jyotish/geocode.hpp>
#include <jyotish/parsing.hpp>
#include <string>
#include <vector>
#include <optional>

namespace jyotish::oracle {

namespace {
int edit_distance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                               prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        prev = cur;
    }
    return prev[b.size()];
}

std::string fold_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b >= 'A' && b <= 'Z') { out += static_cast<char>(b - 'A' + 'a'); continue; }
        if (b == 0xD0 && i + 1 < s.size()) {   // А-Я -> а-я (0x90..0xAF variant, Ё is 0x81)
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if (b1 == 0x81) { out += '\xD1'; out += '\x91'; ++i; continue; }      // Ё -> ё
            if (b1 >= 0x90 && b1 <= 0x9F) { out += '\xD0'; out += static_cast<char>(b1 + 0x20); ++i; continue; }
            if (b1 >= 0xA0 && b1 <= 0xAF) { out += '\xD1'; out += static_cast<char>(b1 - 0x20); ++i; continue; }
        }
        out += s[i];
    }
    return out;
}

bool contains_token(const std::string& low, const char* token) {
    std::string_view sv(low);
    size_t start = 0;
    while (start < sv.size()) {
        size_t found = sv.find(token, start);
        if (found == std::string_view::npos) return false;
        bool left = found == 0 || !std::isalnum(static_cast<unsigned char>(sv[found - 1]));
        size_t end = found + std::string_view(token).size();
        bool right = end >= sv.size() || !std::isalnum(static_cast<unsigned char>(sv[end]));
        if (left && right) return true;
        start = found + 1;
    }
    return false;
}

bool contains_phrase(const std::string& low, const char* phrase) {
    std::string_view pp(phrase);
    size_t n = pp.size();
    size_t at = low.find(phrase);
    while (at != std::string::npos) {
        bool left = at == 0 || !std::isalnum(static_cast<unsigned char>(low[at - 1]));
        bool right = at + n >= low.size() || !std::isalnum(static_cast<unsigned char>(low[at + n]));
        if (left && right) return true;
        at = low.find(phrase, at + 1);
    }
    return false;
}

bool has_any_token(const std::string& low, std::initializer_list<const char*> tokens) {
    for (const char* t : tokens) if (contains_token(low, t)) return true;
    return false;
}

// A message that "fixes" previously extracted birth details. Only words that
// clearly mark a correction; an override additionally requires the message to
// actually contain the new value, so a bare "нет, спасибо" changes nothing.
bool is_correction(const std::string& low) {
    static const std::initializer_list<const char*> strong = {
        "неверн", "не так", "ошиб", "исправ", "поправ", "на самом деле", "не тот",
        "не этот", "не эта", "точнее", "вернее", "правиль", "уточн",
        "wrong", "mistake", "actually", "correct", "ошибоч", "без ошибок"
    };
    static const std::initializer_list<const char*> weak = {
        "родил", "город", "городе", "городом", "born", "birth", "city", "из", "родом"
    };
    if (has_any_token(low, strong)) return true;
    bool neg = has_any_token(low, {" не ", "нет", "никогда", "no", "not", "non"});
    return neg && has_any_token(low, weak);
}

bool looks_like_name(const std::string& city, const std::string& name) {
    auto nc = jyotish::geocode::normalize_city(city);
    auto nn = jyotish::geocode::normalize_city(name);
    auto tc = jyotish::geocode::normalize_city(jyotish::geocode::transliterate(city));
    auto tn = jyotish::geocode::normalize_city(jyotish::geocode::transliterate(name));
    auto prefix = [](const std::string& a, const std::string& b) {
        return a.size() >= 3 && b.rfind(a, 0) == 0;
    };
    if (prefix(nn, nc) || prefix(nc, nn) || prefix(tn, tc) || prefix(tc, tn)) return true;
    if (edit_distance(nc, nn) <= 1 || edit_distance(tc, tn) <= 1) return true;
    if (std::min(nc.size(), nn.size()) <= 4 && edit_distance(nc, nn) <= 2) return true;
    if (std::min(tc.size(), tn.size()) <= 4 && edit_distance(tc, tn) <= 2) return true;
    return false;
}
} // namespace

BirthInfo gather_birth(const std::vector<std::string>& history_texts) {
    BirthInfo info;
    auto set_city = [&](const std::string& text, bool prefer_last) {
        auto city = extract_city(text, prefer_last);
        if (!city) return false;
        bool is_name = !info.name.empty() && looks_like_name(city->name, info.name);
        if (is_name) return false;
        info.city = city->name;
        info.latitude = city->latitude;
        info.longitude = city->longitude;
        info.tz_offset = city->tz_offset;
        info.city_found = true;
        return true;
    };
    for (const auto& text : history_texts) {
        std::string low = fold_lower(text);

        auto set_name = [&]() {
            std::string n = extract_name(text);
            if (!n.empty()) info.name = n;
        };

        auto set_date = [&]() {
            auto d = parse_iso_date(text);
            auto now_days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
            if (!d || d->first < std::chrono::sys_days{std::chrono::year{1920}/1/1}) return false;
            if (d->first > now_days) return false;   // a birth date cannot lie in the future
            auto ymd = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(d->first)};
            int year = static_cast<int>(ymd.year());
            // "мне 36 лет" -> birth year = this year - age, when the text has a
            // month+day but no explicit year (parse_iso_date would default the
            // year to the current one). The age may live in this same message
            // or in an earlier one (already accumulated into info.age).
            int age = extract_age(text).value_or(info.age);
            if (age > 0 && !has_explicit_year(text)) {
                auto now_days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
                int this_year = static_cast<int>(std::chrono::year_month_day{now_days}.year());
                int inferred = this_year - age;
                if (inferred >= 1900 && inferred <= 2100) year = inferred;
            }
            info.birth_date = std::format("{:04}-{:02}-{:02}", year,
                                          static_cast<unsigned>(ymd.month()),
                                          static_cast<unsigned>(ymd.day()));
            if (info.birth_time.empty() && !d->second.empty()) info.birth_time = d->second;
            return true;
        };

        auto set_time = [&]() {
            if (auto tm = extract_time(text)) { info.birth_time = *tm; return true; }
            return false;
        };

        if (auto age = extract_age(text); age && info.age == 0) info.age = *age;

        // The user states ("родился", "я из", "родом") or explicitly corrects
        // ("нет, ... город") birth details — allow overriding whatever was
        // extracted so far instead of keeping the first (possibly wrong) reading.
        // An override is only trusted when the message is clearly about the
        // user themself (first person or an explicit correction).
        bool first_person = has_any_token(low, {"я", "мы", "меня", "мой", "моя", "моё", "мне",
                                                "i", "i'm", "my", "me", "we", "am from", "was born"});
        bool correction = is_correction(low);
        bool states_birth = has_any_token(low, {"родился", "родилась", "родились", "рожден",
                                                "рождена", "родом", "родин"}) ||
                            contains_phrase(low, "я из") || contains_phrase(low, "мы из") ||
                            contains_phrase(low, "i am from") || contains_phrase(low, "i'm from");
        // "мой сын родился в Сочи" is about another person — never override the
        // client's own data with it.
        bool mentions_relative = has_any_token(low, {
            "сын", "дочь", "брат", "сестра", "муж", "жена", "дед", "бабушк", "мам", "пап",
            "ребен", "ребён", "родител", "daughter", "son", "brother", "sister", "wife",
            "husband", "mom", "dad", "parents", "child"});
        bool restates = states_birth && (correction || (first_person && !mentions_relative));
        if (correction || restates) {
            // A fresh full retelling — or an explicit correction — replaces any
            // earlier (possibly stale/example) readings.
            set_name();
            set_date();
            set_time();
        } else {
            if (info.name.empty()) set_name();
            if (info.birth_date.empty()) set_date();
            if (info.birth_time.empty()) set_time();
        }
        if (correction || restates || !info.city_found) {
            set_city(text, correction || restates);
        }
    }
    // Gender: prefer an explicit first-person statement ("я мальчик", "я
    // родилась") seen EARLIEST in the dialogue; never let a later question that
    // merely mentions "мальчик" ("я мальчик или девочка?") override it.
    // Fall back to the confirmed name's morphology.
    for (const auto& text : history_texts) {
        std::string g = guess_gender(text);
        if (!g.empty()) { info.gender = g; break; }
    }
    if (info.gender.empty() && !info.name.empty())
        info.gender = guess_gender(info.name);
    return info;
}

bool birth_is_ready(const BirthInfo& info) {
    return !info.birth_date.empty() && !info.birth_time.empty() && info.city_found;
}

BirthStatus birth_status(const BirthInfo& info) {
    return { !info.name.empty(), !info.birth_date.empty(), !info.birth_time.empty(), info.city_found };
}

std::string onboarding_reply(const BirthInfo& info, const std::string& lang) {
    const bool ru = lang == "ru";
    const bool have_any = !info.name.empty() || !info.birth_date.empty() ||
                          !info.birth_time.empty() || info.city_found;

    // If the user stated an age but no exact date, tell them the year the
    // engine will infer and ask only for day+month — not for the full date.
    if (info.birth_date.empty() && info.age > 0) {
        auto now_days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        int this_year = static_cast<int>(std::chrono::year_month_day{now_days}.year());
        int inferred = this_year - info.age;
        std::string ack;
        if (!info.name.empty()) ack = ru ? ("Хорошо, " + info.name + "! ") : ("Great, " + info.name + "! ");
        if (info.city_found) ack += ru ? ("Запомнил город: " + info.city + ". ") : ("Noted the city: " + info.city + ". ");
        if (ru)
            return ack + "Вижу, вам около " + std::to_string(info.age) + " — значит год рождения примерно "
                + std::to_string(inferred) + ". Подскажите только день и месяц, например: «24 июля» или «15.08».";
        else
            return ack + "You're about " + std::to_string(info.age) + ", so the birth year is around "
                + std::to_string(inferred) + ". Just tell me the day and month, e.g. \"24 July\" or \"15.08\".";
    }

    if (info.birth_date.empty()) {
        if (!have_any) {
            if (ru) return
                "Здравствуйте! Я — ведический оракул. Можете называть меня просто «оракул».\n"
                "Чтобы я дал вам точный гороскоп, мне нужно немного данных о рождении.\n\n"
                "Представьтесь (можно только имя, это необязательно) и подскажите дату и время рождения и город. Можно одной строкой:\n"
                "«Меня зовут Анна, родилась 15.08.1995 в 14:30 в Москве».";
            else return
                "Hello! I am a Vedic oracle. Call me simply \"oracle\". To give you an accurate horoscope I need a few details about your birth.\n\n"
                "Tell me your name (optional) and your birth date, time and city — one line is fine, e.g.:\n"
                "\"My name is Anna, I was born on 15.08.1995 at 14:30 in Moscow\".";
        }
        std::string ack;
        if (!info.name.empty()) ack = ru ? ("Хорошо, " + info.name + "! ") : ("Great, " + info.name + "! ");
        if (info.city_found) ack += ru ? ("Запомнил город: " + info.city + ". ") : ("Noted the city: " + info.city + ". ");
        if (ru) return ack + "Теперь подскажите дату рождения (и, если знаете, время), например: «15.08.1995 в 14:30».";
        else return ack + "Now tell me your birth date (and time if you know it), e.g. \"15.08.1995 at 14:30\".";
    }
    if (info.birth_time.empty()) {
        if (lang == "ru") return info.name.empty() ? "Отлично! А во сколько вы родились? Например: «в 14:30»." : info.name + ", а во сколько вы родились? Например: «в 14:30».";
        else return "Well done! At what time were you born? e.g. \"at 14:30\".";
    }
    if (!info.city_found) {
        if (lang == "ru") return "И в каком городе вы родились? Я распознаю по названию (например: Москва, Санкт-Петербург, Киев).";
        else return "And in which city were you born? I recognise city names (e.g. Moscow, Kyiv, Minsk).";
    }
    return "";
}

std::optional<int> future_year_in_text(const std::string& text) {
    auto now_days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    int cur_year = static_cast<int>(std::chrono::year_month_day{now_days}.year());
    for (size_t i = 0; i + 4 <= text.size(); ++i) {
        bool token_start = i == 0 || !std::isdigit(static_cast<unsigned char>(text[i - 1]));
        bool all_digit = true;
        for (size_t k = 0; k < 4; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(text[i + k]))) { all_digit = false; break; }
        }
        if (!token_start || !all_digit) continue;
        bool token_end = i + 4 == text.size() || !std::isdigit(static_cast<unsigned char>(text[i + 4]));
        if (!token_end) continue;   // part of a longer number (phone, sum, …)
        int y = std::stoi(text.substr(i, 4));
        if (y > cur_year) return y;
    }
    return std::nullopt;
}

std::string future_date_reply(const BirthInfo& info, const std::string& lang, int year) {
    const bool ru = lang == "ru";
    std::string head;
    if (!info.name.empty()) head = ru ? ("Хорошо, " + info.name + "! ") : ("Great, " + info.name + "! ");
    if (ru) {
        return head + "Но вы, кажется, посланник из будущего: вы указали год " + std::to_string(year) +
               ", а это позже нашего с вами дня встречи. Мои знания о вашем времени неактуальны — гороскоп "
               "строится по дате рождения, а она у вас ещё не наступила.\n\n"
               "Введите, пожалуйста, дату рождения до сегодняшнего дня (до дня нашей с вами встречи), "
               "например: «15.08.1995 в 14:30».";
    }
    return head + "But you seem to be a messenger from the future: you gave the year " + std::to_string(year) +
           ", which is after the day we met. My knowledge about your time is not applicable — the horoscope "
           "is built from the birth date, and yours hasn't happened yet.\n\n"
           "Please enter a birth date up to today (up to the day we met), e.g. \"15.08.1995 at 14:30\".";
}

Chart::BirthData build_birth_data(const BirthInfo& info) {
    Chart::BirthData bd;
    bd.name = info.name;
    bd.birth_date = info.birth_date;
    bd.birth_time = info.birth_time;
    bd.city = info.city;
    bd.latitude = info.latitude;
    bd.longitude = info.longitude;
    bd.tz_offset = info.tz_offset;
    return bd;
}

namespace {

const char* RU_MONTHS_GEN[12] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                 "июля", "августа", "сентября", "октября", "ноября", "декабря"};

// "1990-07-24" -> "24 июля 1990" / "07/24/1990"
std::string pretty_birth_date(const std::string& iso, const std::string& lang) {
    std::string y, m, d;
    std::string_view sv(iso);
    auto p1 = sv.find('-');
    if (p1 != std::string_view::npos) {
        y = std::string(sv.substr(0, p1));
        size_t p2 = sv.find('-', p1 + 1);
        m = std::string(sv.substr(p1 + 1, (p2 == std::string_view::npos ? sv.size() : p2) - p1 - 1));
        d = std::string(sv.substr((p2 == std::string_view::npos ? sv.size() : p2) + 1));
    }
    if (lang == "ru") {
        int mon = m.empty() ? 0 : std::stoi(m);
        std::string mon_name = (mon >= 1 && mon <= 12) ? RU_MONTHS_GEN[mon - 1] : m;
        return d + " " + mon_name + " " + y;
    }
    return m + "/" + d + "/" + y;
}

std::string confirm_marker(const BirthInfo& info) {
    return "CONFIRM|" + info.birth_date + "|" + info.birth_time + "|" + info.city + "|" + info.name;
}

bool is_affirmative(const std::string& low) {
    static const std::initializer_list<const char*> tokens = {
        "подтвержд", "верно", "правильно", "правильно", "именно", "точно", "соглас",
        "yes", "yep", "yup", "ok", "okay", "correct", "right", "ага", "угу", "да"
    };
    if (!has_any_token(low, tokens)) return false;
    if (contains_phrase(low, "да нет")) return false;
    return true;
}

std::string confirmation_reply(const BirthInfo& info, const std::string& lang, const std::string& marker) {
    std::string s;
    if (lang == "ru") {
        s = "Я понял так: вы родились " + pretty_birth_date(info.birth_date, lang);
        if (!info.birth_time.empty()) s += " в " + info.birth_time;
        if (info.city_found) s += " в городе " + info.city;
        if (!info.name.empty()) s += ".\nВас зовут " + info.name;
        s += ".\nВерно? Если да — ответьте «да», «верно» или «подтверждаю», и я рассчитаю ваш гороскоп.";
    } else {
        s = "So I've understood: you were born on " + pretty_birth_date(info.birth_date, lang);
        if (!info.birth_time.empty()) s += " at " + info.birth_time;
        if (info.city_found) s += " in " + info.city;
        if (!info.name.empty()) s += ". Your name is " + info.name;
        s += ".\nIs that right? Reply \"yes\", \"correct\" or \"that's right\" and I will compute your horoscope.";
    }
    s += "\n[" + marker + "]";
    return s;
}

int last_confirm_index(const std::vector<std::string>& history, const std::string& marker) {
    int idx = -1;
    for (size_t i = 0; i < history.size(); ++i) {
        auto colon = history[i].find(':');
        std::string role = colon == std::string::npos ? "user" : history[i].substr(0, colon);
        std::string content = colon == std::string::npos ? history[i] : history[i].substr(colon + 2);
        if (role == "assistant" && content.find(marker) != std::string::npos) idx = static_cast<int>(i);
    }
    return idx;
}

bool assistant_after(const std::vector<std::string>& history, size_t after) {
    for (size_t i = after + 1; i < history.size(); ++i) {
        auto colon = history[i].find(':');
        std::string role = colon == std::string::npos ? "user" : history[i].substr(0, colon);
        if (role == "assistant") return true;
    }
    return false;
}

} // namespace

bool signals_unknown_time(const std::string& question) {
    std::string low = fold_lower(question);
    const bool admitted_unknown = has_any_token(low, {
        "не зна", "не помн", "не вспомн", "не увер", "не точн", "неизвест",
        "примерно", "приблиз", "неопредел", "неточно", "навскидку"});
    if (!admitted_unknown) return false;
    const bool about_birth = has_any_token(low, {"время", "времени", "часов", "час",
                                                 "time", "родил", "рожд", "birth",
                                                 "точно", "год"});
    // The confirmation gate has complete birth data in hand, so a bare
    // "примерно" / "не знаю" / "не уверен" in this step refers to those very
    // data (usually the time). Only a longer message spelling out something
    // unrelated keeps the regular flow.
    return about_birth || low.size() <= 60;
}

std::optional<ConfirmEcho> last_confirm_echo(const std::vector<std::string>& history_messages) {
    for (size_t i = history_messages.size(); i-- > 0;) {
        auto colon = history_messages[i].find(':');
        std::string role = colon == std::string::npos ? "user" : history_messages[i].substr(0, colon);
        if (role != "assistant") continue;
        std::string content = colon == std::string::npos ? history_messages[i]
                                                         : history_messages[i].substr(colon + 2);
        auto pos = content.find("[CONFIRM|");
        if (pos == std::string::npos) continue;
        size_t a = pos + 9;
        size_t b = content.find(']', a);
        if (b == std::string::npos) continue;
        std::string f = content.substr(a, b - a);
        ConfirmEcho echo;
        std::vector<std::string> parts;
        size_t s = 0;
        while (s <= f.size()) {
            size_t pipe = f.find('|', s);
            parts.push_back(f.substr(s, pipe == std::string::npos ? std::string::npos : pipe - s));
            if (pipe == std::string::npos) break;
            s = pipe + 1;
        }
        if (parts.size() >= 4) {
            echo.birth_date = parts[0];
            echo.birth_time = parts[1];
            echo.city = parts[2];
            echo.name = parts[3];
        }
        return echo;
    }
    return std::nullopt;
}

ConfirmResult confirm_birth(const std::string& question,
                            const std::vector<std::string>& history_messages,
                            const BirthInfo& info,
                            const std::string& lang) {
    const std::string marker = confirm_marker(info);
    const int im = last_confirm_index(history_messages, marker);
    if (im >= 0 && assistant_after(history_messages, static_cast<size_t>(im))) {
        // This exact data set was already confirmed and followed up — analyse.
        return {ConfirmState::Confirmed, "", false};
    }
    if (im >= 0) {
        // The user says the exact birth time is unknown ("да только время
        // рождения я не знаю") — that is NOT consent to the chart as silently
        // echoed. A leading "да" must not swallow the caveat.
        if (signals_unknown_time(question)) {
            std::string reply = confirmation_reply(info, lang, marker);
            reply += lang == "ru"
                ? "\n\nПринято: точное время рождения вы не знаете. Для расчёта я взял время по умолчанию — полдень (12:00). Это приближение: лагна и дома могут быть неточными, но Луна, накшатра и даша-периоды от него не зависят. Если подходит — ответьте просто «да»."
                : "\n\nUnderstood: you don't know the exact birth time. For the calculation I use the default of noon (12:00). This is an approximation: the lagna and the houses may be slightly off, while the Moon, nakshatra and dasha periods do not depend on it. If that works — just reply \"yes\".";
            return {ConfirmState::NeedConfirm, reply, false};
        }
        // We already asked to confirm this exact data; affirm now or keep waiting.
        if (is_affirmative(fold_lower(question))) return {ConfirmState::Confirmed, "", true};
        return {ConfirmState::NeedConfirm, confirmation_reply(info, lang, marker), false};
    }
    // Complete birth data available for the first time — echo it back and wait.
    return {ConfirmState::NeedConfirm, confirmation_reply(info, lang, marker), false};
}

} // namespace jyotish::oracle