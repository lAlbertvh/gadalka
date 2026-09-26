#include <jyotish/grounding.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <map>
#include <string>
#include <thread>
#include <chrono>

namespace jyotish {

namespace {

int utf8_len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

bool is_combining_mark(unsigned char c0, unsigned char c1) {
    // U+0300..U+036F combining diacritics
    if (c0 == 0xCC) return c1 >= 0x80 && c1 <= 0xBF;
    if (c0 == 0xCD) return c1 >= 0x80 && c1 <= 0x8F;
    return false;
}

bool is_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string wiki_host(const std::string& lang) {
    return lang == "en" ? "https://en.wikipedia.org" : "https://ru.wikipedia.org";
}

// In-process caches so repeated checks (multi-claim messages, many users asking
// the same thing) do not hammer the MediaWiki API. Cleared when they grow big.
std::map<std::string, std::string>& title_cache() {
    static std::map<std::string, std::string> c;
    if (c.size() > 128) c.clear();
    return c;
}
std::map<std::string, std::string>& extract_cache() {
    static std::map<std::string, std::string> c;
    if (c.size() > 128) c.clear();
    return c;
}

// Slight politeness delay between outbound Wikipedia calls.
void wiki_pause() {
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
}

std::string wikipedia_top_title(const std::string& lang, const std::string& search) {
    std::string key = lang + "|" + search;
    {
        auto& c = title_cache();
        auto it = c.find(key);
        if (it != c.end()) return it->second;
    }
    httplib::Client cli(wiki_host(lang));
    cli.set_connection_timeout(8);
    cli.set_read_timeout(12);
    httplib::Headers h = {
        {"User-Agent", "JyotishOracle/0.1 (Vedic astrology app)"},
        {"Accept-Encoding", "identity"},
        {"Accept", "application/json"},
    };
    std::string path = "/w/api.php?action=opensearch&format=json&limit=1&search=" + url_encode(search);
    wiki_pause();
    auto res = cli.Get(path.c_str(), h);
    std::string title;
    if (res && res->status == 200) {
        try {
            auto j = nlohmann::json::parse(res->body);
            if (j.is_array() && j.size() >= 2 && j[1].is_array() && !j[1].empty()) {
                title = j[1][0].get<std::string>();
            }
        } catch (...) {}
    }
    if (title.empty()) title_cache().erase(key);
    else title_cache()[key] = title;
    return title;
}

std::string wikipedia_extract(const std::string& lang, const std::string& title) {
    std::string key = lang + "|" + title;
    {
        auto& c = extract_cache();
        auto it = c.find(key);
        if (it != c.end()) return it->second;
    }
    httplib::Client cli(wiki_host(lang));
    cli.set_connection_timeout(8);
    cli.set_read_timeout(15);
    httplib::Headers h = {
        {"User-Agent", "JyotishOracle/0.1 (Vedic astrology app)"},
        {"Accept-Encoding", "identity"},
        {"Accept", "application/json"},
    };
    std::string path = "/w/api.php?action=query&prop=extracts&explaintext=1&format=json&redirects=1&titles=" +
                       url_encode(title);
    wiki_pause();
    auto res = cli.Get(path.c_str(), h);
    std::string extract;
    if (res && res->status == 200) {
        try {
            auto j = nlohmann::json::parse(res->body);
            const auto& pages = j["query"]["pages"];
            if (pages.is_object()) {
                for (auto it = pages.begin(); it != pages.end(); ++it) {
                    const auto& p = it.value();
                    if (p.contains("extract")) extract = p["extract"].get<std::string>();
                }
            }
        } catch (...) {}
    }
    if (extract.empty()) extract_cache().erase(key);
    else extract_cache()[key] = extract;
    return extract;
}

// Tolerate Russian noun inflection (nominative vs oblique) in a safe way:
// a token matches if it equals the value, or if it is the value stem (value
// minus one letter) plus exactly one more letter, or the value plus one letter.
bool token_matches(const std::string& tok, const std::string& nval, bool inflect) {
    if (tok == nval) return true;
    if (!inflect || nval.size() < 4) return false;
    std::string stem = nval.substr(0, nval.size() - 1);
    if (tok.size() == stem.size() + 1 && tok.rfind(stem, 0) == 0) return true;
    if (tok.size() == nval.size() + 1 && tok.rfind(nval, 0) == 0) return true;
    return false;
}

// Does `val` appear in `text` as a standalone word/phrase?
bool token_contains(const std::string& text, const std::string& val, bool inflect) {
    if (val.empty()) return false;
    size_t pos = 0;
    while ((pos = text.find(val, pos)) != std::string::npos) {
        bool left_ok = pos == 0 || text[pos - 1] == ' ';
        bool right_ok = pos + val.size() >= text.size() || text[pos + val.size()] == ' ';
        if (left_ok && right_ok) return true;
        pos += val.size();
    }
    if (!inflect) return false;
    // Search each token boundary for an inflected form of the value.
    size_t i = 0;
    while (i <= text.size()) {
        size_t end = text.find(' ', i);
        if (end == std::string::npos) end = text.size();
        std::string tok = text.substr(i, end - i);
        if (token_matches(tok, val, true)) return true;
        i = end + 1;
    }
    return false;
}

// Confirm a single candidate (subject search -> title -> extract).
// Returns true when the article is about the subject and contains the value.
bool confirm(const std::string& lang, const std::string& search, const std::string& nval) {
    std::string title = wikipedia_top_title(lang, search);
    if (title.empty()) return false;
    std::string extract = wikipedia_extract(lang, title);
    if (extract.empty()) return false;
    std::string ntext = ground_normalize(extract);
    std::string ntitle = ground_normalize(title);
    if (ntext.find(ntitle) == std::string::npos) return false;
    return token_contains(ntext, nval, lang == "ru");
}

} // namespace

std::string ground_normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        int len = utf8_len(b0);
        if (len == 1) {
            if (is_space(b0)) out += ' ';
            else if (std::isalnum(b0)) out += static_cast<char>(std::tolower(b0));
            i += 1;
            continue;
        }
        if (len >= 2 && i + 1 < s.size() &&
            is_combining_mark(b0, static_cast<unsigned char>(s[i + 1]))) {
            i += 2;
            continue;
        }
        if (len == 2 && i + 1 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if ((b0 == 0xD0 || b0 == 0xD1) && b1 >= 0x80 && b1 <= 0xBF) {
                if ((b0 == 0xD1 && b1 == 0x91) || (b0 == 0xD0 && b1 == 0x81)) {
                    out += '\xD0'; out += '\xB5';            // ё/Ё -> е
                } else if (b0 == 0xD0 && (b1 == 0xB9 || b1 == 0x99)) { // й/Й (U+0439/U+0419) -> и
                    out += '\xD0'; out += '\xB8';
                } else {
                    unsigned char l = b1;
                    if (b0 == 0xD0 && b1 >= 0x90 && b1 <= 0xAF) l = static_cast<unsigned char>(b1 + 0x20);
                    out += static_cast<char>(b0);
                    out += static_cast<char>(l);
                }
                i += 2;
                continue;
            }
        }
        i += static_cast<size_t>(len);
    }
    std::string collapsed;
    collapsed.reserve(out.size());
    bool prev_space = false;
    for (char c : out) {
        if (c == ' ') {
            if (!prev_space) collapsed += c;
            prev_space = true;
        } else {
            collapsed += c;
            prev_space = false;
        }
    }
    return collapsed;
}

bool wikipedia_grounds(const std::string& subject, const std::string& value,
                       const std::string& lang) {
    if (subject.empty() || value.empty()) return false;
    std::string nval = ground_normalize(value);
    if (nval.size() < 2) return false;
    std::string next = ground_normalize(subject);
    if (next.empty()) return false;

    // Try the raw subject and then normalized variants (handles the
    // «Йена — город» / «Иена — валюта» ambiguity via й->и folding).
    std::vector<std::string> candidates;
    candidates.push_back(subject);
    candidates.push_back(next);
    if (subject != next) candidates.push_back(next);
    for (const auto& c : candidates) {
        if (c.empty()) continue;
        if (confirm(lang, c, nval)) return true;
    }
    return false;
}

} // namespace jyotish