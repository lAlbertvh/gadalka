#pragma once

#include <string>
#include <vector>

namespace jyotish::news {

struct NewsItem {
    std::string title;
    std::string source;
    std::string link;
    std::string date;  // "YYYY-MM-DD" if known
};

// Refresh the news cache from a set of RSS feeds. Returns number of items cached
// (0 if network unavailable). Errors are non-fatal: cached data (if any) stays.
int refresh_news(const std::string& cache_file,
                 const std::vector<std::string>& feed_urls,
                 const std::string& lang = "ru");

// Load the most recent news items from the cache file (deterministic order).
// Returns empty vector on missing/invalid cache.
std::vector<NewsItem> load_news(const std::string& cache_file, size_t max_items = 15);

// Build the news context block injected into model prompts. Empty when no news.
std::string news_block(const std::vector<NewsItem>& items, const std::string& lang = "ru");

} // namespace jyotish::news