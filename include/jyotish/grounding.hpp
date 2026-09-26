#pragma once

#include <string>
#include <optional>

namespace jyotish {

// Verify a proposed factual correction against Wikipedia.
//
// `subject` is the entity the claim is about (e.g. "Австралия"),
// `value`   is the corrected fact (e.g. "Канберра").
// Returns true when the subject's Wikipedia article (in `lang` — "ru"/"en")
// demonstrably contains the corrected value, i.e. the correction is grounded.
bool wikipedia_grounds(const std::string& subject, const std::string& value,
                       const std::string& lang = "ru");

// Normalize text for tolerant matching (lowercase, ё->е, drop accents/punct).
std::string ground_normalize(const std::string& s);

} // namespace jyotish