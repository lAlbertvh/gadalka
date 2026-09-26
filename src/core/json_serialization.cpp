#include <jyotish/types.hpp>
#include <jyotish/i18n.hpp>
#include <nlohmann/json.hpp>

namespace jyotish {

namespace {
template <class Map>
std::string loc(const Map& tbl, const std::string& key) {
    auto it = tbl.find(key);
    return it != tbl.end() ? it->second : key;
}
}

void localize_chart_json(nlohmann::json& j, const std::string& lang) {
    const auto& t = get(lang);
    auto ls = [&](const std::string& s) { return loc(t.sign, s); };
    auto lp = [&](const std::string& s) { return loc(t.planet, s); };
    auto ln = [&](const std::string& s) { return loc(t.nakshatra, s); };

    if (j.contains("ascendant") && j["ascendant"].is_object() && j["ascendant"].contains("sign"))
        j["ascendant"]["sign_local"] = ls(j["ascendant"]["sign"].get<std::string>());
    if (j.contains("lagna_lord") && j["lagna_lord"].is_string())
        j["lagna_lord_local"] = lp(j["lagna_lord"].get<std::string>());
    if (j.contains("planets") && j["planets"].is_array()) {
        int idx = 0;
        for (auto& p : j["planets"]) {
            if (!p.is_object()) { ++idx; continue; }
            if (idx < 9) {                       // chart.planets order == Planet enum
                std::string pname = planet_name(static_cast<Planet>(idx));
                p["name"] = pname;
                p["name_local"] = lp(pname);
            }
            if (p.contains("sign") && p["sign"].is_string())
                p["sign_local"] = ls(p["sign"].get<std::string>());
            if (p.contains("nakshatra") && p["nakshatra"].is_object()) {
                auto& n = p["nakshatra"];
                if (n.contains("name") && n["name"].is_string()) n["name_local"] = ln(n["name"].get<std::string>());
                if (n.contains("lord") && n["lord"].is_string()) n["lord_local"] = lp(n["lord"].get<std::string>());
            }
            ++idx;
        }
    }
    if (j.contains("moon_nakshatra") && j["moon_nakshatra"].is_object()) {
        auto& n = j["moon_nakshatra"];
        if (n.contains("name") && n["name"].is_string()) n["name_local"] = ln(n["name"].get<std::string>());
        if (n.contains("lord") && n["lord"].is_string()) n["lord_local"] = lp(n["lord"].get<std::string>());
    }
    if (j.contains("vimshottari_balance") && j["vimshottari_balance"].is_object()
        && j["vimshottari_balance"].contains("lord") && j["vimshottari_balance"]["lord"].is_string())
        j["vimshottari_balance"]["lord_local"] = lp(j["vimshottari_balance"]["lord"].get<std::string>());
    if (j.contains("houses") && j["houses"].is_array()) {
        for (auto& h : j["houses"])
            if (h.is_object() && h.contains("sign") && h["sign"].is_string())
                h["sign_local"] = ls(h["sign"].get<std::string>());
    }
    if (j.contains("aspects") && j["aspects"].is_array()) {
        for (auto& a : j["aspects"]) {
            if (!a.is_object()) continue;
            if (a.contains("from") && a["from"].is_string()) a["from_local"] = lp(a["from"].get<std::string>());
            if (a.contains("to") && a["to"].is_string()) a["to_local"] = lp(a["to"].get<std::string>());
        }
    }
}

void localize_transit_json(nlohmann::json& j, const std::string& lang) {
    const auto& t = get(lang);
    auto ls = [&](const std::string& s) { return loc(t.sign, s); };
    auto lp = [&](const std::string& s) { return loc(t.planet, s); };

    if (j.contains("ascendant") && j["ascendant"].is_object() && j["ascendant"].contains("sign"))
        j["ascendant"]["sign_local"] = ls(j["ascendant"]["sign"].get<std::string>());
    if (j.contains("planets") && j["planets"].is_array()) {
        int idx = 0;
        for (auto& p : j["planets"]) {
            if (!p.is_object()) { ++idx; continue; }
            if (idx < 9) {                       // transit planets order == Planet enum
                std::string pname = planet_name(static_cast<Planet>(idx));
                p["name"] = pname;
                p["name_local"] = lp(pname);
            }
            if (p.contains("sign") && p["sign"].is_string())
                p["sign_local"] = ls(p["sign"].get<std::string>());
            ++idx;
        }
    }
    if (j.contains("conjunctions") && j["conjunctions"].is_array()) {
        for (auto& c : j["conjunctions"]) {
            if (!c.is_object()) continue;
            if (c.contains("transit") && c["transit"].is_string()) c["transit_local"] = lp(c["transit"].get<std::string>());
            if (c.contains("natal") && c["natal"].is_string()) c["natal_local"] = lp(c["natal"].get<std::string>());
        }
    }
}

void to_json(nlohmann::json& j, const Chart::BirthData& b) {
    j["name"] = b.name;
    j["birth_date"] = b.birth_date;
    j["birth_time"] = b.birth_time;
    j["latitude"] = b.latitude;
    j["longitude"] = b.longitude;
    j["tz_offset"] = b.tz_offset;
    j["city"] = b.city;
}

void to_json(nlohmann::json& j, const NakshatraInfo& n) {
    j["name"] = nakshatra_name(n.nakshatra);
    j["pada"] = n.pada;
    j["lord"] = planet_name(n.lord);
}

void to_json(nlohmann::json& j, const PlanetPosition& p) {
    j = nlohmann::json{
        {"sign", sign_name(p.sign)},
        {"house", static_cast<int>(p.house)},
        {"degree", p.degree},
        {"longitude", p.longitude},
        {"retrograde", p.retrograde},
        {"nakshatra", {
            {"name", nakshatra_name(p.nakshatra.nakshatra)},
            {"pada", p.nakshatra.pada},
            {"lord", planet_name(p.nakshatra.lord)}
        }}
    };
    if (p.combustion.combust) {
        j["combustion"] = {{"combust", true}, {"orb", p.combustion.orb}};
    } else {
        j["combustion"] = {{"combust", false}};
    }
}

void to_json(nlohmann::json& j, const AscendantInfo& a) {
    j = nlohmann::json{
        {"sign", sign_name(a.sign)},
        {"degree", a.degree},
        {"longitude", a.longitude}
    };
}

void to_json(nlohmann::json& j, const VimshottariBalance& v) {
    j = nlohmann::json{
        {"lord", planet_name(v.lord)},
        {"years", v.years_remaining}
    };
}

void to_json(nlohmann::json& j, const HouseInfo& h) {
    j = nlohmann::json{
        {"house", static_cast<int>(h.house)},
        {"sign", sign_name(h.sign)}
    };
}

void to_json(nlohmann::json& j, const Aspect& a) {
    j = nlohmann::json{
        {"from", planet_name(a.from)},
        {"to", planet_name(a.to)},
        {"type", a.type == AspectType::Conjunction ? "conjunction" :
               a.type == AspectType::Opposition ? "opposition" :
               a.type == AspectType::Trine ? "trine" : "other"},
        {"orb", a.orb}
    };
}

void to_json(nlohmann::json& j, const Yoga& y) {
    j["name_ru"] = y.name_ru;
    j["name_en"] = y.name_en;
    j["description_ru"] = y.description_ru;
    j["description_en"] = y.description_en;
}

void to_json(nlohmann::json& j, const Chart& c) {
    j["birth"] = c.birth;
    j["julian_day_ut"] = c.julian_day_ut;
    j["ascendant"] = c.ascendant;
    j["lagna_lord"] = planet_name(c.lagna_lord);
    j["planets"] = c.planets;
    j["moon_nakshatra"] = c.moon_nakshatra;
    j["aspects"] = c.aspects;
    j["vimshottari_balance"] = c.vimshottari_balance;
    j["houses"] = c.houses;
    j["yogas"] = c.yogas;
}

void to_json(nlohmann::json& j, const Antardasha& a) {
    j["lord"] = planet_name(a.lord);
    j["start"] = a.start;
    j["end"] = a.end;
    j["years"] = a.years;
}

void to_json(nlohmann::json& j, const Mahadasha& m) {
    j["lord"] = planet_name(m.lord);
    j["start"] = m.start;
    j["end"] = m.end;
    j["years"] = m.years;
    j["antardashas"] = m.antardashas;
}

void to_json(nlohmann::json& j, const DashaTimeline& dt) {
    j = nlohmann::json::array();
    for (const auto& m : dt) {
        j.push_back(m);
    }
}

void to_json(nlohmann::json& j, const TransitPlanet& p) {
    j["sign"] = sign_name(p.sign);
    j["degree"] = p.degree;
    j["longitude"] = p.longitude;
    if (p.natal_house) j["natal_house"] = *p.natal_house;
}

void to_json(nlohmann::json& j, const TransitConjunction& c) {
    j["transit"] = planet_name(c.transit);
    j["natal"] = planet_name(c.natal);
    j["orb"] = c.orb;
}

void to_json(nlohmann::json& j, const TransitSnapshot& ts) {
    j["date"] = ts.date;
    j["time"] = ts.time;
    j["tz_offset"] = ts.tz_offset;
    j["ascendant"] = ts.ascendant;
    j["planets"] = ts.planets;
    j["conjunctions"] = ts.conjunctions;
}

void from_json(const nlohmann::json& j, Chart& c) {
    // Not needed for now
}

} // namespace jyotish