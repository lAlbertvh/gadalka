#pragma once

#include <string>
#include <vector>

namespace jyotish::oracle {

extern const std::vector<std::string> QUOTES_RU;
extern const std::vector<std::string> QUOTES_EN;

std::vector<std::string> sample_quotes(const std::string& lang, int n);

// How the reply tail was repaired by enforce_quote_finale().
enum class QuoteRepair {
    None,          // the tail already was a verbatim pool quote
    Canonicalized, // matched the pool but had markdown/case noise
};

// Make the reply's final line a verbatim pool quote IF the model already chose
// one — strips markdown, casing and surrounding quotes so the line matches the
// pool byte for byte.
//
// It deliberately does nothing when the tail is not a pool quote. Swapping in
// an arbitrary quote produced random, off-topic finales and read far worse than
// an occasional missing one.
QuoteRepair enforce_quote_finale(std::string& reply, const std::string& lang);

} // namespace jyotish::oracle