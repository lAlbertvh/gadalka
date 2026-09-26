#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace jyotish::predictions {

std::string ollama_chat(const std::vector<nlohmann::json>& messages, double temperature = 0.7, const std::string& model = "");

} // namespace jyotish::predictions