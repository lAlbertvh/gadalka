#pragma once

#include <jyotish/types.hpp>
#include <string>
#include <optional>

namespace jyotish {

using DashaTimeline = std::vector<Mahadasha>;

DashaTimeline build_dasha_timeline(const std::string& birth_date, const std::string& birth_time, double tz_offset, const Chart& chart);
std::optional<DashaPeriod> dasha_at_date(const DashaTimeline& timeline, const std::string& target_date);

} // namespace jyotish