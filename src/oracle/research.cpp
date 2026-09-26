#include <jyotish/research.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <set>

namespace jyotish::research {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) {
        unsigned char b = static_cast<unsigned char>(c);
        if (b >= 'A' && b <= 'Z') c = static_cast<char>(b - 'A' + 'a');
    }
    return s;
}

// Lowercase ASCII and common Cyrillic (А-Я, Ё). Cyrillic uppercase letters
// are 0xD0 0x81 (Ё) and 0xD0 0x90..0x9F (А-Я); lowercase a-я are 0xD0 0xB0..0xBF
// and р-я use 0xD1 0x80..0x8F. Only touch genuine CAPITAL range.
std::string lower_u8(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        if (b0 >= 'A' && b0 <= 'Z') { s[i] = static_cast<char>(b0 - 'A' + 'a'); continue; }
        if (i + 1 >= s.size()) continue;
        unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
        if (b0 == 0xD0 && b1 == 0x81) { s[i] = '\xD1'; s[i + 1] = '\x91'; continue; } // Ё -> ё
        if (b0 == 0xD0 && b1 >= 0x90 && b1 <= 0x9F) {   // А-Я -> а-я
            s[i + 1] = static_cast<char>(b1 + 0x20);
            ++i;
        }
    }
    return s;
}

bool contains_any(const std::string& text, const std::vector<const char*>& words) {
    for (const char* w : words) {
        if (text.find(w) != std::string::npos) return true;
    }
    return false;
}

std::string url_encode(const std::string& s) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string url_decode(const std::string& s) {
    auto hexval = [](char c) {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        return 0u;
    };
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>((hexval(s[i + 1]) << 4) | hexval(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
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
    auto res = cli.Get(path, {{"User-Agent", "jyotish-research/1.0"}});
    if (!res || res->status != 200) return "";
    return res->body;
}

std::string hostname(const std::string& url) {
    size_t s = url.find("://");
    size_t start = s == std::string::npos ? 0 : s + 3;
    size_t e = url.find('/', start);
    if (e == std::string::npos) e = url.size();
    std::string h = url.substr(start, e - start);
    size_t colon = h.find(':');
    if (colon != std::string::npos) h = h.substr(0, colon);
    if (h.rfind("www.", 0) == 0) h = h.substr(4);
    return h;
}

// Rough second-level domain: last two dot-labels ("en.wikipedia.org" -> "wikipedia.org").
std::string base_domain(const std::string& url) {
    std::string h = hostname(url);
    size_t dot = h.rfind('.');
    if (dot == std::string::npos) return h;
    size_t dot2 = h.rfind('.', dot - 1);
    if (dot2 == std::string::npos) return h;
    return h.substr(dot2 + 1);
}

// Parse DuckDuckGo HTML results (html.duckduckgo.com) — the endpoint that works
// without cookies. Real URLs hide in "//duckduckgo.com/l/?uddg=<percent-encoded>".
std::vector<SearchHit> parse_ddg_html(const std::string& html) {
    std::vector<SearchHit> out;
    const std::string cls = "class=\"result__a\"";
    size_t pos = 0;
    while (out.size() < 12) {
        size_t a = html.find(cls, pos);
        if (a == std::string::npos) break;
        pos = a + cls.size();

        size_t href = html.rfind("href=\"", a);
        if (href == std::string::npos) continue;
        href += 6;
        size_t href_end = html.find('"', href);
        if (href_end == std::string::npos) continue;
        std::string href_url = html_unescape(html.substr(href, href_end - href));

        std::string real;
        size_t ud = href_url.find("uddg=");
        if (ud != std::string::npos) real = url_decode(href_url.substr(ud + 5));
        else if (href_url.rfind("http", 0) == 0) real = href_url;
        else if (href_url.rfind("//", 0) == 0) real = "https:" + href_url;
        if (real.rfind("//duckduckgo.com", 0) != std::string::npos) real.clear();

        size_t gt = html.find('>', a);
        if (gt == std::string::npos) continue;
        size_t ta = html.find("</a>", gt);
        if (ta == std::string::npos) continue;
        std::string title = trim(html_unescape(strip_tags(html.substr(gt + 1, ta - gt - 1))));
        if (title.empty()) continue;

        const size_t block_end = [&]() {
            size_t nx = html.find(cls, pos);
            return nx == std::string::npos ? html.size() : nx;
        }();
        std::string snippet;
        for (const char* sc : {"class=\"result__snippet\"", "class=\"result-snippet\""}) {
            size_t s = html.find(sc, pos);
            if (s == std::string::npos || s > block_end) continue;
            size_t st = html.find('>', s);
            size_t se = html.find("</a>", st);
            if (se == std::string::npos || se > block_end) se = html.find("</div>", st);
            if (st != std::string::npos && se != std::string::npos && se > st) {
                snippet = trim(html_unescape(strip_tags(html.substr(st + 1, se - st - 1))));
            }
            break;
        }

        if (real.rfind("http", 0) != 0) continue;
        out.push_back({title, real, snippet});
    }
    return out;
}

std::vector<SearchHit> duckduckgo(const std::string& query, int max_results, int timeout_s) {
    std::string url = "https://html.duckduckgo.com/html/?q=" + url_encode(query);
    std::string body = fetch(url, timeout_s);
    if (body.empty()) return {};
    auto hits = parse_ddg_html(body);
    if (hits.size() > static_cast<size_t>(max_results)) hits.resize(static_cast<size_t>(max_results));
    return hits;
}

std::vector<SearchHit> searx(const std::string& base, const std::string& query,
                             int max_results, int timeout_s) {
    std::string base_url = base;
    while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
    std::string url = base_url + "/search?q=" + url_encode(query) + "&format=json";
    std::string body = fetch(url, timeout_s);
    if (body.empty()) return {};
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("results") || !j["results"].is_array()) return {};
        std::vector<SearchHit> out;
        for (const auto& r : j["results"]) {
            if (!r.is_object()) continue;
            SearchHit hit;
            hit.title = trim(r.value("title", ""));
            hit.url = r.value("url", "");
            hit.snippet = trim(r.value("content", ""));
            if (hit.title.empty() || hit.url.rfind("http", 0) != 0) continue;
            out.push_back(std::move(hit));
            if (out.size() >= static_cast<size_t>(max_results)) break;
        }
        return out;
    } catch (...) {
        return {};
    }
}

} // namespace

bool world_question(const std::string& question, const std::string& lang) {
    std::string q = lower_u8(question);
    static const std::vector<const char*> world_ru = {
        "политик", "президент", "выбор", "власт", "государств", "правитель",
        "министр", "войн", "конфликт", "экономик", "рынок", "рынк", "курс",
        "валюта", "биткоин", "доллар", "евро", "рубл", "фондов", "инфляц",
        "кризис", "санкц", "нефт", "медиа", "новост", "страна", "проблем", "воен",
        "арми", "референдум", "реформ", "законодат", "происход", "произош",
        "случил", "итоги", "итог", "результат", "выборлы", "электора",
        "рынка", "инвестиц", "добыч", "мир", "общест", "биржа",
        "бензин", "топлив", "заправк", "очеред", "дефицит", "поставк",
        "цен", "ценах", "цены", "цену", "дефицита", "скачк", "подорожа", "дорожа"
    };
    static const std::vector<const char*> world_en = {
        "politics", "president", "election", "elections", "govern", "minister",
        "war", "conflict", "economy", "market", "currency", "bitcoin", "dollar",
        "euro", "ruble", "stock", "inflation", "crisis", "sanction", "oil",
        "media", "news", "world", "country", "military", "army", "parliament",
        "reform", "law", "trade", "invest", "happen", "happened", "result",
        "election", "electoral", "rate", "rates", "world situation", "geopolitic",
        "fuel", "gasoline", "petrol", "gas price", "prices", "shortage", "queue",
        "station", "supply", "cost", "price hike"
    };
    const auto& words = lang == "en" ? world_en : world_ru;
    return contains_any(q, words);
}

std::vector<SearchHit> web_search(const std::string& query, int max_results,
                                  const Settings& s) {
    if (max_results <= 0) max_results = 5;
    if (s.search_provider == "searx" && !s.searx_base_url.empty()) {
        return searx(s.searx_base_url, query, max_results, s.search_timeout_ms / 1000);
    }
    if (s.search_provider == "duckduckgo" || s.search_provider.empty()) {
        return duckduckgo(query, max_results, s.search_timeout_ms / 1000);
    }
    return {};
}

std::vector<SearchHit> wiki_search(const std::string& query, const std::string& lang,
                                   int max_results) {
    if (max_results <= 0) max_results = 4;
    std::string wl = (lang == "en") ? "en" : "ru";
    std::string url = "https://" + wl + ".wikipedia.org/w/api.php?action=query&list=search"
        "&format=json&utf8=1&srlimit=" + std::to_string(max_results) +
        "&srsearch=" + url_encode(query);
    std::string body = fetch(url, 10);
    if (body.empty()) return {};
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("query") || !j["query"].contains("search")) return {};
        std::vector<SearchHit> out;
        for (const auto& r : j["query"]["search"]) {
            if (!r.is_object()) continue;
            SearchHit hit;
            hit.title = r.value("title", "");
            hit.snippet = trim(html_unescape(strip_tags(r.value("snippet", ""))));
            std::string url_title = hit.title;
            for (auto& c : url_title) if (c == ' ') c = '_';
            hit.url = "https://" + wl + ".wikipedia.org/wiki/" + url_encode(url_title);
            if (hit.title.empty()) continue;
            out.push_back(std::move(hit));
        }
        return out;
    } catch (...) {
        return {};
    }
}

Evidence research(const std::string& question, const std::string& lang,
                  const Settings& s) {
    Evidence ev;
    ev.attempted = true;

    // Current year for the secondary query ("итоги выборов 2026").
    std::string year = [&]() {
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t t = system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04d", tm.tm_year + 1900);
        return std::string(buf);
    }();
    auto has_year = [&](const std::string& q) {
        for (size_t i = 0; i + 4 <= q.size(); ++i) {
            bool all_digit = true;
            for (size_t k = 0; k < 4; ++k) if (!std::isdigit(static_cast<unsigned char>(q[i + k]))) { all_digit = false; break; }
            if (all_digit) return true;
        }
        return false;
    };

    std::vector<std::string> queries;
    queries.push_back(question);
    if (!has_year(question)) queries.push_back(question + " " + year);

    std::map<std::string, SearchHit> dedupe;
    for (const auto& q : queries) {
        auto hits = web_search(q, s.search_max_results, s);
        for (const auto& h : hits) {
            std::string key = base_domain(h.url) + "|" + lower_u8(h.title);
            if (dedupe.count(key)) continue;
            if (dedupe.size() >= static_cast<size_t>(s.search_max_results + 3)) break;
            dedupe.emplace(key, h);
            if (!h.url.empty()) ev.domains.push_back(base_domain(h.url));
        }
        if (dedupe.size() >= static_cast<size_t>(s.search_max_results)) break;
    }
    if (dedupe.empty()) return ev;  // network/provider failure -> not grounded

    // Corroboration: >=2 hits from >=2 distinct base domains.
    std::set<std::string> distinct;
    for (const auto& d : ev.domains) if (!d.empty()) distinct.insert(d);
    if (distinct.size() < 2 || dedupe.size() < 2) return ev;
    ev.grounded = true;

    std::vector<SearchHit> hits;
    for (const auto& [k, h] : dedupe) {
        (void)k;
        hits.push_back(h);
    }

    bool ru = lang == "en" ? false : true;
    std::string md;
    if (ru) {
        md += "\nПРОВЕРЕННЫЕ ДАННЫЕ ИЗ ПОИСКА (согласовано в " + std::to_string(distinct.size()) +
              " независимых источниках):\n";
    } else {
        md += "\nVERIFIED SEARCH DATA (corroborated across " + std::to_string(distinct.size()) +
              " independent sources):\n";
    }
    size_t shown = 0;
    for (const auto& h : hits) {
        if (shown >= 6) break;
        ++shown;
        std::string snip = h.snippet.empty()
            ? (ru ? "источник доступен по ссылке" : "source available at link")
            : h.snippet;
        if (snip.size() > 220) {
            snip.resize(220);
            snip += "…";
        }
        md += ru ? "- «" + h.title + "» — " + hostname(h.url) + ": " + snip
                 : "- \"" + h.title + "\" — " + hostname(h.url) + ": " + snip;
        md += "\n";
    }

    // Wikipedia grounding as an authoritative extra block.
    auto wm = wiki_search(question, lang, 4);
    if (!wm.empty()) {
        md += ru ? "\nВИКИПЕДИЯ:\n" : "\nWIKIPEDIA:\n";
        for (size_t i = 0; i < wm.size() && i < 3; ++i) {
            std::string snip = wm[i].snippet;
            if (snip.size() > 180) { snip.resize(180); snip += "…"; }
            md += ru ? "- «" + wm[i].title + "»: " + snip
                     : "- \"" + wm[i].title + "\": " + snip;
            md += "\n";
        }
    }

    md += ru
        ? "\nТЕПЕРЬ ОТВЕТЬ ПО ЭТИМ ПРОВЕРЕННЫМ ДАННЫМ: изложи факты по сути, не выдумывай цифр и деталей, которых нет в источниках; при необходимости кратко назови источник (домен)."
        : "\nNOW ANSWER FROM THESE VERIFIED FACTS: state the facts on point, do not invent figures or details not present in the sources; when needed name the source (domain) briefly.";
    ev.context_md = std::move(md);
    return ev;
}

} // namespace jyotish::research