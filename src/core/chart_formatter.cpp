#include <jyotish/chart_formatter.hpp>
#include <jyotish/i18n.hpp>

namespace jyotish {

std::string chart_summary(const Chart& chart, const std::string& lang) {
    const auto& t = get(lang);
    std::string result;
    
    std::string nakshatra_str = nakshatra_name(chart.moon_nakshatra.nakshatra);
    
    result += t.lagna_word + ": " + t.sign.at(sign_name(chart.ascendant.sign)) + " " + std::to_string(static_cast<int>(chart.ascendant.degree)) + "°\n";
    result += t.lord_word + ": " + t.planet.at(planet_name(chart.lagna_lord)) + "\n";
    result += t.planets_word + ":\n";
    
    for (int i = 0; i < 9; ++i) {
        const auto& p = chart.planets[i];
        auto pit = t.planet.find(planet_name(static_cast<Planet>(i)));
        std::string pname = (pit != t.planet.end()) ? pit->second : planet_name(static_cast<Planet>(i));
        std::string retro = p.retrograde ? t.retro_word : "";
        std::string combust = p.combustion.combust ? t.combust_word : "";
        std::string naks;
        if (i != 1) { // Moon gets its own dedicated line below
            const std::string nk_en = nakshatra_name(p.nakshatra.nakshatra);
            const std::string nk = t.nakshatra.count(nk_en) ? t.nakshatra.at(nk_en) : nk_en;
            auto lit = t.planet.find(planet_name(p.nakshatra.lord));
            const std::string lord = (lit != t.planet.end()) ? lit->second : planet_name(p.nakshatra.lord);
            naks = ", " + t.nakshatra_word + " " + nk + " (" + lord + ")";
        }
        std::string sign_str = t.sign.count(sign_name(p.sign)) ? t.sign.at(sign_name(p.sign)) : sign_name(p.sign);
        result += "  " + pname + ": " + sign_str + " " + std::to_string(static_cast<int>(p.degree)) + "°, " + t.house_word + " " + std::to_string(static_cast<int>(p.house)) + retro + combust + naks + "\n";
    }
    
    std::string moon_nak_str = nakshatra_name(chart.moon_nakshatra.nakshatra);
    std::string nakshatra_display = t.nakshatra.count(nakshatra_str) ? t.nakshatra.at(nakshatra_str) : nakshatra_str;
    result += t.moon_nakshatra_word + ": " + nakshatra_display + " (" + t.planet.at(planet_name(chart.moon_nakshatra.lord)) + ")\n";
    
    auto it = t.planet.find(planet_name(chart.vimshottari_balance.lord));
    std::string lord_name = (it != t.planet.end()) ? it->second : planet_name(chart.vimshottari_balance.lord);
    result += t.balance_word + ": " + (lang == "ru" ? "махадаша " : "mahadasha ") + lord_name + ", " + 
              (lang == "ru" ? "остаток " : "balance ") + std::to_string(static_cast<int>(chart.vimshottari_balance.years_remaining)) + " " + t.years_word + "\n";
    
    if (!chart.aspects.empty()) {
        result += t.aspects_word + ":\n";
        for (const auto& a : chart.aspects) {
            auto fit = t.planet.find(planet_name(a.from));
            auto tit = t.planet.find(planet_name(a.to));
            std::string from_name = (fit != t.planet.end()) ? fit->second : planet_name(a.from);
            std::string to_name = (tit != t.planet.end()) ? tit->second : planet_name(a.to);
            std::string type_str;
        switch (a.type) {
            case AspectType::Conjunction: type_str = "conjunction"; break;
            case AspectType::Opposition: type_str = "opposition"; break;
            case AspectType::Trine: type_str = "trine"; break;
            case AspectType::Square: type_str = "square"; break;
            case AspectType::Sextile: type_str = "sextile"; break;
            case AspectType::JupiterSpecial: type_str = "jupiter_special"; break;
            case AspectType::MarsSpecial: type_str = "mars_special"; break;
            case AspectType::SaturnSpecial: type_str = "saturn_special"; break;
            case AspectType::RahuKetuSpecial: type_str = "rahu_ketu_special"; break;
            default: type_str = "other";
        }
        std::string type = t.aspect.count(type_str) ? t.aspect.at(type_str) : type_str;
            result += "  " + from_name + " → " + to_name + " (" + type + ")\n";
        }
    }
    
    if (!chart.yogas.empty()) {
        result += t.yogas_word + ":\n";
        for (const auto& y : chart.yogas) {
            std::string name = lang == "ru" ? y.name_ru : y.name_en;
            result += "  " + name + "\n";
        }
    }
    
    return result;
}

} // namespace jyotish