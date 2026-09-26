#include <jyotish/transits.hpp>
#include <jyotish/i18n.hpp>
#include "swe_util.h"
#include <string>
#include <sstream>
#include <cmath>
#include <array>
#include <algorithm>

namespace jyotish {

namespace {

// Whole-sign house of a transiting planet relative to the natal ascendant.
int natal_house_of_sign(Sign transit_sign, Sign natal_asc_sign) {
    return (static_cast<int>(transit_sign) - static_cast<int>(natal_asc_sign) + 12) % 12 + 1;
}

// Default conjunction orbs (degrees) per transiting planet.
constexpr std::array<double, 9> TRANSIT_ORB = {
    8.0,  // Sun
    8.0,  // Moon
    6.0,  // Mars
    6.0,  // Mercury
    8.0,  // Jupiter
    7.0,  // Venus
    6.0,  // Saturn
    5.0,  // Rahu
    5.0,  // Ketu
};

double angular_distance(double a, double b) {
    double d = std::fabs(a - b);
    if (d > 180.0) d = 360.0 - d;
    return d;
}

} // namespace

TransitSnapshot compute_transit_snapshot(const Chart& natal, const std::string& date, 
                                          const std::string& time, double tz_offset) {
    TransitSnapshot snap;
    snap.date = date;
    snap.time = time;
    snap.tz_offset = tz_offset;

    swe_detail::swe_init();
    double jd = swe_detail::julian_day_ut(date, time, tz_offset);

    // Transit ascendant at the birth location (houses are location-dependent).
    double cusps[13], ascmc[10];
    swe_houses_ex(jd, swe_detail::SWE_FLAGS, natal.birth.latitude, natal.birth.longitude, 'W',
                  cusps, ascmc);
    snap.ascendant.longitude = std::fmod(ascmc[0], 360.0);
    snap.ascendant.sign = sign_of(snap.ascendant.longitude);
    snap.ascendant.degree = deg_in_sign(snap.ascendant.longitude);

    // Real positions for all nine planets.
    for (int i = 0; i < 9; ++i) {
        Planet p = static_cast<Planet>(i);
        double lon = 0.0, speed = 0.0;
        swe_detail::calc_planet(p, jd, lon, speed);
        snap.planets[i].longitude = lon;
        snap.planets[i].sign = sign_of(lon);
        snap.planets[i].degree = deg_in_sign(lon);
        snap.planets[i].natal_house =
            natal_house_of_sign(snap.planets[i].sign, natal.ascendant.sign);
    }

    // Conjunctions between transiting and natal planets within orb.
    for (int i = 0; i < 9; ++i) {
        Planet trans = static_cast<Planet>(i);
        double tl = snap.planets[i].longitude;
        for (int j = 0; j < 9; ++j) {
            if (i == j) continue;
            Planet nat = static_cast<Planet>(j);
            double nl = natal.planets[j].longitude;
            double dist = angular_distance(tl, nl);
            if (dist <= TRANSIT_ORB[static_cast<uint8_t>(trans)])
                snap.conjunctions.push_back({trans, nat, dist});
        }
    }
    std::sort(snap.conjunctions.begin(), snap.conjunctions.end(),
              [](const TransitConjunction& a, const TransitConjunction& b) { return a.orb < b.orb; });

    return snap;
}

TransitSnapshot localize_transits(const TransitSnapshot& snap, const std::string& lang) {
    (void)lang;
    // Localization of Sign/String names happens at formatting time in transit_block.
    return snap;
}

std::string transit_block(const TransitSnapshot& snap, const std::optional<DashaPeriod>& period,
                          double tz_offset, const std::string& lang) {
    const auto& t = get(lang);
    std::ostringstream out;

    auto sign_l = [&](Sign s) {
        auto it = t.sign.find(sign_name(s));
        return it != t.sign.end() ? it->second : sign_name(s);
    };
    auto planet_l = [&](Planet p) {
        auto it = t.planet.find(planet_name(p));
        return it != t.planet.end() ? it->second : planet_name(p);
    };

    if (lang == "ru") {
        out << "НА ДАННЫЙ МОМЕНТ — транзиты на " << snap.date << " " << snap.time
            << " (UTC" << (tz_offset >= 0 ? "+" : "") << std::lround(tz_offset) << "):\n";
        out << "Транзитная Лагна: " << sign_l(snap.ascendant.sign) << " "
            << std::lround(snap.ascendant.degree) << "°\n";
        out << "Планеты (знак → натальный дом):\n";
    } else {
        out << "FOR THIS MOMENT — transits on " << snap.date << " " << snap.time
            << " (UTC" << (tz_offset >= 0 ? "+" : "") << std::lround(tz_offset) << "):\n";
        out << "Transit Lagna: " << sign_l(snap.ascendant.sign) << " "
            << std::lround(snap.ascendant.degree) << "°\n";
        out << "Planets (sign → natal house):\n";
    }

    for (int i = 0; i < 9; ++i) {
        const auto& p = snap.planets[i];
        out << "  " << planet_l(static_cast<Planet>(i)) << ": " << sign_l(p.sign) << " "
            << std::lround(p.degree) << "° → нат. дом "
            << (p.natal_house ? std::to_string(*p.natal_house) : "?") << "\n";
    }

    if (!snap.conjunctions.empty()) {
        if (lang == "ru") out << "Соединения транзитных планет с натальными:\n";
        else out << "Transit conjunctions with natal planets:\n";
        for (size_t k = 0; k < snap.conjunctions.size() && k < 8; ++k) {
            const auto& c = snap.conjunctions[k];
            out << "  " << planet_l(c.transit) << " ⊕ " << planet_l(c.natal)
                << " (орбис " << std::lround(c.orb) << "°)\n";
        }
    }

    if (period) {
        if (lang == "ru") {
            out << "Текущий период вимшоттари: махадаша " << planet_l(period->mahadasha)
                << " (" << period->maha_start << "–" << period->maha_end << ")";
            if (period->antardasha)
                out << "\nантардаша " << planet_l(*period->antardasha)
                    << " (" << period->ad_start.value_or("") << "–" << period->ad_end.value_or("") << ")";
        } else {
            out << "Current Vimshottari period: mahadasha " << planet_l(period->mahadasha)
                << " (" << period->maha_start << "–" << period->maha_end << ")";
            if (period->antardasha)
                out << "\nantardasha " << planet_l(*period->antardasha)
                    << " (" << period->ad_start.value_or("") << "–" << period->ad_end.value_or("") << ")";
        }
        out << "\n";
    }

    return out.str();
}

} // namespace jyotish