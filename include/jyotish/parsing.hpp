#pragma once

#include <jyotish/types.hpp>
#include <jyotish/oracle.hpp>
#include <jyotish/geocode.hpp>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace jyotish::oracle {

// Parse birth data from conversation history
BirthInfo gather_birth(const std::vector<std::string>& history_texts);
bool birth_is_ready(const BirthInfo& info);
BirthStatus birth_status(const BirthInfo& info);
std::string onboarding_reply(const BirthInfo& info, const std::string& lang);
Chart::BirthData build_birth_data(const BirthInfo& info);

// Date/time parsing
std::optional<std::pair<std::chrono::sys_days, std::string>> parse_relative_date(const std::string& text, std::chrono::sys_days base);
std::optional<std::pair<std::chrono::sys_days, std::string>> parse_iso_date(const std::string& text);
std::optional<std::string> extract_time(const std::string& text);
std::string extract_name(const std::string& text);
std::optional<int> extract_age(const std::string& text);
bool has_explicit_year(const std::string& text);
std::optional<jyotish::geocode::CityInfo> extract_city(const std::string& text, bool prefer_last = false);

// Situation detection
std::vector<std::string> detect_situations(const std::string& text, const std::string& lang);
std::string format_situations(const std::vector<std::string>& situations, const std::string& lang);

// Casual conversation (no birth data needed)
bool is_casual_question(const std::string& text, const std::string& lang, const BirthInfo* info);
std::optional<std::string> casual_answer(const std::string& text, const std::string& lang, std::chrono::sys_days day);

// Utility
std::vector<std::string> split_words(const std::string& s);
std::string join(const std::vector<std::string>& parts, const std::string& delimiter);

} // namespace jyotish::oracle