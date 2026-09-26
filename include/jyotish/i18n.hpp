#pragma once

#include <string>
#include <unordered_map>

namespace jyotish {

struct I18n {
    std::unordered_map<std::string, std::string> sign;
    std::unordered_map<std::string, std::string> planet;
    std::unordered_map<std::string, std::string> nakshatra;
    std::string lagna_word, lord_word, planets_word, moon_nakshatra_word, nakshatra_word, house_word;
    std::string balance_word, years_word, retro_word, combust_word;
    std::string aspects_word, yogas_word, areas_word, why_word;
    std::unordered_map<std::string, std::string> aspect;
};

const I18n& get(const std::string& lang = "ru");

} // namespace jyotish