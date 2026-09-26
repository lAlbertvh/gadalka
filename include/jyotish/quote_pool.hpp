#pragma once

#include <string>
#include <vector>

namespace jyotish::oracle {

extern const std::vector<std::string> QUOTES_RU;
extern const std::vector<std::string> QUOTES_EN;

std::vector<std::string> sample_quotes(const std::string& lang, int n);

} // namespace jyotish::oracle