// Public API for core calculations
#pragma once

#include <jyotish/types.hpp>

namespace jyotish {

// Chart computation
Chart compute_chart(const Chart::BirthData& birth);

// Dasha calculations
DashaTimeline build_dasha_timeline(const Chart::BirthData& birth, const Chart& chart);
std::optional<DashaPeriod> dasha_at_date(const DashaTimeline& timeline, const std::string& target_date);

// Transit calculations
TransitSnapshot compute_transit_snapshot(const Chart& natal, const std::string& date, 
                                          const std::string& time, double tz_offset);
TransitSnapshot localize_transits(const TransitSnapshot& snap, const std::string& lang);

// Yoga detection
std::vector<Yoga> detect_yogas(const Chart& chart);

// Geocoding
struct CityInfo {
    std::string name;
    double latitude;
    double longitude;
    double tz_offset;
    std::string country;
};
std::vector<CityInfo> find_cities(const std::string& query, int limit = 8);

} // namespace jyotish