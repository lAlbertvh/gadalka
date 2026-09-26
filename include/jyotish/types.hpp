#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace jyotish {

// ----- Basic types -----
using JulianDay = double;
using Degrees = double;

enum class Planet : uint8_t {
    Sun = 0, Moon, Mars, Mercury, Jupiter, Venus, Saturn, Rahu, Ketu, Count
};

enum class Sign : uint8_t {
    Aries = 0, Taurus, Gemini, Cancer, Leo, Virgo,
    Libra, Scorpio, Sagittarius, Capricorn, Aquarius, Pisces
};

enum class House : uint8_t {
    H1 = 1, H2, H3, H4, H5, H6, H7, H8, H9, H10, H11, H12
};

enum class Nakshatra : uint8_t {
    Ashwini = 0, Bharani, Krittika, Rohini, Mrigashira, Ardra,
    Punarvasu, Pushya, Ashlesha, Magha, PurvaPhalguni, UttaraPhalguni,
    Hasta, Chitra, Swati, Vishakha, Anuradha, Jyeshtha,
    Mula, PurvaAshadha, UttaraAshadha, Shravana, Dhanishta, Shatabhisha,
    PurvaBhadrapada, UttaraBhadrapada, Revati
};

enum class AspectType {
    Conjunction, Opposition, Trine, Square, Sextile,
    JupiterSpecial, MarsSpecial, SaturnSpecial, RahuKetuSpecial
};

// ----- Data structures -----
struct NakshatraInfo {
    Nakshatra nakshatra;
    int pada;  // 1-4
    Planet lord;
};

struct PlanetPosition {
    Sign sign;
    House house;
    Degrees degree;       // 0-30 within sign
    Degrees longitude;    // 0-360 absolute
    bool retrograde = false;
    NakshatraInfo nakshatra;
    struct CombustionInfo {
        bool combust = false;
        Degrees orb = 0.0;
    } combustion;
};

struct AscendantInfo {
    Sign sign;
    Degrees degree;
    Degrees longitude;
};

struct VimshottariBalance {
    Planet lord;
    double years_remaining;
};

struct HouseInfo {
    House house;
    Sign sign;
};

struct Aspect {
    Planet from;
    Planet to;
    AspectType type;
    Degrees orb;
};

struct Yoga {
    std::string name_ru;
    std::string name_en;
    std::string description_ru;
    std::string description_en;
};

// ----- Full chart -----
struct Chart {
    // Birth data (input)
    struct BirthData {
        std::string name;
        std::string birth_date;   // YYYY-MM-DD
        std::string birth_time;   // HH:MM
        double latitude = 0.0;
        double longitude = 0.0;
        double tz_offset = 0.0;   // hours from UTC
        std::string city;
    } birth;

    // Computed
    JulianDay julian_day_ut = 0.0;
    AscendantInfo ascendant;
    Planet lagna_lord = Planet::Sun;
    std::array<PlanetPosition, 9> planets;  // indexed by Planet enum
    NakshatraInfo moon_nakshatra;
    std::vector<Aspect> aspects;
    VimshottariBalance vimshottari_balance;
    std::array<HouseInfo, 12> houses;
    std::vector<Yoga> yogas;
    
    // Advice (lifestyle)
    struct Advice {
        struct Item { std::string text; std::string reason; };
        std::vector<Item> nutrition;
        std::vector<Item> lifestyle;
        std::vector<Item> sport;
        struct Dosha { std::string label; Sign lagna; std::string element; } dosha;
    } advice;
};

// ----- Dasha -----
struct Antardasha {
    Planet lord;
    std::string start;  // YYYY-MM-DD
    std::string end;
    double years;
};

struct Mahadasha {
    Planet lord;
    std::string start;
    std::string end;
    double years;
    std::vector<Antardasha> antardashas;
};

using DashaTimeline = std::vector<Mahadasha>;

struct DashaPeriod {
    Planet mahadasha;
    std::string maha_start, maha_end;
    double maha_years;
    std::optional<Planet> antardasha;
    std::optional<std::string> ad_start, ad_end;
};

// ----- Transits -----
struct TransitPlanet {
    Sign sign;
    Degrees degree;
    Degrees longitude;
    std::optional<int> natal_house;
};

struct TransitConjunction {
    Planet transit;
    Planet natal;
    Degrees orb;
};

struct TransitSnapshot {
    std::string date;
    std::string time;
    double tz_offset;
    AscendantInfo ascendant;
    std::array<TransitPlanet, 9> planets;
    std::vector<TransitConjunction> conjunctions;
};

// ----- Utility -----
inline const char* planet_name(Planet p) {
    static constexpr const char* names[] = {
        "Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn", "Rahu", "Ketu"
    };
    return names[static_cast<uint8_t>(p)];
}

inline const char* sign_name(Sign s) {
    static constexpr const char* names[] = {
        "Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"
    };
    return names[static_cast<uint8_t>(s)];
}

inline const char* nakshatra_name(Nakshatra n) {
    static constexpr const char* names[] = {
        "Ashwini", "Bharani", "Krittika", "Rohini", "Mrigashira", "Ardra",
        "Punarvasu", "Pushya", "Ashlesha", "Magha", "Purva Phalguni", "Uttara Phalguni",
        "Hasta", "Chitra", "Swati", "Vishakha", "Anuradha", "Jyeshtha",
        "Mula", "Purva Ashadha", "Uttara Ashadha", "Shravana", "Dhanishta", "Shatabhisha",
        "Purva Bhadrapada", "Uttara Bhadrapada", "Revati"
    };
    return names[static_cast<uint8_t>(n)];
}

inline Planet nakshatra_lord(Nakshatra n) {
    static constexpr Planet lords[] = {
        Planet::Ketu, Planet::Venus, Planet::Sun, Planet::Moon, Planet::Mars, Planet::Rahu,
        Planet::Jupiter, Planet::Saturn, Planet::Mercury, Planet::Ketu, Planet::Venus, Planet::Sun,
        Planet::Moon, Planet::Mars, Planet::Rahu, Planet::Jupiter, Planet::Saturn, Planet::Mercury,
        Planet::Ketu, Planet::Venus, Planet::Sun, Planet::Moon, Planet::Mars, Planet::Rahu,
        Planet::Jupiter, Planet::Saturn, Planet::Mercury
    };
    return lords[static_cast<uint8_t>(n)];
}

inline Sign sign_of(Degrees lon) {
    return static_cast<Sign>(static_cast<int>(lon / 30.0) % 12);
}

inline Degrees deg_in_sign(Degrees lon) {
    return std::fmod(lon, 30.0);
}

// Dasha constants
inline constexpr std::array<Planet, 9> DASHA_ORDER = {
    Planet::Ketu, Planet::Venus, Planet::Sun, Planet::Moon, Planet::Mars,
    Planet::Rahu, Planet::Jupiter, Planet::Saturn, Planet::Mercury
};

inline constexpr std::array<int, 9> DASHA_YEARS = {7, 20, 6, 10, 7, 18, 16, 19, 17};

// ----- JSON serialization -----
void to_json(nlohmann::json& j, const Chart& c);
void from_json(const nlohmann::json& j, Chart& c);

void to_json(nlohmann::json& j, const DashaTimeline& dt);
void to_json(nlohmann::json& j, const TransitSnapshot& ts);

// In-place add *_local fields (sign_local, planet_local, nakshatra_local, ...)
// so the UI can render names fully in the chosen language (RU/EN) instead of
// raw English enum names.
void localize_chart_json(nlohmann::json& j, const std::string& lang);
void localize_transit_json(nlohmann::json& j, const std::string& lang);

} // namespace jyotish