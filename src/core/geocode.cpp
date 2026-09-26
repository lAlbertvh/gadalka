#include <jyotish/geocode.hpp>
#include <jyotish/config.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace jyotish::geocode {

namespace {

// ---------- UTF-8 helpers ----------

struct Cp { unsigned int code; int len; };

Cp decode(const std::string& s, size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return {c, 1};
    if ((c >> 5) == 0x6 && i + 1 < s.size())
        return {((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu), 2};
    if ((c >> 4) == 0xE && i + 2 < s.size())
        return {((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                    (static_cast<unsigned char>(s[i + 2]) & 0x3Fu),
                3};
    if ((c >> 3) == 0x1E && i + 3 < s.size())
        return {((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                    ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                    (static_cast<unsigned char>(s[i + 3]) & 0x3Fu),
                4};
    return {c, 1};
}

void append_utf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool is_cyr_letter(unsigned int cp) {
    return (cp >= 0x410 && cp <= 0x44F) || (cp >= 0x400 && cp <= 0x40F) || (cp >= 0x450 && cp <= 0x45F) ||
           cp == 0x490 || cp == 0x491 || cp == 0x406 || cp == 0x456 || cp == 0x407 || cp == 0x457 ||
           cp == 0x404 || cp == 0x454;
}

// Map Latin accented letters to their ASCII base. Returns 0 if untouched.
unsigned int latin_base(unsigned int cp) {
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
        case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105: return 'a';
        case 0x1E00: case 0x1E01: return 'a';
        case 0x00C7: case 0x00E7: case 0x0106: case 0x0107: case 0x010C: case 0x010D:
        case 0x0108: case 0x0109: case 0x010A: case 0x010B: return 'c';
        case 0x00D0: case 0x00F0: case 0x010E: case 0x010F: case 0x0110: case 0x0111: return 'd';
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: case 0x00E8: case 0x00E9:
        case 0x00EA: case 0x00EB: case 0x0112: case 0x0113: case 0x0114: case 0x0115:
        case 0x0116: case 0x0117: case 0x0118: case 0x0119: case 0x011A: case 0x011B: return 'e';
        case 0x011C: case 0x011D: case 0x011E: case 0x011F: case 0x0120: case 0x0121:
        case 0x0122: case 0x0123: return 'g';
        case 0x0124: case 0x0125: case 0x0126: case 0x0127: return 'h';
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: case 0x00EC: case 0x00ED:
        case 0x00EE: case 0x00EF: case 0x0128: case 0x0129: case 0x012A: case 0x012B:
        case 0x012C: case 0x012D: case 0x012E: case 0x012F: case 0x0130: case 0x0131: return 'i';
        case 0x0134: case 0x0135: return 'j';
        case 0x0136: case 0x0137: case 0x0138: return 'k';
        case 0x0139: case 0x013A: case 0x013B: case 0x013C: case 0x013D: case 0x013E:
        case 0x013F: case 0x0140: case 0x0141: case 0x0142: return 'l';
        case 0x00D1: case 0x00F1: case 0x0143: case 0x0144: case 0x0145: case 0x0146:
        case 0x0147: case 0x0148: case 0x0149: return 'n';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
        case 0x014C: case 0x014D: case 0x014E: case 0x014F: case 0x0150: case 0x0151: return 'o';
        case 0x0154: case 0x0155: case 0x0156: case 0x0157: case 0x0158: case 0x0159: return 'r';
        case 0x015A: case 0x015B: case 0x015C: case 0x015D: case 0x015E: case 0x015F:
        case 0x0160: case 0x0161: case 0x017F: return 's';
        case 0x0162: case 0x0163: case 0x0164: case 0x0165: case 0x0166: case 0x0167: return 't';
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: case 0x00F9: case 0x00FA:
        case 0x00FB: case 0x00FC: case 0x0168: case 0x0169: case 0x016A: case 0x016B:
        case 0x016C: case 0x016D: case 0x016E: case 0x016F: case 0x0170: case 0x0171:
        case 0x0172: case 0x0173: return 'u';
        case 0x0174: case 0x0175: return 'w';
        case 0x00DD: case 0x00FD: case 0x0176: case 0x0177: case 0x00FF: case 0x0178: return 'y';
        case 0x0179: case 0x017A: case 0x017B: case 0x017C: case 0x017D: case 0x017E: return 'z';
        default: return 0;
    }
}

std::string lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        Cp c = decode(s, i);
        unsigned int cp = c.code;
        if (cp < 0x80) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(cp)));
        } else if (cp >= 0x410 && cp <= 0x42F) {  // А-Я
            append_utf8(out, cp + 0x20);
        } else if (cp == 0x401) {  // Ё
            append_utf8(out, 0x451);
        } else if (cp == 0x406) {
            append_utf8(out, 0x456);
        } else if (cp == 0x407) {
            append_utf8(out, 0x457);
        } else if (cp == 0x404) {
            append_utf8(out, 0x454);
        } else if (cp == 0x490) {
            append_utf8(out, 0x491);
        } else {
            append_utf8(out, cp);
        }
        i += c.len;
    }
    return out;
}

// ---------- normalization ----------

} // namespace

std::string normalize_city(const std::string& s) {
    std::string lowered = lower_utf8(s);
    std::string out;
    out.reserve(lowered.size());
    for (size_t i = 0; i < lowered.size();) {
        Cp c = decode(lowered, i);
        unsigned int cp = c.code;
        i += c.len;

        if (cp == 0x451 || cp == 0x401) {  // ё / Ё
            append_utf8(out, 0x435);       // е
            continue;
        }
        if (cp < 0x80) {
            char ch = static_cast<char>(cp);
            if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) out += ch;
            continue;
        }
        if (is_cyr_letter(cp)) {
            append_utf8(out, cp);
            continue;
        }
        if (latin_base(cp) != 0) {
            out += static_cast<char>(latin_base(cp));
            continue;
        }
        // anything else (space, -, ', punctuation, other scripts) dropped
    }
    return out;
}

std::string transliterate(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        Cp c = decode(s, i);
        unsigned int cp = c.code;
        i += c.len;
        if (cp < 0x80) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(cp)));
            continue;
        }
        // uppercase -> lowercase
        if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
        if (cp == 0x401) cp = 0x451;
        if (cp == 0x406) cp = 0x456;
        if (cp == 0x407) cp = 0x457;
        if (cp == 0x404) cp = 0x454;
        if (cp == 0x490) cp = 0x491;
        const char* t = nullptr;
        switch (cp) {
            case 0x430: t = "a"; break;  case 0x431: t = "b"; break;
            case 0x432: t = "v"; break;  case 0x433: t = "g"; break;
            case 0x434: t = "d"; break;  case 0x435: t = "e"; break;
            case 0x436: t = "zh"; break; case 0x437: t = "z"; break;
            case 0x438: t = "i"; break;  case 0x439: t = "i"; break;
            case 0x43A: t = "k"; break;  case 0x43B: t = "l"; break;
            case 0x43C: t = "m"; break;  case 0x43D: t = "n"; break;
            case 0x43E: t = "o"; break;  case 0x43F: t = "p"; break;
            case 0x440: t = "r"; break;  case 0x441: t = "s"; break;
            case 0x442: t = "t"; break;  case 0x443: t = "u"; break;
            case 0x444: t = "f"; break;  case 0x445: t = "h"; break;
            case 0x446: t = "ts"; break; case 0x447: t = "ch"; break;
            case 0x448: t = "sh"; break; case 0x449: t = "shch"; break;
            case 0x44A: t = ""; break;   case 0x44B: t = "y"; break;
            case 0x44C: t = ""; break;   case 0x44D: t = "e"; break;
            case 0x44E: t = "yu"; break; case 0x44F: t = "ya"; break;
            case 0x451: t = "e"; break;
            case 0x456: t = "i"; break;  case 0x457: t = "i"; break;
            case 0x454: t = "e"; break;  case 0x491: t = "g"; break;
            default: break;
        }
        if (t) {
            out += t;
        } else {
            append_utf8(out, cp);
        }
    }
    return out;
}

namespace {

// ---------- database ----------

struct Entry {
    CityInfo info;
    std::vector<std::string> nk;  // normalized keys (script preserving)
    std::vector<std::string> nt;  // normalized romanized keys (Latin)
};

struct Db {
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::vector<int>> index;  // variant -> entry ids
};

const std::vector<CityInfo>& builtin_cities() {
    static const std::vector<CityInfo> list = {
        {"Moscow", 55.7558, 37.6176, 3.0, "Россия", 10381222, {"москва", "moscow", "мск", "moskva"}},
        {"Санкт-Петербург", 59.9343, 30.3351, 3.0, "Россия", 5351935, {"санкт-петербург", "питер", "спб", "saint petersburg", "st petersburg", "leningrad"}},
        {"Новосибирск", 55.0084, 82.9357, 7.0, "Россия", 1612833, {"новосибирск", "novosibirsk", "nsk"}},
        {"Екатеринбург", 56.8389, 60.6057, 5.0, "Россия", 1495066, {"екатеринбург", "yekaterinburg", "ekb"}},
        {"Казань", 55.8304, 49.0661, 3.0, "Россия", 1259000, {"казань", "kazan"}},
        {"Нижний Новгород", 56.3287, 44.002, 3.0, "Россия", 1259013, {"нижний новгород", "nizhny novgorod"}},
        {"Киев", 50.4501, 30.5234, 2.0, "Украина", 2952301, {"киев", "kyiv", "kiev"}},
        {"Минск", 53.9045, 27.5615, 3.0, "Беларусь", 2009786, {"минск", "minsk"}},
        {"Лондон", 51.5074, -0.1278, 0.0, "Великобритания", 8961989, {"лондон", "london"}},
        {"Нью-Йорк", 40.7128, -74.0060, -5.0, "США", 8804190, {"нью-йорк", "new york", "nyc"}},
        {"Париж", 48.8566, 2.3522, 1.0, "Франция", 2138551, {"париж", "paris"}},
        {"Берлин", 52.52, 13.405, 1.0, "Германия", 3645000, {"берлин", "berlin"}},
        {"Рим", 41.9028, 12.4964, 1.0, "Италия", 2318895, {"рим", "rome", "roma"}},
        {"Стамбул", 41.0082, 28.9784, 3.0, "Турция", 15701602, {"стамбул", "istanbul"}},
        {"Мумбаи", 19.076, 72.8777, 5.5, "Индия", 12442373, {"мумбаи", "mumbai", "bombay"}},
        {"Дели", 28.6139, 77.209, 5.5, "Индия", 10927986, {"дели", "delhi"}},
        {"Пекин", 39.9042, 116.4074, 8.0, "Китай", 18960744, {"пекин", "beijing"}},
        {"Шанхай", 31.2304, 121.4737, 8.0, "Китай", 24874500, {"шанхай", "shanghai"}},
        {"Токио", 35.6762, 139.6503, 9.0, "Япония", 13929286, {"токио", "tokyo"}},
        {"Сеул", 37.5665, 126.978, 9.0, "Южная Корея", 10349312, {"сеул", "seoul"}},
        {"Мехико", 19.4326, -99.1332, -6.0, "Мексика", 12294193, {"мехико", "mexico city", "ciudad de mexico"}},
        {"Дубай", 25.2048, 55.2708, 4.0, "ОАЭ", 3478300, {"дубай", "dubai"}},
        {"Сидней", -33.8688, 151.2093, 11.0, "Австралия", 5312163, {"сидней", "sydney"}},
    };
    return list;
}

std::string find_data_file(const std::string& name) {
    if (name.empty()) return "";
    namespace fs = std::filesystem;
    std::error_code ec;

    if (const char* env = std::getenv("JYOTISH_CITIES")) {
        if (fs::exists(env, ec)) return env;
    }
    if (fs::path(name).is_absolute()) {
        if (fs::exists(name, ec)) return name;
        return "";
    }
    std::vector<fs::path> candidates;
    candidates.emplace_back(name);
    candidates.emplace_back(fs::path("data") / name);
#if defined(__linux__)
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fs::path exe = fs::path(buf).parent_path();
        candidates.push_back(exe / name);
        candidates.push_back(exe / "data" / name);
        candidates.push_back(exe.parent_path() / "data" / name);
    }
#elif defined(_WIN32)
    char buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH)) {
        fs::path exe = fs::path(buf).parent_path();
        candidates.push_back(exe / name);
        candidates.push_back(exe / "data" / name);
    }
#endif
    for (const auto& c : candidates) {
        if (fs::exists(c, ec)) return c.string();
    }
    return "";
}

void add_variant(Db& db, int id, const std::string& raw) {
    std::string v = normalize_city(raw);
    if (v.size() >= 3) {
        auto& e = db.entries[id];
        if (std::find(e.nk.begin(), e.nk.end(), v) == e.nk.end()) {
            e.nk.push_back(v);
            db.index[v].push_back(id);
        }
    }
    std::string t = normalize_city(transliterate(raw));
    if (t.size() >= 3) {
        auto& e = db.entries[id];
        if (std::find(e.nt.begin(), e.nt.end(), t) == e.nt.end()) {
            e.nt.push_back(t);
            db.index[t].push_back(id);
        }
    }
}

void add_entry(Db& db, const CityInfo& info) {
    int id = static_cast<int>(db.entries.size());
    db.entries.push_back({info, {}, {}});
    add_variant(db, id, info.name);
    for (const auto& k : info.keys) add_variant(db, id, k);
}

void load_tsv(Db& db, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        // name \t subject \t cc \t lat \t lon \t tz \t pop \t keys
        std::vector<std::string> f;
        f.reserve(8);
        size_t start = 0;
        while (true) {
            size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                f.push_back(line.substr(start));
                break;
            }
            f.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (f.size() < 8) continue;
        CityInfo c;
        c.name = f[0];
        c.country = f[1];
        try {
            c.latitude = std::stod(f[3]);
            c.longitude = std::stod(f[4]);
            c.tz_offset = std::stod(f[5]);
            c.population = std::stoll(f[6]);
        } catch (...) {
            continue;
        }
        start = 0;
        while (true) {
            size_t bar = f[7].find('|', start);
            if (bar == std::string::npos) {
                if (start < f[7].size()) c.keys.push_back(f[7].substr(start));
                break;
            }
            c.keys.push_back(f[7].substr(start, bar - start));
            start = bar + 1;
        }
        add_entry(db, c);
    }
}

const Db& database() {
    static const Db db = [] {
        Db d;
        std::string path = find_data_file(jyotish::settings().cities_file);
        if (!path.empty()) load_tsv(d, path);
        if (d.entries.empty()) {
            for (const auto& c : builtin_cities()) add_entry(d, c);
        }
        return d;
    }();
    return db;
}

int lev_bounded(const std::string& a, const std::string& b, int maxd) {
    int la = static_cast<int>(a.size()), lb = static_cast<int>(b.size());
    if (std::abs(la - lb) > maxd) return maxd + 1;
    std::vector<int> prev(lb + 1), cur(lb + 1);
    for (int j = 0; j <= lb; ++j) prev[j] = j;
    for (int i = 1; i <= la; ++i) {
        cur[0] = i;
        int rowmin = cur[0];
        for (int j = 1; j <= lb; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            rowmin = std::min(rowmin, cur[j]);
        }
        if (rowmin > maxd) return maxd + 1;
        std::swap(prev, cur);
    }
    return prev[lb];
}

int score_structural(const std::string& q, const std::string& kv) {
    if (kv == q) return 1000;
    size_t ql = q.size(), kl = kv.size();
    if (ql >= 3 && kl >= ql && kv.compare(0, ql, q) == 0)
        return 800 - static_cast<int>(std::min<size_t>(120, kl - ql));
    if (ql >= 4 && kv.find(q) != std::string::npos) return 600;
    return 0;
}

bool ascii_only(const std::string& s) {
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}

int score_fuzzy(const std::string& q, const std::string& kv) {
    if (!ascii_only(q) || !ascii_only(kv)) return 0;
    size_t ql = q.size(), kl = kv.size();
    if (ql < 6 || ql > 24 || kl < 5 || kl > 24) return 0;
    size_t md = std::max(ql, kl);
    int thr = md <= 4 ? 1 : (md <= 7 ? 2 : 3);
    int d = lev_bounded(q, kv, thr);
    if (d <= thr) return 520 - d * 70;
    return 0;
}

std::string trim_chars(const std::string& s, int n) {
    size_t end = s.size();
    while (n-- > 0 && end > 0) {
        do {
            --end;
        } while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80);
    }
    return s.substr(0, end);
}

} // namespace

std::vector<CityMatch> search(const std::string& query, int limit) {
    std::vector<CityMatch> out;
    const Db& db = database();
    std::string qn = normalize_city(query);
    if (qn.size() < 2) return out;
    std::string qt = normalize_city(transliterate(query));

    std::unordered_map<int, int> best;
    auto add = [&](int i, int s) {
        auto it = best.find(i);
        if (it == best.end() || it->second < s) best[i] = s;
    };

    for (const std::string& q : {qn, qt}) {
        if (q.size() < 2) continue;
        auto it = db.index.find(q);
        if (it != db.index.end())
            for (int i : it->second) add(i, 1000);
    }

    for (size_t i = 0; i < db.entries.size(); ++i) {
        const Entry& e = db.entries[i];
        int s = 0;
        for (const auto& kv : e.nk) {
            int sc = score_structural(qn, kv);
            if (sc > s) s = sc;
            if (s >= 800) break;
        }
        if (s < 800)
            for (const auto& kv : e.nt) {
                int sc = score_structural(qt, kv);
                if (sc > s) s = sc;
                if (s >= 800) break;
            }
        if (s < 800)
            for (const auto& kv : e.nt) {
                int sc = score_fuzzy(qt, kv);
                if (sc > s) s = sc;
            }
        if (s >= 300) add(static_cast<int>(i), s);
    }

    out.reserve(best.size());
    for (const auto& [i, s] : best) out.push_back({db.entries[i].info, s});
    std::sort(out.begin(), out.end(), [](const CityMatch& a, const CityMatch& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.city.population > b.city.population;
    });
    if (limit > 0 && static_cast<int>(out.size()) > limit) out.resize(limit);
    return out;
}

std::vector<CityInfo> find_cities(const std::string& query, int limit) {
    std::vector<CityInfo> res;
    for (auto& m : search(query, limit)) res.push_back(m.city);
    return res;
}

std::optional<CityInfo> resolve_exact(const std::string& name) {
    const Db& db = database();
    std::string qn = normalize_city(name);
    if (qn.size() < 3) return std::nullopt;
    std::string qt = normalize_city(transliterate(name));

    const CityInfo* best = nullptr;
    auto consider = [&](const std::string& q) {
        auto it = db.index.find(q);
        if (it == db.index.end()) return;
        for (int i : it->second) {
            const CityInfo& c = db.entries[i].info;
            if (!best || c.population > best->population) best = &c;
        }
    };
    consider(qn);
    if (qt != qn) consider(qt);
    if (best) return *best;
    return std::nullopt;
}

std::optional<CityInfo> resolve_loose(const std::string& name) {
    const Db& db = database();
    std::string n = normalize_city(name);
    if (n.size() < 3) return std::nullopt;

    std::optional<CityInfo> best;
    auto consider = [&](const std::string& cand) {
        auto it = db.index.find(cand);
        if (it == db.index.end()) return;
        for (int i : it->second) {
            const CityInfo& c = db.entries[i].info;
            if (!best || c.population > best->population) best = c;
        }
    };
    if (auto exact = resolve_exact(name)) best = *exact;

    static const char* endings[] = {"", "а", "я", "ь", "й", "о", "ы", "и", "е", "ё", "ей", "ий", "ии"};
    for (int trim = 1; trim <= 2; ++trim) {
        std::string stem = trim_chars(n, trim);
        if (stem.size() < 6) continue;
        for (const char* e : endings) consider(stem + e);
    }
    return best;
}

std::optional<CityInfo> resolve_city(const std::string& name) {
    auto res = search(name, 1);
    if (!res.empty() && res.front().score >= 300) return res.front().city;
    return std::nullopt;
}

std::vector<CityInfo> get_all_cities() {
    std::vector<CityInfo> res;
    const Db& db = database();
    res.reserve(db.entries.size());
    for (const auto& e : db.entries) res.push_back(e.info);
    return res;
}

} // namespace jyotish::geocode
