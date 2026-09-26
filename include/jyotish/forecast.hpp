#pragma once

#include <jyotish/types.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace jyotish::forecast {

// One rising sign's forecast for the period. Every text field is a template
// slot keyed by a whole-sign house (1..12): focus / jupiter / favourable /
// how_to_live / training use the house the transiting JUPITER occupies for this
// lagna, `mars` uses the house of the transiting MARS, so the copy stays
// astronomically accurate even when the two planets are in different signs.
// When both planets share one sign (the case the period copy was written for)
// all slots come from the same house and the block reads as the single period
// paragraph seen in the source template.
struct SignForecast {
    Sign lagna = Sign::Aries;
    int house = 1;        // house of the transit (Jupiter) sign for this lagna, 1..12
    int mars_house = 1;   // house of the transiting Mars for this lagna, 1..12
    std::string focus;         // "Фокус на ..."
    std::string jupiter;       // "Юпитер ..."
    std::string mars;          // "Марс ..."
    std::string favourable;    // optional trailing sentence(s) of the period paragraph
    std::string how_to_live;   // text following "Как проживать:"
    std::string training;      // text following "К интенсиву:"
    std::string text;          // preformatted block, exactly like the template
};

struct PeriodForecast {
    std::string date;
    std::string time;
    double tz_offset = 0.0;
    std::string lang = "ru";
    Sign jupiter_sign = Sign::Aries;
    Sign mars_sign = Sign::Aries;
    int jupiter_deg = 0;
    int mars_deg = 0;
    std::string title;    // e.g. "Прогноз периода: Юпитер и Марс в Раке"
    std::string summary;  // "Итог периода ..."
    std::vector<SignForecast> signs;
};

// Computes the real sidereal Jupiter/Mars transits for a local date/time (UTC
// offset tz_offset, hours) and returns a full 12-sign period forecast keyed by
// whole-sign houses. `lang` is accepted for symmetry with the rest of the API;
// the period copy is Russian, matching the source template.
PeriodForecast compute_period_forecast(const std::string& date,
                                       const std::string& time = "12:00",
                                       double tz_offset = 0.0,
                                       const std::string& lang = "ru");

void to_json(nlohmann::json& j, const PeriodForecast& f);

} // namespace jyotish::forecast