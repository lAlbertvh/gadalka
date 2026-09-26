#pragma once

#include <string>
#include <vector>

namespace jyotish::theory {

// Domain of a curated analytical framework.
enum class Domain { Macro, Psychology, Math, Philosophy };

struct Module {
    Domain domain;
    std::string tag;      // short label, e.g. "monetary policy"
    std::vector<std::string> keywords_ru;
    std::vector<std::string> keywords_en;
    std::string ru;       // the framework text (RU)
    std::string en;       // the framework text (EN)
};

// Curated analytical frameworks ("theory layer"): macroeconomics, psychology
// and probability/math heuristics that the oracle may apply when reasoning
// about world events. These are FRAMEWORKS, not facts — the block is labelled
// as such so the model never presents theory as verified data.
const std::vector<Module>& modules();

std::string domain_name(Domain d);

// Decide which domains the question touches (RU/EN keyword match).
std::vector<Domain> detect(const std::string& question, const std::string& lang);

// Build the "Analytical framework" context block for a question. Empty when
// no module matched (so neutral questions are not bloated). Deterministic.
std::string theory_block(const std::string& question, const std::string& lang);

} // namespace jyotish::theory