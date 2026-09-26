#include <jyotish/core.hpp>
#include <fmt/format.h>
#include <iostream>

int main() {
    using namespace jyotish;
    
    // Test chart computation
    Chart::BirthData birth;
    birth.name = "Test";
    birth.birth_date = "1995-08-15";
    birth.birth_time = "14:30";
    birth.latitude = 55.75;
    birth.longitude = 37.62;
    birth.tz_offset = 3.0;
    birth.city = "Moscow";
    
    try {
        Chart chart = compute_chart(birth);
        
        fmt::print("=== Natal Chart ===\n");
        fmt::print("Name: {}\n", chart.birth.name);
        fmt::print("Ascendant: {} {}°\n", sign_name(chart.ascendant.sign), chart.ascendant.degree);
        fmt::print("Lagna Lord: {}\n", planet_name(chart.lagna_lord));
        fmt::print("Moon Nakshatra: {} pada {}\n", nakshatra_name(chart.moon_nakshatra.nakshatra), chart.moon_nakshatra.pada);
        fmt::print("Vimshottari: {} ({:.2f} years)\n", planet_name(chart.vimshottari_balance.lord), chart.vimshottari_balance.years_remaining);
        
        fmt::print("\nPlanets:\n");
        for (int i = 0; i < 9; ++i) {
            Planet p = static_cast<Planet>(i);
            const auto& pos = chart.planets[i];
            fmt::print("  {}: {} {}° House {} {} {}\n",
                planet_name(p),
                sign_name(pos.sign), pos.degree,
                static_cast<int>(pos.house),
                pos.retrograde ? "(R)" : "",
                pos.combustion.combust ? fmt::format("[Combust {:.1f}°]", pos.combustion.orb) : ""
            );
        }
        
        fmt::print("\nHouses:\n");
        for (const auto& h : chart.houses) {
            fmt::print("  House {}: {}\n", static_cast<int>(h.house), sign_name(h.sign));
        }
        
        fmt::print("\nAspects: {}\n", chart.aspects.size());
        for (const auto& a : chart.aspects) {
            fmt::print("  {} -> {} ({})\n", planet_name(a.from), planet_name(a.to), 
                a.type == AspectType::Conjunction ? "Conj" :
                a.type == AspectType::Opposition ? "Opp" :
                a.type == AspectType::Trine ? "Trine" : "Other");
        }
        
        // Test dasha
        DashaTimeline timeline = build_dasha_timeline(birth, chart);
        fmt::print("\nDasha Timeline ({} periods):\n", timeline.size());
        for (size_t i = 0; i < std::min<size_t>(3, timeline.size()); ++i) {
            fmt::print("  {}: {} - {} ({:.1f}y) [{}]\n", 
                planet_name(timeline[i].lord), timeline[i].start, timeline[i].end, 
                timeline[i].years, timeline[i].antardashas.size());
        }
        
        fmt::print("\n✅ All core calculations work!\n");
        
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        return 1;
    }
    
    return 0;
}