#pragma once

#include <string>
#include <vector>
#include <optional>

namespace jyotish::geocode {

struct CityInfo {
    std::string name;
    double latitude = 0.0;
    double longitude = 0.0;
    double tz_offset = 0.0;
    std::string country;            // region/country label for display
    long long population = 0;       // tie-breaker: prefer bigger cities
    std::vector<std::string> keys;  // alternative names (raw, for reference)
};

struct CityMatch {
    CityInfo city;
    int score = 0;
};

// Ranked search: exact -> prefix -> substring -> fuzzy (typos/declensions).
std::vector<CityMatch> search(const std::string& query, int limit = 8);
std::vector<CityInfo> find_cities(const std::string& query, int limit = 8);

// Exact normalized-alias lookup (used for fast n-gram extraction).
std::optional<CityInfo> resolve_exact(const std::string& name);

// Like resolve_exact, but also tries common case endings (declensions):
// "в Москве" / "из Казани" -> Москва / Казань. Prefers the largest city.
std::optional<CityInfo> resolve_loose(const std::string& name);

// Best-effort single city for a free-form name (routes through search).
std::optional<CityInfo> resolve_city(const std::string& name);

std::vector<CityInfo> get_all_cities();

// Exposed for tests / reuse.
std::string normalize_city(const std::string& s);   // fold case, ё->е, strip diacritics/spaces/punct
std::string transliterate(const std::string& s);    // Cyrillic -> Latin (other chars kept)

} // namespace jyotish::geocode
