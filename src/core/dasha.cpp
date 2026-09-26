#include <jyotish/types.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace jyotish {

std::string format_date(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

std::chrono::system_clock::time_point add_years(std::chrono::system_clock::time_point tp, double years) {
    auto days = static_cast<long long>(years * 365.2425);
    return tp + std::chrono::hours(days * 24);
}

DashaTimeline build_dasha_timeline(const Chart::BirthData& birth, const Chart& chart) {
    DashaTimeline timeline;
    
    // Parse birth datetime
    int year, month, day, hour, min;
    sscanf(birth.birth_date.c_str(), "%d-%d-%d", &year, &month, &day);
    sscanf(birth.birth_time.c_str(), "%d:%d", &hour, &min);
    
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = 0;
    tm.tm_isdst = 0;
    
    // Convert to UTC
    time_t local_time = std::mktime(&tm);
    time_t utc_time = local_time - static_cast<time_t>(birth.tz_offset * 3600);
    
    std::chrono::system_clock::time_point start = std::chrono::system_clock::from_time_t(utc_time);
    
    Planet first_lord = chart.vimshottari_balance.lord;
    double balance_years = chart.vimshottari_balance.years_remaining;
    
    int start_idx = -1;
    for (int i = 0; i < 9; ++i) if (DASHA_ORDER[i] == first_lord) { start_idx = i; break; }
    
    std::chrono::system_clock::time_point cursor = start;
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int step = 0; step < 9; ++step) {
            Planet md_lord = DASHA_ORDER[(start_idx + step) % 9];
            double md_years = (step == 0 && cycle == 0) ? balance_years : DASHA_YEARS[step];
            
            Mahadasha md;
            md.lord = md_lord;
            md.years = md_years;
            md.start = format_date(cursor);
            cursor = add_years(cursor, md_years);
            md.end = format_date(cursor);
            
            // Antardashas
            for (int adel = 0; adel < 9; ++adel) {
                Planet ad_lord = DASHA_ORDER[(start_idx + step + adel) % 9];
                double ad_years = md_years * DASHA_YEARS[adel] / 120.0;
                md.antardashas.push_back({ad_lord, md.start, md.end, ad_years});  // simplified dates
            }
            
            timeline.push_back(std::move(md));
        }
    }
    
    return timeline;
}

std::optional<DashaPeriod> dasha_at_date(const DashaTimeline& timeline, const std::string& target_date) {
    for (const auto& md : timeline) {
        if (md.start <= target_date && target_date <= md.end) {
            for (const auto& ad : md.antardashas) {
                if (ad.start <= target_date && target_date <= ad.end) {
                    return DashaPeriod{
                        md.lord, md.start, md.end, md.years,
                        ad.lord, ad.start, ad.end
                    };
                }
            }
            return DashaPeriod{md.lord, md.start, md.end, md.years, {}, {}, {}};
        }
    }
    return std::nullopt;
}

DashaTimeline build_dasha_timeline(const std::string& birth_date, const std::string& birth_time,
                                   double tz_offset, const Chart& chart) {
    Chart::BirthData birth;
    birth.birth_date = birth_date;
    birth.birth_time = birth_time;
    birth.tz_offset = tz_offset;
    return build_dasha_timeline(birth, chart);
}

} // namespace jyotish