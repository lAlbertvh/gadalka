#include <jyotish/types.hpp>
#include "swe_util.h"
#include <cmath>
#include <array>

namespace jyotish {

namespace {

constexpr std::array<double, 9> COMBUSTION_ORB = {
    0.0,    // Sun
    0.0,    // Moon
    17.0,   // Mars
    14.0,   // Mercury
    11.0,   // Jupiter
    10.0,   // Venus
    15.0,   // Saturn
    0.0,    // Rahu
    0.0     // Ketu
};
} // namespace

// ----- Internal helpers -----
static double julian_day_ut(const Chart::BirthData& birth) {
    return swe_detail::julian_day_ut(birth.birth_date, birth.birth_time, birth.tz_offset);
}

static NakshatraInfo calc_nakshatra(Degrees lon) {
    constexpr double nakshatra_span = 360.0 / 27.0;
    int idx = static_cast<int>(lon / nakshatra_span) % 27;
    Nakshatra n = static_cast<Nakshatra>(idx);
    Degrees pada_deg = std::fmod(lon, nakshatra_span);
    int pada = static_cast<int>(pada_deg / (nakshatra_span / 4.0)) + 1;
    return {n, pada, nakshatra_lord(n)};
}

static PlanetPosition calc_planet(Planet p, double lon, double speed, 
                                   const AscendantInfo& asc, double sun_lon) {
    PlanetPosition pos;
    pos.sign = sign_of(lon);
    pos.degree = deg_in_sign(lon);
    pos.longitude = lon;
    pos.retrograde = (p != Planet::Sun && p != Planet::Moon && 
                      p != Planet::Rahu && p != Planet::Ketu && speed < 0);
    pos.nakshatra = calc_nakshatra(lon);
    
    // House (whole sign from ascendant)
    int asc_sign_idx = static_cast<int>(asc.sign);
    int planet_sign_idx = static_cast<int>(pos.sign);
    pos.house = static_cast<House>((planet_sign_idx - asc_sign_idx + 12) % 12 + 1);
    
    // Combustion
    if (COMBUSTION_ORB[static_cast<uint8_t>(p)] > 0) {
        Degrees diff = std::abs(lon - sun_lon);
        if (diff > 180) diff = 360 - diff;
        pos.combustion.combust = diff <= COMBUSTION_ORB[static_cast<uint8_t>(p)];
        pos.combustion.orb = diff;
    }
    
    return pos;
}

static VimshottariBalance calc_vimshottari_balance(const NakshatraInfo& moon_nak) {
    constexpr double nakshatra_span = 360.0 / 27.0;
    int nak_idx = static_cast<int>(moon_nak.nakshatra);
    double elapsed = std::fmod(moon_nak.nakshatra == Nakshatra::Revati ? 0.0 : 
                               (nak_idx * nakshatra_span + (moon_nak.pada - 1) * nakshatra_span / 4.0), 360.0);
    // Simplified: use nakshatra lord and pada
    Planet lord = moon_nak.lord;
    int lord_idx = -1;
    for (int i = 0; i < 9; ++i) if (DASHA_ORDER[i] == lord) { lord_idx = i; break; }
    double fraction = (moon_nak.pada - 1) / 4.0;  // rough
    double balance = DASHA_YEARS[lord_idx] * (1.0 - fraction);
    return {lord, balance};
}

// ----- Public API -----
Chart compute_chart(const Chart::BirthData& birth) {
    Chart chart;
    chart.birth = birth;
    
    // Initialize Swiss Ephemeris
    swe_detail::swe_init();
    
    double jd = julian_day_ut(birth);
    chart.julian_day_ut = jd;
    
    // Calculate planets
    double sun_lon = 0.0;
    for (int i = 0; i < 9; ++i) {
        Planet p = static_cast<Planet>(i);
        double lon, speed;
        swe_detail::calc_planet(p, jd, lon, speed);
        
        if (p == Planet::Sun) sun_lon = lon;
        
        // Temporarily store, will fix ascendant later
        chart.planets[i] = calc_planet(p, lon, speed, {Sign::Aries, 0, 0}, sun_lon);
    }
    
    // Calculate houses (whole sign)
    double cusps[13], ascmc[10];
    swe_houses_ex(jd, swe_detail::SWE_FLAGS, birth.latitude, birth.longitude, 'W', cusps, ascmc);
    
    AscendantInfo asc;
    asc.longitude = std::fmod(ascmc[0], 360.0);
    asc.sign = sign_of(asc.longitude);
    asc.degree = deg_in_sign(asc.longitude);
    chart.ascendant = asc;
    
    // Recalculate planet houses with correct ascendant
    for (auto& pos : chart.planets) {
        int asc_sign_idx = static_cast<int>(asc.sign);
        int planet_sign_idx = static_cast<int>(pos.sign);
        pos.house = static_cast<House>((planet_sign_idx - asc_sign_idx + 12) % 12 + 1);
    }
    
    // Lagna lord
    static constexpr Planet SIGN_LORDS[12] = {
        Planet::Mars, Planet::Venus, Planet::Mercury, Planet::Moon, Planet::Sun, Planet::Mercury,
        Planet::Venus, Planet::Mars, Planet::Jupiter, Planet::Saturn, Planet::Saturn, Planet::Jupiter
    };
    chart.lagna_lord = SIGN_LORDS[static_cast<uint8_t>(asc.sign)];
    
    // Moon nakshatra
    chart.moon_nakshatra = chart.planets[static_cast<uint8_t>(Planet::Moon)].nakshatra;
    
    // Vimshottari balance
    chart.vimshottari_balance = calc_vimshottari_balance(chart.moon_nakshatra);
    
    // Houses
    for (int i = 0; i < 12; ++i) {
        chart.houses[i] = {static_cast<House>(i + 1), sign_of(cusps[i])};
    }
    
    // Aspects
    for (int i = 0; i < 9; ++i) {
        Planet p = static_cast<Planet>(i);
        int house_p = static_cast<int>(chart.planets[i].house);
        
        for (int j = 0; j < 9; ++j) {
            if (i == j) continue;
            Planet q = static_cast<Planet>(j);
            int house_q = static_cast<int>(chart.planets[j].house);
            int dist = (house_q - house_p + 12) % 12;
            
            AspectType type = AspectType::Conjunction;
            bool has_aspect = false;
            
            if (dist == 0) { type = AspectType::Conjunction; has_aspect = true; }
            else if (dist == 6) { type = AspectType::Opposition; has_aspect = true; }
            else if (p == Planet::Jupiter && (dist == 4 || dist == 8)) { type = AspectType::Trine; has_aspect = true; }
            else if (p == Planet::Mars && (dist == 3 || dist == 7)) { type = AspectType::MarsSpecial; has_aspect = true; }
            else if (p == Planet::Saturn && (dist == 2 || dist == 9)) { type = AspectType::SaturnSpecial; has_aspect = true; }
            else if ((p == Planet::Rahu || p == Planet::Ketu) && dist == 4) { type = AspectType::RahuKetuSpecial; has_aspect = true; }
            
            if (has_aspect) {
                chart.aspects.push_back({p, q, type, 0.0});
            }
        }
    }
    
    // TODO: Yogas, Advice - implement later
    
    return chart;
}

} // namespace jyotish