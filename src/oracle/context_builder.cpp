#include <jyotish/oracle.hpp>
#include <jyotish/chart.hpp>
#include <jyotish/dasha.hpp>
#include <jyotish/transits.hpp>
#include <jyotish/chart_formatter.hpp>
#include <jyotish/i18n.hpp>
#include <jyotish/parsing.hpp>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

namespace jyotish::oracle {

OracleContext build_context(const Chart::BirthData& birth, const Chart& charter, const Chart& canonical,
                           const std::string& question, const std::string& photo_context,
                           const std::string& lang) {
    std::string summary = chart_summary(charter, lang);
    std::vector<std::string> parts = { "НАТАЛЬНАЯ КАРТА:\n" + summary };

    // Anchor the confirmed birth facts so the model never invents/changes the date.
    {
        std::string facts = lang == "ru"
            ? "ПОДТВЕРЖДЁННЫЕ ДАННЫЕ РОЖДЕНИЯ (истина, не меняй): "
            : "CONFIRMED BIRTH DATA (the truth, do not change): ";
        if (!birth.name.empty())
            facts += (lang == "ru" ? "имя " : "name ") + birth.name + ", ";
        facts += (lang == "ru" ? "дата " : "date ") + birth.birth_date;
        if (!birth.birth_time.empty()) facts += " " + birth.birth_time;
        facts += (lang == "ru" ? ", город " : ", city ") + (birth.city.empty() ? "не указан" : birth.city);
        parts.insert(parts.begin(), facts);
    }

    // Explicit computed Lagna row — a single source of truth the model must not
    // second-guess ("если родились днём/ночью" hedging).
    {
        std::string lagna_line = lang == "ru"
            ? "РАССЧИТАННАЯ ЛАГНА (точная, определена эфемеридами по данным выше; НЕ предлагай другие варианты, не говори «если родились …», не меняй её): "
            : "COMPUTED LAGNA (exact, derived from the data above; do NOT propose alternatives, do not say \"if you were born …\", do not change it): ";
        const auto& t = jyotish::get(lang);
        auto sit = t.sign.find(jyotish::sign_name(charter.ascendant.sign));
        std::string lagna_display = sit != t.sign.end() ? sit->second : jyotish::sign_name(charter.ascendant.sign);
        lagna_line += lagna_display
            + " " + std::to_string(static_cast<int>(charter.ascendant.degree)) + "°";
        parts.insert(parts.begin() + 1, lagna_line);
    }
    
    // Parse date from question
    auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    std::optional<std::chrono::sys_days> found_date;
    std::optional<std::string> found_time;
    std::optional<DashaPeriod> period;
    std::optional<TransitSnapshot> transits_snapshot;
    
    if (auto rel = parse_relative_date(question, today); rel) {
        found_date = rel->first;
        found_time = rel->second;
    } else if (auto iso = parse_iso_date(question); iso) {
        found_date = iso->first;
        found_time = iso->second;
    }
    
    // Check if it's birth date discussion
    if (found_date && std::format("{:%Y-%m-%d}", *found_date) == birth.birth_date &&
        (question.find("родил") != std::string::npos || question.find("рожд") != std::string::npos ||
         question.find("born") != std::string::npos || question.find("birth") != std::string::npos) &&
        question.find("прогноз") == std::string::npos && question.find("будет") == std::string::npos &&
        question.find("жд") == std::string::npos && question.find("forecast") == std::string::npos) {
        found_date.reset();
        found_time.reset();
    }
    
    // Build dasha timeline
    auto timeline = build_dasha_timeline(birth.birth_date, birth.birth_time, birth.tz_offset, canonical);
    
    // Convert today to string manually
    auto today_c = std::chrono::system_clock::to_time_t(today);
    std::tm today_tm = *std::gmtime(&today_c);
    char today_buf[11];
    std::strftime(today_buf, sizeof(today_buf), "%Y-%m-%d", &today_tm);
    std::string today_str = today_buf;
    
    // Current period + today's transits
    if (auto today_period = dasha_at_date(timeline, today_str)) {
        std::string lang_code = lang == "ru" ? "ru" : "en";
        auto planet_names = jyotish::get(lang_code).planet;
        std::string ad = "";
        if (today_period->antardasha) {
            auto it = planet_names.find(planet_name(*today_period->antardasha));
            ad = (it != planet_names.end()) ? it->second : planet_name(*today_period->antardasha);
        }
        
        if (lang == "ru") {
            parts.push_back("ТЕКУЩИЙ ПЕРИОД (сегодня, контекст): махадаша " + 
                planet_names.at(planet_name(today_period->mahadasha)) + " (" + today_period->maha_start + "–" + today_period->maha_end + "), " +
                "антардаша " + ad + " (" + today_period->ad_start.value_or("") + "–" + today_period->ad_end.value_or("") + ")");
        } else {
            parts.push_back("CURRENT PERIOD (today, context): mahadasha " + 
                planet_names.at(planet_name(today_period->mahadasha)) + " (" + today_period->maha_start + "–" + today_period->maha_end + "), " +
                "antardasha " + ad + " (" + today_period->ad_start.value_or("") + "–" + today_period->ad_end.value_or("") + ")");
        }
    }
    
    try {
        auto today_snap = compute_transit_snapshot(canonical, today_str, "12:00", birth.tz_offset);
        auto today_loc = localize_transits(today_snap, lang);
        std::string tb = transit_block(today_loc, std::nullopt, birth.tz_offset, lang);
        if (lang == "ru") parts.push_back("ТРАНЗИТЫ НА СЕГОДНЯ (" + today_str + "):\n" + tb);
        else parts.push_back("TRANSITS FOR TODAY (" + today_str + "):\n" + tb);
    } catch (...) {}
    
    // Target date transits
    if (found_date) {
        // Convert found_date to string
        auto ymd = std::chrono::year_month_day{*found_date};
        std::tm fd_tm = {};
        fd_tm.tm_year = static_cast<int>(ymd.year()) - 1900;
        fd_tm.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
        fd_tm.tm_mday = static_cast<unsigned>(ymd.day());
        fd_tm.tm_isdst = 0;
        std::time_t fd_tt = std::mktime(&fd_tm);
        char fd_buf[11];
        std::strftime(fd_buf, sizeof(fd_buf), "%Y-%m-%d", std::gmtime(&fd_tt));
        std::string target_date = fd_buf;
        
        std::string target_time = found_time.value_or("12:00");
        period = dasha_at_date(timeline, target_date);
        auto snap = compute_transit_snapshot(canonical, target_date, target_time, birth.tz_offset);
        auto localized = localize_transits(snap, lang);
        std::string tb = transit_block(localized, period, birth.tz_offset, lang);
        if (lang == "ru") {
            parts.push_back("ГОРОСКОП НА ДАТУ (транзиты на " + target_date + " " + target_time + "):\n" + tb);
        } else {
            parts.push_back("HOROSCOPE FOR DATE (transits on " + target_date + " " + target_time + "):\n" + tb);
        }
        transits_snapshot = localized;
    }
    
    if (!photo_context.empty()) {
        parts.push_back((lang == "ru" ? "ФОТО-АНАЛИЗ (учти кратко):\n" : "PHOTO-ANALYSIS (briefly):\n") + photo_context);
    }
    
    auto situations = detect_situations(question, lang);
    if (!situations.empty()) {
        parts.push_back((lang == "ru" ? "КАКИЕ СФЕРЫ ВОЛНУЮТ:\n" : "AREAS OF CONCERN:\n") + format_situations(situations, lang));
    }

    // The question may name a nakshatra or a sign that is not in this chart.
    // Left unchecked the model happily invents a link, so state the truth.
    if (auto mm = detect_chart_mismatch(charter, question, lang); !mm.absent_nakshatras.empty() || !mm.wrong_planet_signs.empty()) {
        if (std::string mb = mismatch_block(mm, charter, lang); !mb.empty())
            parts.push_back(mb);
    }
    
    std::optional<std::string> found_date_str;
    if (found_date) {
        auto ymd = std::chrono::year_month_day{*found_date};
        std::tm fd_tm = {};
        fd_tm.tm_year = static_cast<int>(ymd.year()) - 1900;
        fd_tm.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
        fd_tm.tm_mday = static_cast<unsigned>(ymd.day());
        fd_tm.tm_isdst = 0;
        std::time_t fd_tt = std::mktime(&fd_tm);
        char fd_buf[11];
        std::strftime(fd_buf, sizeof(fd_buf), "%Y-%m-%d", std::gmtime(&fd_tt));
        found_date_str = fd_buf;
    }
    
    return {
        join(parts, "\n\n"),
        found_date_str,
        found_time,
        period,
        transits_snapshot,
        situations,
        nlohmann::json::object()
    };
}

} // namespace jyotish::oracle