#pragma once

#include <jyotish/types.hpp>
#include <jyotish/core.hpp>

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

} // namespace jyotish