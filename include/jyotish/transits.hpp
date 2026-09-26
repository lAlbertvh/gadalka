#pragma once

#include <jyotish/types.hpp>
#include <string>
#include <optional>

namespace jyotish {

TransitSnapshot compute_transit_snapshot(const Chart& natal, const std::string& date, 
                                          const std::string& time, double tz_offset);
TransitSnapshot localize_transits(const TransitSnapshot& snap, const std::string& lang);
std::string transit_block(const TransitSnapshot& snap, const std::optional<DashaPeriod>& period, 
                          double tz_offset, const std::string& lang);

} // namespace jyotish