#pragma once

#include <jyotish/config.hpp>
#include <string>
#include <vector>

namespace jyotish::research {

struct SearchHit {
    std::string title;
    std::string url;
    std::string snippet;
};

// True when the question is about the outside world / current events
// (politics, economy, markets, currency, conflicts, media, world news)
// rather than personal astrology. Governs whether the live research step runs.
bool world_question(const std::string& question, const std::string& lang);

// Live web search over the configured provider ("duckduckgo" | "searx").
// Returns up to max_results hits; empty on network/provider failure.
std::vector<SearchHit> web_search(const std::string& query, int max_results,
                                  const Settings& s);

// Wikipedia full-text search (per language). Returns top titles with URL/snippet
// so claims can be grounded in an established source. Empty on failure.
std::vector<SearchHit> wiki_search(const std::string& query, const std::string& lang,
                                   int max_results);

struct Evidence {
    bool grounded = false;      // results corroborated by >=2 independent domains
    bool attempted = false;     // research step actually ran (network reachable)
    std::string context_md;     // verified-facts prompt block (RU/EN); "" when nothing
    std::vector<std::string> domains;
};

// Run the research step for a question in `lang`: searches the question (plus a
// current-year query), cross-checks independent domains, and appends Wikipedia
// grounding. Never throws; on any failure returns Evidence{false, false, ""}.
Evidence research(const std::string& question, const std::string& lang,
                  const Settings& s);

} // namespace jyotish::research