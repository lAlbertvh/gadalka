// Shared Swiss Ephemeris helpers (internal to the core library).
#pragma once

#include <jyotish/types.hpp>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <string>

#if defined(JYOTISH_MOCK_SWISSEPH)
    #include "mock_swisseph.h"
#else
    #include <swephexp.h>
#endif

namespace jyotish { namespace swe_detail {

inline constexpr int32_t SWE_FLAGS = SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_SIDEREAL;

inline int planet_swe_id(Planet p) {
    switch (p) {
        case Planet::Sun: return SE_SUN;
        case Planet::Moon: return SE_MOON;
        case Planet::Mars: return SE_MARS;
        case Planet::Mercury: return SE_MERCURY;
        case Planet::Jupiter: return SE_JUPITER;
        case Planet::Venus: return SE_VENUS;
        case Planet::Saturn: return SE_SATURN;
        case Planet::Rahu: return SE_TRUE_NODE;
        case Planet::Ketu: return -1;   // computed from Rahu + 180
        default: return SE_SUN;
    }
}

inline void swe_init() {
    swe_set_ephe_path(nullptr);
    swe_set_sid_mode(SE_SIDM_LAHIRI, 0, 0);
}

// Local civil date/time (YYYY-MM-DD, HH:MM) + UTC offset (hours) -> UT Julian Day.
// The timezone correction may cross midnight: the day is adjusted properly.
inline double julian_day_ut(const std::string& date, const std::string& time, double tz_offset) {
    int year = 0, month = 0, day = 0;
    std::sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);
    int hour = 0, min = 0;
    std::sscanf(time.c_str(), "%d:%d", &hour, &min);
    double local_hours = hour + min / 60.0;
    double ut_hours = local_hours - tz_offset;

    auto sd = std::chrono::sys_days{
        std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)}
            / std::chrono::day{static_cast<unsigned>(day)}};
    long shift = static_cast<long>(std::floor(ut_hours / 24.0));
    double rem = ut_hours - static_cast<double>(shift) * 24.0;
    auto ymd = std::chrono::year_month_day{sd + std::chrono::days{shift}};

    int yy = static_cast<int>(ymd.year());
    int mm = static_cast<int>(static_cast<unsigned>(ymd.month()));
    int dd = static_cast<int>(static_cast<unsigned>(ymd.day()));
    return swe_julday(yy, mm, dd, rem, SE_GREG_CAL);
}

// Sidereal longitude/speed for one planet at a UT Julian Day. Ketu = Rahu + 180.
inline void calc_planet(Planet p, double jd, double& lon, double& speed) {
    if (p == Planet::Ketu) {
        double rlon = 0.0, rspeed = 0.0;
        calc_planet(Planet::Rahu, jd, rlon, rspeed);
        lon = std::fmod(rlon + 180.0, 360.0);
        speed = rspeed;
        return;
    }
    double xx[6];
    int ret = swe_calc_ut(jd, planet_swe_id(p), SWE_FLAGS, xx, nullptr);
    if (ret < 0) {
        lon = 0.0;
        speed = 0.0;
    } else {
        lon = std::fmod(xx[0], 360.0);
        speed = xx[3];
    }
}

}} // namespace jyotish::swe_detail