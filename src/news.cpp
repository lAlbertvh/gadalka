#include <jyotish/news.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>
#include <cctype>

namespace jyotish::news {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string html_unescape(std::string s) {
    auto repl = [&](const std::string& from, const char* to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += strlen(to);
        }
    };
    repl("&amp;", "&");
    repl("&lt;", "<");
    repl("&gt;", ">");
    repl("&quot;", "\"");
    repl("&apos;", "'");
    repl("&#39;", "'");
    repl("&#8212;", "—");
    repl("&nbsp;", " ");
    return s;
}

std::string strip_tags(const std::string& s) {
    std::string out;
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out += c;
    }
    return out;
}

// Parse RSS/Atom XML body into items (title + link + dc:date/pubDate).
std::vector<NewsItem> parse_rss(const std::string& xml, const std::string& fallback_source) {
    std::vector<NewsItem> items;
    // Split into <item>...</item> elements (RSS) or <entry>...</entry> (Atom).
    for (const std::string& tag : {std::string("<item"), std::string("<entry")}) {
        size_t pos = 0;
        while ((pos = xml.find(tag, pos)) != std::string::npos) {
            size_t close = xml.find('>', pos);
            size_t end = xml.find("</" + tag.substr(1), close);
            if (close == std::string::npos || end == std::string::npos) break;
            std::string body = xml.substr(close + 1, end - close - 1);
            pos = end + tag.size() + 2;

            NewsItem item;
            item.source = fallback_source;
            std::smatch m;
            if (std::regex_search(body, m, std::regex("<title[^>]*>(.*?)</title>", std::regex::icase))) {
                item.title = html_unescape(trim(strip_tags(m[1].str())));
            }
            if (std::regex_search(body, m, std::regex("<link[^>]*>(.*?)</link>", std::regex::icase))) {
                item.link = trim(m[1].str());
            } else if (std::regex_search(body, m, std::regex("<link[^>]*href=[\"'](.*?)[\"']", std::regex::icase))) {
                item.link = m[1].str();
            }
            if (std::regex_search(body, m, std::regex("<pubDate[^>]*>(.*?)</pubDate>", std::regex::icase))) {
                item.date = trim(m[1].str());
            } else if (std::regex_search(body, m, std::regex("<dc:date[^>]*>(.*?)</dc:date>", std::regex::icase))) {
                item.date = trim(m[1].str());
            } else if (std::regex_search(body, m, std::regex("<updated[^>]*>(.*?)</updated>", std::regex::icase))) {
                item.date = trim(m[1].str());
            }
            if (!item.title.empty()) items.push_back(std::move(item));
        }
    }
    return items;
}

std::string fetch(const std::string& url, int timeout_s) {
    std::string rest = url;
    std::string scheme = "http";
    if (rest.rfind("http://", 0) == 0) rest = rest.substr(7);
    else if (rest.rfind("https://", 0) == 0) { scheme = "https"; rest = rest.substr(8); }
    else return "";

    size_t slash = rest.find('/');
    std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

    httplib::Client cli((scheme == "https" ? "https://" : "http://") + host);
    cli.set_connection_timeout(timeout_s);
    cli.set_read_timeout(timeout_s);
    auto res = cli.Get(path, {{"User-Agent", "jyotish-news/1.0"}});
    if (!res || res->status != 200) return "";
    return res->body;
}

// "Wed, 16 Sep 2026 12:00:00 +0300" -> "2026-09-16" (best effort)
std::string to_iso_date(const std::string& rfc) {
    static const std::vector<std::pair<std::string, std::string>> months = {
        {"jan", "01"}, {"feb", "02"}, {"mar", "03"}, {"apr", "04"},
        {"may", "05"}, {"jun", "06"}, {"jul", "07"}, {"aug", "08"},
        {"sep", "09"}, {"oct", "10"}, {"nov", "11"}, {"dec", "12"},
    };
    std::smatch m;
    if (std::regex_search(rfc, m, std::regex("([0-9]{1,2})\\s+([A-Za-z]{3})[^ ]*\\s+([0-9]{4})"))) {
        std::string day = m[1].str();
        std::string mon3 = m[2].str();
        for (auto& c : mon3) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string month;
        for (const auto& [abbr, num] : months) {
            if (abbr == mon3) { month = num; break; }
        }
        if (month.empty()) return rfc;
        return m[3].str() + "-" + month + "-" + (day.size() == 1 ? "0" : "") + day;
    }
    // ISO already: 2026-09-16T...
    if (std::regex_search(rfc, m, std::regex("([0-9]{4}-[0-9]{2}-[0-9]{2})"))) return m[1].str();
    return rfc;
}

} // namespace

int refresh_news(const std::string& cache_file,
                 const std::vector<std::string>& feed_urls,
                 const std::string& lang) {
    (void)lang;
    std::vector<NewsItem> all;
    int idx = 0;
    for (const auto& url : feed_urls) {
        std::string xml = fetch(url, 10);
        auto parsed = parse_rss(xml, "feed" + std::to_string(idx));
        idx++;
        for (auto& it : parsed) {
            it.date = to_iso_date(it.date);
            all.push_back(std::move(it));
        }
    }
    if (all.empty()) return 0;

    // Sort by date desc (best-effort on ISO strings); keep stable.
    std::stable_sort(all.begin(), all.end(), [](const NewsItem& a, const NewsItem& b) {
        return a.date > b.date;
    });
    if (all.size() > 50) all.resize(50);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& it : all) {
        arr.push_back({{"title", it.title}, {"source", it.source}, {"link", it.link}, {"date", it.date}});
    }
    std::ofstream f(cache_file, std::ios::trunc);
    if (!f) return 0;
    f << arr.dump();
    return static_cast<int>(arr.size());
}

std::vector<NewsItem> load_news(const std::string& cache_file, size_t max_items) {
    std::ifstream f(cache_file);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        auto j = nlohmann::json::parse(ss.str());
        if (!j.is_array()) return {};
        std::vector<NewsItem> out;
        for (const auto& e : j) {
            out.push_back({e.value("title", ""), e.value("source", ""), e.value("link", ""), e.value("date", "")});
        }
        if (out.size() > max_items) out.resize(max_items);
        return out;
    } catch (...) {
        return {};
    }
}

std::string news_block(const std::vector<NewsItem>& items, const std::string& lang) {
    if (items.empty()) return "";
    std::string header;
    if (lang == "en") {
        header = "RECENT NEWS (last 24-48h, for context — mention only if relevant to the user's question; never invent facts beyond these headlines):";
    } else {
        header = "СВЕЖИЕ НОВОСТИ (за последние 24–48 ч, для контекста — упоминай, только если уместно к вопросу; фактов сверх этих заголовков не выдумывай):";
    }
    std::string out = header + "\n";
    int n = 0;
    for (const auto& it : items) {
        if (n++ >= 12) break;
        out += "- [" + it.date + "] " + it.title;
        if (!it.source.empty()) out += " (" + it.source + ")";
        out += "\n";
    }
    return out;
}

} // namespace jyotish::news