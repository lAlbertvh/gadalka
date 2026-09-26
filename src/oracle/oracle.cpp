#include <jyotish/oracle.hpp>
#include <jyotish/predictions.hpp>
#include <jyotish/config.hpp>
#include <jyotish/prompt_templates.hpp>
#include <jyotish/quote_pool.hpp>
#include <jyotish/wisdom.hpp>
#include <jyotish/news.hpp>
#include <jyotish/parsing.hpp>
#include <jyotish/grounding.hpp>
#include <jyotish/famous.hpp>
#include <jyotish/research.hpp>
#include <jyotish/theory.hpp>
#include <jyotish/i18n.hpp>
#include <string>
#include <vector>
#include <regex>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace jyotish::oracle {

static bool has_cjk(const std::string& text) {
    // Decode UTF-8 and look for real CJK codepoints (CJK U+4E00-U+9FFF,
    // hiragana/katakana U+3040-U+30FF, CJK punct U+3000-U+303F, fullwidth U+FF00-U+FFEF,
    // Hangul syllables U+AC00-U+D7AF).
    // Em-dashes & Cyrillic are multi-byte but NOT CJK — those must not match.
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        if (b0 >= 0xE4 && b0 <= 0xE9) return true;          // CJK Unified Ideographs
        if (b0 == 0xE3 && b1 >= 0x80 && b1 <= 0x83) return true; // CJK punct/hiragana/katakana
        if ((b0 >= 0xEA && b0 <= 0xEC) || (b0 == 0xED && b1 <= 0x9F)) return true; // Hangul syllables
        if (b0 == 0xEF && (b1 == 0xBC || b1 == 0xBD)) return true; // fullwidth forms
    }
    return false;
}

static bool has_cyrillic(const std::string& text) {
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        if ((b0 == 0xD0 || b0 == 0xD1) && b1 >= 0x80 && b1 <= 0xBF) return true;
    }
    return false;
}

// A Russian answer sometimes carries an untranslated English clause in the
// middle of an otherwise correct reply ("…проявиться черезIncreased
// self-discipline and a greater focus on personal responsibility"). The whole
// text still contains Cyrillic, so the "no Cyrillic" retry never fires. Any
// contiguous span of Latin letters (words joined by spaces/hyphens/commas/
// periods/em-dashes) longer than `latin_min` inside a Russian answer is a
// language leak → the retry pass regenerates it. Cyrillic resets the span;
// CJK is already rejected by has_cjk().
bool has_english_leak(const std::string& text, size_t latin_min) {
    if (latin_min == 0) return false;
    size_t latin = 0;
    bool run = false;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { ++latin; run = true; continue; }
        if (!run) continue;
        if (c == ' ' || c == '-' || c == ',' || c == '.' || c == '\'') continue;  // connector
        if (c >= 0xD0 && c <= 0xD4) { latin = 0; run = false; continue; }         // Cyrillic breaks the run
        if (c >= 0x80) {                                                          // em-dash, curly quotes, "…"
            while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) ++i;
            continue;
        }
        latin = 0;
        run = false;
    }
    return latin >= latin_min;
}

static std::string trim_copy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Keep a reading inside the same budget as a horoscope (~1500 chars): never let
// the model rehash "War and Peace". The cut lands on the last sentence end
// ("."/"!"/"?"/newline/"…") at or before the byte budget and never splits a
// UTF-8 character.
std::string clip_reply(const std::string& reply, size_t budget) {
    if (budget == 0) return std::string();
    if (reply.size() <= budget) return reply;
    // Search back from the cap for the last clean ending; never cut before half
    // the budget so a reading keeps most of its content.
    const size_t floor = budget / 2;
    // Back the cap up to a UTF-8 leading byte boundary.
    size_t cut = budget;
    if (cut < reply.size()) {
        while (cut > 0 && (static_cast<unsigned char>(reply[cut]) & 0xC0) == 0x80) --cut;
    }
    size_t best = cut;
    for (size_t i = cut; i > floor; --i) {
        const unsigned char c = static_cast<unsigned char>(reply[i - 1]);
        if (c == '.' || c == '!' || c == '?' || c == '\n') {
            best = i;
            break;
        }
        if (i >= 3 && reply.compare(i - 3, 3, "\xE2\x80\xA6") == 0) {  // "…"
            best = i;
            break;
        }
    }
    return trim_copy(reply.substr(0, best));
}

// ---- name enforcement -----------------------------------------------------
// The oracle LLM sometimes addresses the user by a wrong made-up name. Since a
// confirmed name is authoritative, we replace other common names in the reply
// with the real one. Only nominative singular full-word matches are rewritten
// (an address form "Алексей, ..." -> "Альберт, ..."); an interesting-follower
// pattern "Александр Пушкин" (name + capitalized surname) is left untouched so
// analogies about famous people survive. Everyday words that happen to be names
// ("лев", "рак", "роза") are only rewritten when capitalized.
static const std::vector<std::string> common_names_ru = {
    // male
    "алексей", "александр", "дмитрий", "сергей", "андрей", "михаил", "иван",
    "владимир", "николай", "павел", "антон", "артём", "максим", "игорь", "олег",
    "виктор", "юрий", "евгений", "вадим", "роман", "тимур", "руслан", "денис",
    "кирилл", "глеб", "борис", "валерий", "константин", "степан", "григорий",
    "даниил", "данила", "пётр", "эдуард", "аркадий", "герман", "марк", "илья",
    "тарас", "егор", "матвей", "семён", "мирон", "арсений",
    // female
    "анна", "мария", "ольга", "елена", "наталья", "ирина", "светлана", "татьяна",
    "юлия", "екатерина", "александра", "виктория", "дарья", "алла", "валентина",
    "надежда", "людмила", "ксения", "полина", "вероника", "валерия", "диана",
    "марина", "софья", "софия", "эльвира", "оксана", "галина", "лариса", "карина"};
static const std::vector<std::string> capital_only_names_ru = {
    "лев", "рак", "роза", "лилия", "ирис", "майя"};

// ASCII + Cyrillic lowercase, byte-safe (UTF-8 trained as-is).
std::string lower_name(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        unsigned char b0 = static_cast<unsigned char>(out[i]);
        unsigned char b1 = static_cast<unsigned char>(out[i + 1]);
        if (b0 == 0xD0 && b1 >= 0x90 && b1 <= 0x9F) out[i + 1] = static_cast<char>(b1 + 0x20);     // А-П -> а-п
        else if (b0 == 0xD0 && b1 >= 0xA0 && b1 <= 0xAF) { out[i] = '\xD1'; out[i + 1] = static_cast<char>(b1 - 0x20); } // Р-Я -> р-я
        else if (b0 == 0xD0 && b1 == 0x81) { out[i] = '\xD1'; out[i + 1] = '\x91'; }
        else if (b0 >= 'A' && b0 <= 'Z') out[i] = static_cast<char>(b0 - 'A' + 'a');
    }
    return out;
}

static bool is_capital_ru(const std::string& s) {
    if (s.empty()) return false;
    unsigned char b0 = static_cast<unsigned char>(s[0]);
    unsigned char b1 = s.size() > 1 ? static_cast<unsigned char>(s[1]) : 0;
    return (b0 >= 'A' && b0 <= 'Z') ||
           (b0 == 0xD0 && b1 >= 0x90 && b1 <= 0xAF) ||
           (b0 == 0xD0 && b1 == 0x81);
}

static void uppercase_first_ru(std::string& s) {
    if (s.empty()) return;
    unsigned char b0 = static_cast<unsigned char>(s[0]);
    if (b0 >= 'a' && b0 <= 'z') { s[0] = static_cast<char>(b0 - 'a' + 'A'); return; }
    if (b0 == 0xD0 && s.size() > 1) {
        unsigned char b1 = static_cast<unsigned char>(s[1]);
        if (b1 >= 0xB0 && b1 <= 0xBF) s[1] = static_cast<char>(b1 - 0x20);   // а-п -> А-П
        else if (b1 == 0x91) { s[0] = '\xD0'; s[1] = '\x81'; }               // ё -> Ё
    }
    if (b0 == 0xD1 && s.size() > 1) {
        unsigned char b1 = static_cast<unsigned char>(s[1]);
        if (b1 >= 0x80 && b1 <= 0x8F) { s[0] = '\xD0'; s[1] = static_cast<char>(b1 + 0x20); } // р-я -> Р-Я
        else if (b1 == 0x91) { s[0] = '\xD0'; s[1] = '\x81'; }                // ё -> Ё
    }
}

// Word boundaries: preceding/following chars must not be letters.
static bool is_letter_utf8(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

std::string enforce_user_name(const std::string& reply, const std::string& real_name) {
    if (reply.empty() || real_name.empty()) return reply;
    std::string real_l = lower_name(real_name);
    std::string out = reply;
    bool ambiguous = std::find(capital_only_names_ru.begin(), capital_only_names_ru.end(), real_l) != capital_only_names_ru.end();
    (void)ambiguous;

    std::vector<std::pair<std::string, bool>> needles; // lowercase name, must_be_capital
    for (const auto& nm : common_names_ru) needles.push_back({nm, false});
    for (const auto& nm : capital_only_names_ru) needles.push_back({nm, true});
    std::sort(needles.begin(), needles.end(), [](const auto& a, const auto& b){ return a.first.size() > b.first.size(); });

    // Track spans already visited so a long name doesn't overlap with a shorter
    // suffix (e.g. "мария" inside "марианна"); we scan linearly by position.
    size_t i = 0;
    std::string word;
    size_t wstart = 0;
    auto flush = [&](size_t wend) {
        if (word.empty()) return;
        std::string wl = lower_name(word);
        for (const auto& nd : needles) {
            if (wl != nd.first) continue;
            if (nd.second && !is_capital_ru(word)) continue;   // "лев"/"рак" must be capitalized
            // skip if it is the user's own name
            if (wl == real_l) break;
            // look ahead: "Александр Пушкин" / "Иван Иванов" — capitalized word
            // right after the name means a surname, keep untouched.
            size_t a = wend;
            while (a < out.size() && (out[a]==' ' || out[a]=='\t' || out[a]=='\n' || out[a]=='\r')) ++a;
            if (a < out.size() && is_capital_ru(out.substr(a, 3))) continue;
            std::string repl = lower_name(real_name);
            if (is_capital_ru(word)) uppercase_first_ru(repl);
            out.replace(wstart, wend - wstart, repl);
            i = wstart + repl.size();
            return;
        }
    };
    // scan
    while (i < out.size()) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (is_letter_utf8(c)) { 
            if (word.empty()) wstart = i;
            word.push_back(out[i]);
            ++i;
        } else {
            flush(i);
            word.clear();
            ++i;
        }
    }
    flush(out.size());
    return out;
}

// The small local model occasionally emits a literal form placeholder instead
// of the user's name ("Меня зовут [Ваше имя]", "[Имя собеседника] ...").
// Such tokens never come from our templates — they are LLM hallucinations.
// Replace any bracketed token that mentions a "name"/"имя" with the confirmed
// name; if the name is unknown, drop the placeholder brackets entirely.
std::string substitute_name_placeholders(const std::string& s, const std::string& real_name) {
    if (s.empty()) return s;
    // A fragment lowercased with lower_name(): any Cyrillic/ASCII letters in it.
    auto mentions_name = [](const std::string& inner) {
        std::string low = lower_name(inner);
        return low.find("имя") != std::string::npos ||
               low.find("имени") != std::string::npos ||
               low.find("ваш") != std::string::npos ||
               low.find("name") != std::string::npos ||
               low.find("user") != std::string::npos;
    };
    std::string out = s;
    size_t i = 0;
    while (i < out.size()) {
        if (out[i] != '[') { ++i; continue; }
        size_t close = out.find(']', i);
        if (close == std::string::npos) break;
        std::string inner = out.substr(i + 1, close - i - 1);
        if (mentions_name(inner)) {
            if (!real_name.empty()) {
                out.replace(i, close - i + 1, real_name);
                i += real_name.size();
            } else {
                out.erase(i, close - i + 1);
            }
        } else {
            i = close + 1;
        }
    }
    // A stray "Меня зовут ..." whose name is missing gets the real name too.
    if (!real_name.empty()) {
        const std::string key = "меня зовут ";
        size_t pos = 0;
        while ((pos = out.find(key, pos)) != std::string::npos) {
            size_t name_start = pos + key.size();
            size_t name_end = out.find_first_of(".,!?:;\n", name_start);
            if (name_end == std::string::npos) name_end = out.size();
            std::string token = out.substr(name_start, name_end - name_start);
            // The token is either an empty bracket remnant or a placeholder-ish
            // word ("дальше", "тут", "и т.д.") — never fill over a real name.
            bool emptyish = token.empty() || token == "…" || token == "..." || token == "дальше" ||
                            token == "тут" || token == "далее" || token == "здесь";
            if (emptyish) {
                out.erase(name_start, name_end - name_start);
                out.insert(name_start, real_name);
            }
            pos = name_end;
        }
    }
    return out;
}

// The critic signals "nothing to fix" with a bare OK marker (legacy fallback).
static bool is_ok_marker(const std::string& raw) {
    std::string t = trim_copy(raw);
    while (!t.empty() && (t.back() == '.' || t.back() == '!' || t.back() == '"' || t.back() == ')'))
        t.pop_back();
    t = trim_copy(t);
    std::string up;
    for (char c : t) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up == "OK" || up == "OKAY" || up == "ОК";
}

// Cut anything outside the outermost JSON braces (the critic may wrap it in
// prose or a ```json fence).
static std::string extract_json_object(const std::string& s) {
    auto start = s.find('{');
    auto end = s.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) return {};
    return s.substr(start, end - start + 1);
}

// The critic occasionally truncates the JSON mid-output (drops closing braces).
// Re-append closers for any unbalanced brackets, respecting string literals.
static void repair_json_braces(std::string& s) {
    std::vector<char> stack;
    bool in_str = false, esc = false;
    for (char ch : s) {
        if (esc) { esc = false; continue; }
        if (ch == '\\') { esc = true; continue; }
        if (ch == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (ch == '{') stack.push_back('{');
        else if (ch == '[') stack.push_back('[');
        else if (ch == '}') { for (size_t i = stack.size(); i > 0; --i) if (stack[i-1] == '{') { stack.pop_back(); break; } }
        else if (ch == ']') { for (size_t i = stack.size(); i > 0; --i) if (stack[i-1] == '[') { stack.pop_back(); break; } }
    }
    for (size_t i = stack.size(); i > 0; --i) {
        s += (stack[i - 1] == '{') ? '}' : ']';
    }
}

// Second LLM pass: focused on factual claims *the user made*. The critic
// returns structured JSON; each correction is grounded against Wikipedia
// before it is prepended to the reply. Nothing is ever free-form rewritten.
static std::string fact_check(const std::string& question, const std::string& draft, const std::string& lang) {
    if (!jyotish::settings().fact_check) return draft;
    if (draft.empty() || has_cjk(draft)) return draft;
    if (draft == ORACLE_CANNOT_RU || draft == ORACLE_CANNOT_EN) return draft;

    std::string system = lang == "ru" ? FACTCHECK_SYSTEM_RU : FACTCHECK_SYSTEM_EN;
    std::string prompt = lang == "ru"
        ? "СООБЩЕНИЕ ПОЛЬЗОВАТЕЛЯ:\n" + question
        : "USER MESSAGE:\n" + question;

    auto sys_msg = nlohmann::json{{"role", "system"}, {"content", system}};
    auto usr_msg = nlohmann::json{{"role", "user"}, {"content", prompt}};
    std::string out = trim_copy(predictions::ollama_chat({sys_msg, usr_msg},
        jyotish::settings().factcheck_temperature, jyotish::settings().factcheck_model));

    if (out.empty() || has_cjk(out)) return draft;
    if (out.find("Ollama error") != std::string::npos) return draft;

    // JSON path (current). Any non-JSON response -> fail safe: no correction.
    nlohmann::json parsed;
    std::string candidate = extract_json_object(out);
    bool repaired = false;
    try {
        parsed = nlohmann::json::parse(candidate);
    } catch (...) {
        repair_json_braces(candidate);
        try {
            parsed = nlohmann::json::parse(candidate);
            repaired = true;
        } catch (...) {
            if (is_ok_marker(out)) return draft;
            return draft;  // unrecognizable critic output: never trust it
        }
    }
    (void)repaired;
    if (!parsed.is_object()) return draft;
    auto status = parsed.value("status", "ok");
    if (status != "fix" || !parsed.contains("claims")) return draft;

    std::string wiki_lang = lang == "en" ? "en" : "ru";
    std::vector<std::string> notes;
    for (const auto& c : parsed["claims"]) {
        if (!c.is_object()) continue;
        std::string subject = c.value("subject", "");
        std::string correct = c.value("correct", "");
        std::string note = c.value("note", "");
        if (subject.empty() || correct.empty() || note.empty()) continue;
        if (note.size() > 300) continue;
        // Hard reliability gate: the correction must be verifiable on Wikipedia,
        // otherwise it is dropped (a small model can invent "facts").
        bool ok_lang = (lang == "ru" && has_cyrillic(note)) || (lang != "ru" && !has_cyrillic(note));
        if (!ok_lang) continue;
        if (!jyotish::wikipedia_grounds(subject, correct, wiki_lang)) continue;
        notes.push_back(note);
    }
    if (notes.empty()) return draft;

    std::string prefix;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (i) prefix += " ";
        prefix += notes[i];
    }
    return prefix + "\n\n" + draft;
}

// Uppercase the first Cyrillic/ASCII letter of a word (а-я, ё -> А-Я, Ё).
static void capitalize_first_ru(std::string& t) {
    if (t.empty()) return;
    unsigned char c0 = static_cast<unsigned char>(t[0]);
    if (c0 >= 'a' && c0 <= 'z') { t[0] = static_cast<char>(c0 - 'a' + 'A'); return; }
    if (c0 == 0xD0 && t.size() > 1) {
        unsigned char c1 = static_cast<unsigned char>(t[1]);
        if (c1 >= 0xB0 && c1 <= 0xBF) t[1] = static_cast<char>(c1 - 0x20);        // а-п -> А-П
        else if (c1 >= 0xE0 && c1 <= 0xEF) t[1] = static_cast<char>(c1 - 0x50);   // р-я -> Р-Я
        else if (c1 == 0x91) { t[0] = '\xD0'; t[1] = '\x81'; }                    // ё -> Ё
    }
}

// The small local generation model (qwen2) sometimes botches Russian verb
// conjugation ("ответю" instead of "отвечу"). Patch common wrong 1st-person
// forms / future-tense forms with their correct equivalents (word-boundary
// only). Pairs are lowercase; the replacement inherits the original casing.
static void fix_common_ru_errors(std::string& s) {
    if (s.empty()) return;
    static const std::vector<std::pair<std::string, std::string>> fixes = {
        {"ответю", "отвечу"},       // чередование -т-/-ч- в 1-м лице
        {"отвечю", "отвечу"},
        {"спросию", "спрошу"},
        {"попросию", "попрошу"},
        {"простию", "прощу"},
        {"спешию", "спешу"},
        {"решию", "решу"},
        {"бросию", "брошу"},
        {"заметю", "замечу"},
        {"встретю", "встречу"},
        {"полетю", "полечу"},
        {"взлетю", "взлечу"},
        {"ходию", "хожу"},
        {"водию", "вожу"},
        {"носию", "ношу"},
        {"ездию", "езжу"},
        {"сидею", "сижу"},
        {"глядею", "гляжу"},
        {"увидею", "увижу"},
        {"выглядею", "выгляжу"},
        {"ненавидею", "ненавижу"},
        {"зависю", "завишу"},
        {"смотрию", "смотрю"},
        {"писаю", "пишу"},
        {"искаю", "ищу"},
        {"сказаю", "скажу"},
        {"показаю", "покажу"},
        {"указаю", "укажу"},
        {"подсказаю", "подскажу"},
        {"наказаю", "накажу"},
        {"отказаю", "откажу"},
        {"приказаю", "прикажу"},
        {"расскажю", "расскажу"},
        {"положю", "положу"},
        {"предложю", "предложу"},
        {"отложю", "отложу"},
        {"доложю", "доложу"},
        {"приложю", "приложу"},
        {"победю", "побежу"},
        {"удержю", "удержу"},
        {"побегю", "побегу"},
        {"бегю", "бегу"},
        {"готовю", "готовлю"},
        {"любю", "люблю"},
        {"ловю", "ловлю"},
        {"ставю", "ставлю"},
        {"купю", "куплю"},
        {"спю", "сплю"},
        {"терпю", "терплю"},
        {"помогю", "помогу"},
        {"могю", "могу"},
        {"смогю", "смогу"},
        {"примю", "приму"},
        {"поймю", "пойму"},
        {"обнему", "обниму"},
        {"снимю", "сниму"},
        {"заимю", "займу"},
        {"наидю", "найду"},
        {"закрю", "закрою"},
        {"открю", "открою"},
        // типовые ошибки слов (не глаголы)
        {"будующее", "будущее"},
        {"следущий", "следующий"},
    };
    for (const auto& [bad_low, good_low] : fixes) {
        for (int case_variant = 0; case_variant < 2; ++case_variant) {
            std::string bad = bad_low;
            if (case_variant == 1) capitalize_first_ru(bad);
            size_t pos = 0;
            while ((pos = s.find(bad, pos)) != std::string::npos) {
                size_t end = pos + bad.size();
                bool left_ok = pos == 0 || !is_letter_utf8(static_cast<unsigned char>(s[pos - 1]));
                bool right_ok = end >= s.size() || !is_letter_utf8(static_cast<unsigned char>(s[end]));
                if (left_ok && right_ok) {
                    std::string repl = good_low;
                    if (case_variant == 1) capitalize_first_ru(repl);
                    s.replace(pos, bad.size(), repl);
                    pos += repl.size();
                } else {
                    pos += bad.size();
                }
            }
        }
    }
}

std::vector<std::string> split_history(const std::string& raw) {
    std::vector<std::string> result;
    try {
        auto j = nlohmann::json::parse(raw);
        if (j.is_array()) {
            for (const auto& m : j) {
                if (m.is_object() && m.contains("role") && m.contains("content")) {
                    result.push_back(m["role"].get<std::string>() + ": " + m["content"].get<std::string>());
                }
            }
        }
    } catch (...) {}
    return result;
}

std::string casual_chat(const std::string& question, const std::string& lang) {
    if (auto canned = casual_answer(question, lang)) return *canned;
    
    std::string system = lang == "ru" ? CASUAL_SYSTEM_RU : CASUAL_SYSTEM_EN;
    auto sys_msg = nlohmann::json{{"role", "system"}, {"content", system}};
    auto usr_msg = nlohmann::json{{"role", "user"}, {"content", question}};
    std::string reply = predictions::ollama_chat({sys_msg, usr_msg}, 0.6);
    
    const bool wrong_lang = has_cjk(reply) ||
        (lang == "ru" && !has_cyrillic(reply) && reply.length() > 8) ||
        (lang == "ru" && has_english_leak(reply)) ||
        (lang != "ru" && has_cyrillic(reply));
    if (wrong_lang) {
        sys_msg = nlohmann::json{{"role", "system"}, {"content", system + (lang == "ru" ? ORACLE_RU_RETRY : ORACLE_EN_RETRY)}};
        usr_msg = nlohmann::json{{"role", "user"}, {"content", question}};
        reply = predictions::ollama_chat({sys_msg, usr_msg}, 0.45);
    }
    if (has_cjk(reply) || reply.empty()) {
        return lang == "ru" ? ORACLE_CANNOT_RU : ORACLE_CANNOT_EN;
    }
    fix_common_ru_errors(reply);
    reply = fact_check(question, reply, lang);
    if (settings().max_reply_chars > 0 &&
        reply.size() > static_cast<size_t>(settings().max_reply_chars))
        reply = clip_reply(reply, static_cast<size_t>(settings().max_reply_chars));
    return reply;
}

std::tuple<std::string, OracleContext, nlohmann::json> oracle_chat(
    const Chart::BirthData& birth,
    const Chart& charter,
    const Chart& canonical,
    const std::vector<nlohmann::json>& history,
    const std::string& question,
    const std::string& photo_context,
    bool first_reply,
    const std::string& lang,
    const nlohmann::json& profile
) {
    auto info = build_context(birth, charter, canonical, question, photo_context, lang);
    std::string system = lang == "en" ? ORACLE_PROMPT_EN : ORACLE_PROMPT_RU;
    if (!profile.empty() && profile.contains("name") &&
        !profile["name"].is_null() && !profile["name"].get<std::string>().empty()) {
        const std::string uname = profile["name"].get<std::string>();
        system += lang == "ru"
            ? "\nСОБЕСЕДНИКА (пользователя) зовут " + uname + ". Обращайся к нему ТОЛЬКО по имени " + uname + ", никогда не используй другие имена и не меняй его."
            : "\nThe USER's name is " + uname + ". Always address the user ONLY by the name " + uname + ", never use or change it to any other name.";
    }
    system += lang == "en" ? RISK_BLOCK_EN : RISK_BLOCK_RU;

    // The birth time passed to you is LOCAL time of the birth place. Do NOT
    // convert it to Moscow/UTC/any other timezone, do not add/subtract hours,
    // do not comment on timezone differences ("это N часов по московскому").
    system += lang == "ru"
        ? "\nВРЕМЯ РОЖДЕНИЯ, которое тебе дано, — ЛОКАЛЬНОЕ время места рождения. НИКОГДА не пересчитывай его в московское/UTC/другое время, не прибавляй и не вычитай часы, не упоминай разницу часовых поясов. Принимай его как есть."
        : "\nThe birth time you are given is the LOCAL time of the birth place. NEVER convert it to Moscow/UTC/any other timezone, do not add or subtract hours, do not mention timezone differences. Take it as is.";
    
    // Fresh news (best effort, cached). Only questions about a quote/source/meaning
    // ("чья это цитата?", "что значит …?") don't need it — elsewhere (current events,
    // elections, "кто победил …?") the news is the grounding facts the model must
    // answer FROM instead of guessing.
    bool reference_question = false;
    {
        std::string lq = lower_name(question);
        if (lq.find(" цитат") != std::string::npos || lq.find("quote") != std::string::npos ||
            lq.find("что значит") != std::string::npos || lq.find("что означает") != std::string::npos ||
            lq.find("чья") != std::string::npos || lq.find("чей ") != std::string::npos ||
            lq.find("чья это") != std::string::npos || lq.find("написа") != std::string::npos ||
            lq.find("сказа") != std::string::npos || lq.find("смысл") != std::string::npos ||
            lq.find("meaning") != std::string::npos || lq.find("whose") != std::string::npos) {
            reference_question = true;
        }
    }
    if (!reference_question) {
        auto news_items = jyotish::news::load_news(jyotish::settings().news_cache_file, 12);
        std::string news_str = jyotish::news::news_block(news_items, lang);
        if (!news_str.empty()) {
            info.context += "\n\n" + news_str;
        }
    }
    
    // Vedic wisdom block driven by detected situations
    if (!info.situations.empty() && !reference_question) {
        std::string seed = question + birth.birth_date;
        info.context += "\n\n" + jyotish::wisdom::build_wisdom_block(info.situations, seed, lang);
    }

    // Live web research: for world/current-events questions (politics, economy,
    // markets, currency, conflicts) we search the web and Wikipedia, cross-check
    // >=2 independent domains, and only then inject the verified facts + let the
    // law-of-large-numbers analog engine run. Unverified questions never get the
    // analog narrative (it would be speculation).
    jyotish::research::Evidence research_ev;
    const bool research_enabled = jyotish::settings().web_search &&
                                  jyotish::settings().search_provider != "none";
    const bool research_was_run = !reference_question && research_enabled &&
                                  jyotish::research::world_question(question, lang);
    if (research_was_run) {
        research_ev = jyotish::research::research(question, lang, jyotish::settings());
        info.grounded = research_ev.grounded;
        if (research_ev.grounded && !research_ev.context_md.empty()) {
            info.context += "\n\n" + research_ev.context_md;
        } else {
            // Search ran but FAILED to ground (fewer than 2 independent
            // sources). The famous-analog narrative is skipped above, and we
            // must also tell the model out loud that there are no verified
            // data, so it does not quietly fall back to an unverified recall —
            // that would read as a confident hallucination. The
            // law-of-large-numbers narrative is exactly what gets blocked.
            info.context += lang == "ru"
                ? "\n\n[ВАЖНО: по этому вопросу НЕ удалось найти подтверждённых данных из независимых источников (меньше двух). НЕ выдавай конкретные цифры, даты и факты за проверенные. НО ты ОБЯЗАН провести настоящий анализ, а не отказаться: (1) перечисли факторы и причинно-следственные связи, (2) сравни, как похожие ситуации развивались в прошлом, — как паттерн/аналогию, явно пометив, что это рассуждение, а не проверенный факт, (3) дай прогноз в виде сценариев с диапазонами и условиями («если X, то…; если Y, то…»), (4) назови явные допущения и неопределённость. Плохой ответ — «это сложно и зависит от факторов» без анализа. Хороший ответ — честно помеченный аналитический разбор с вероятностным прогнозом.]"
                : "\n\n[IMPORTANT: no confirmed data could be found in independent sources (fewer than two). Do NOT present concrete numbers, dates or facts as verified. BUT you MUST still do real analysis instead of refusing: (1) list the factors and cause-effect links, (2) compare how similar situations evolved in the past as an analogy/pattern, clearly marked as reasoning, not a verified fact, (3) give a forecast as scenarios with ranges and conditions ('if X then…; if Y then…'), (4) state explicit assumptions and uncertainty. A bad answer is \"it is complex and depends on factors\" with no analysis. A good answer is an honestly-labelled analytical breakdown with a probabilistic forecast.]";
        }
    }

    // Law-of-large-numbers analog analysis over famous people — applied only
    // AFTER a corroborated search, or when no research was attempted (offline /
    // disabled). If research RAN but failed to corroborate (fewer than 2
    // independent sources), the analog narrative would be unverified
    // speculation, so it is skipped and the oracle says so explicitly instead.
    std::string famous_block;
    if (!reference_question && !(research_was_run && !research_ev.grounded)) {
        famous_block = jyotish::famous::analog_block(question, birth, canonical, lang, info.analogs);
    }
    if (!famous_block.empty()) {
        info.context += "\n\n" + famous_block;
    }

    // Analytical theory layer: curated frameworks (macroeconomics, psychology,
    // probability/math, philosophy) injected when the question touches those
    // domains. The block is explicitly labelled "NOT verified facts" so the
    // model applies theory as a reasoning frame but never presents it as
    // grounded data — grounding honesty is preserved.
    {
        std::string theory_str = jyotish::theory::theory_block(question, lang);
        if (!theory_str.empty()) {
            info.context += "\n\n" + theory_str;
        }
    }
    
    // Add profile info
    if (!profile.empty()) {
        std::vector<std::string> facts;
        if (profile.contains("name") && !profile["name"].is_null())
            facts.push_back((lang == "ru" ? "Имя: " : "Name: ") + profile["name"].get<std::string>());
        if (profile.contains("gender") && !profile["gender"].is_null())
            facts.push_back((lang == "ru" ? "Пол: " : "Gender: ") + profile["gender"].get<std::string>());
        if (profile.contains("birth_date") && !profile["birth_date"].is_null())
            facts.push_back((lang == "ru" ? "Дата рождения: " : "Birth date: ") + profile["birth_date"].get<std::string>());
        if (profile.contains("birth_time") && !profile["birth_time"].is_null())
            facts.push_back((lang == "ru" ? "Время рождения: " : "Birth time: ") + profile["birth_time"].get<std::string>());
        if (profile.contains("birth_place") && !profile["birth_place"].is_null())
            facts.push_back((lang == "ru" ? "Место рождения: " : "Birth place: ") + profile["birth_place"].get<std::string>());
        if (profile.contains("topics") && profile["topics"].is_array() && !profile["topics"].empty()) {
            std::string joined;
            for (const auto& t : profile["topics"]) joined += t.get<std::string>() + ", ";
            if (!joined.empty()) joined.pop_back(), joined.pop_back();
            facts.push_back((lang == "ru" ? "Ранее спрашивал(а) о: " : "Previously asked about: ") + joined);
        }
        if (!facts.empty()) {
            std::string label = lang == "ru" ? "О ПОЛЬЗОВАТЕЛЕ (учти):" : "ABOUT THE PERSON:";
            info.context = label + "\n- " + join(facts, "\n- ") + "\n\n" + info.context;
        }
    }
    
    if (first_reply) {
        const auto& t = jyotish::get(lang);
        auto sit = t.sign.find(jyotish::sign_name(charter.ascendant.sign));
        std::string lagna_display = sit != t.sign.end() ? sit->second : jyotish::sign_name(charter.ascendant.sign);
        std::string lagna_rule = lang == "ru"
            ? "\nТвоя точно рассчитанная ЛАГНА — " + lagna_display
              + " " + std::to_string(static_cast<int>(charter.ascendant.degree)) + "°. Называй её ИМЕННО так, никогда не называй другой знак лагной."
            : "\nYour exactly computed LAGNA is " + lagna_display
              + " " + std::to_string(static_cast<int>(charter.ascendant.degree)) + "°. Always state it as this exact sign, never name any other sign as the lagna.";
        system += lang == "ru" ? 
            "\n\nЭТО ПЕРВЫЙ ОТВЕТ: дай КОРОТКОЕ знакомство — 2–3 абзаца: Лагна, Луна, 2–3 планеты, текущий период. Не разбирай дома. Заверши тепло, предложи фото или вопрос.\nНИКОГДА не называй себя именем собеседника и не начинай со «Меня зовут …». Не добавляй заголовок «Гороскоп на …». Сразу начинай с фактов: «Ваша лагна — …»."
            : "\n\nFIRST REPLY: brief intro — 2-3 paragraphs: Lagna, Moon, 2-3 planets, current period. No house analysis. End warmly, suggest photo or question.\nNEVER introduce yourself using the user's name and never start with \"My name is …\". Do not add a \"Horoscope for …\" heading. Start straight with the facts: \"Your lagna is …\".";
        system += lagna_rule;
    }
    
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"}, {"content", system}});
    
    // Add history (last 10 messages, dedup consecutive)
    std::vector<nlohmann::json> deduped;
    for (auto it = history.rbegin(); it != history.rend() && deduped.size() < 12; ++it) {
        if (it->contains("role") && it->contains("content") && 
            (it->at("role") == "user" || it->at("role") == "assistant") &&
            !it->at("content").get<std::string>().empty()) {
            if (deduped.empty() || deduped.back()["role"] != it->at("role") || deduped.back()["content"] != it->at("content")) {
                deduped.push_back({{"role", it->at("role")}, {"content", it->at("content")}});
            }
        }
    }
    std::reverse(deduped.begin(), deduped.end());
    for (const auto& m : deduped) messages.push_back(m);
    
    // Sample quotes and filter out ones already used in this dialogue — both in
    // the chat history and in the generated context (wisdom/news/analog blocks
    // already show "цитата на сегодняшний день" etc.), so the LLM never echoes
    // a quote the user has already seen in the same dialogue.
    auto pool = sample_quotes(lang, 28);
    std::unordered_set<std::string> used;
    std::vector<std::string> seen_sources;
    for (const auto& m : history)
        if (m.contains("content")) seen_sources.push_back(m.at("content").get<std::string>());
    seen_sources.push_back(info.context);
    for (const auto& q : (lang == "ru" ? QUOTES_RU : QUOTES_EN)) {
        for (const auto& c : seen_sources)
            if (c.find(q) != std::string::npos) { used.insert(q); break; }
    }
    std::vector<std::string> fresh;
    for (const auto& q : pool) if (!used.count(q)) fresh.push_back(q);
    if (fresh.empty()) fresh = lang == "ru" ? QUOTES_RU : QUOTES_EN;
    
    std::string quote_section;
    if (lang == "ru") {
        quote_section = reference_question
            ? "ВОПРОС ПОЛЬЗОВАТЕЛЯ:\n" + question + "\n\n"
              "ВАЖНО:\n"
              "- СНАЧАЛА прямо и конкретно ответь на сам вопрос, коротко и честно.\n"
              "- НЕ выдумывай автора цитаты, источник или факт, которого не знаешь; если не знаешь — так и скажи.\n"
              "- НЕ превращай ответ в астрологическое рассуждение, если вопрос был фактический.\n"
              "- Цитату-финал добавлять НЕ обязательно.\n"
            : "ВОПРОС ПОЛЬЗОВАТЕЛЯ:\n" + question + "\n\n"
              "ВАЖНО:\n"
              "- СНАЧАЛА прямо и конкретно ответь на сам вопрос, словами на том языке, на каком задан.\n"
              "- Если спрашивают факт («чья это цитата?», «что значит …?», «кто такой …?») — ответь на него коротко и честно. НЕ выдумывай автора цитаты и не приписывай источники, которых не знаешь; если не знаешь — так и скажи.\n"
              "- Не повторяй общие шаблоны и не переходи на пустые рассуждения, если вопрос был конкретный.\n"
              "- В конце МОЖНО добавить ОДНУ цитату из пула ниже, только если она действительно относится к вопросу. Не повторяй цитаты, уже звучавшие в диалоге.\n\n"
              "ПУЛ ЦИТАТ:\n";
    } else {
        quote_section = reference_question
            ? "USER QUESTION:\n" + question + "\n\n"
              "IMPORTANT:\n"
              "- FIRST directly and concretely answer the question itself, briefly and honestly.\n"
              "- Do NOT invent a quote's author, source or any fact you don't know; if you don't know, say so.\n"
              "- Do NOT turn the answer into astrological speculation when the question was factual.\n"
              "- A closing quote is NOT required.\n"
            : "USER QUESTION:\n" + question + "\n\n"
              "IMPORTANT:\n"
              "- FIRST directly and concretely answer the question itself, in the language it was asked.\n"
              "- If it asks for a fact, answer briefly and honestly. Do NOT invent an author or cite sources you don't know; if you don't know, say so.\n"
              "- Do not fall back to generic advice when the question was specific.\n"
              "- At the end you MAY add ONE quote from the pool below, only if it actually fits the question. Do not repeat quotes already used in the dialogue.\n\n"
              "QUOTE POOL:\n";
    }
    for (size_t i = 0; i < std::min<size_t>(fresh.size(), 24); ++i) {
        quote_section += "- \"" + fresh[i] + "\"\n";
    }
    
    messages.push_back({{"role", "user"}, {"content", info.context + "\n\n" + quote_section}});
    
std::string reply = predictions::ollama_chat(messages, jyotish::settings().chat_temperature);

    const bool wrong_language = has_cjk(reply) ||
        (lang == "ru" && !has_cyrillic(reply) && reply.length() > 8) ||
        (lang == "ru" && has_english_leak(reply)) ||
        (lang != "ru" && has_cyrillic(reply));
    if (wrong_language) {
        auto retry_messages = messages;
        retry_messages[0] = {{"role", "system"}, {"content", system + (lang == "ru" ? ORACLE_RU_RETRY : ORACLE_EN_RETRY)}};
        reply = predictions::ollama_chat(retry_messages, jyotish::settings().chat_temperature - 0.15);
    }

    // The model sometimes leaks prompt scaffolding into the answer
    // ("[ПУЛ ЦИТАТ: ...]", "[QUOTE POOL: ...]", "(с)" attribution). Strip it.
    {
        auto strip_bracketed = [](std::string& s) {
            const std::vector<std::string> markers = {"[ПУЛ ЦИТАТ", "[QUOTE POOL", "[ПУЛ", "[BПЕРЕД"};
            for (const auto& m : markers) {
                size_t pos = s.find(m);
                while (pos != std::string::npos) {
                    size_t end = s.find(']', pos);
                    if (end == std::string::npos) {
                        s.erase(pos);
                        break;
                    }
                    s.erase(pos, end - pos + 1);
                    pos = s.find(m, pos);
                }
            }
        };
        strip_bracketed(reply);
        // A bare " (с)" attribution dangling after a quote.
        size_t pos = reply.find("(с)");
        while (pos != std::string::npos) {
            size_t line_end = reply.find('\n', pos);
            reply.erase(pos, (line_end == std::string::npos ? 0 : line_end - pos) + (line_end == std::string::npos ? 0 : 1));
            if (line_end != std::string::npos) {
                pos = reply.find("(с)", pos);
            } else break;
        }
    }

    const bool still_wrong = has_cjk(reply) || reply.empty() ||
        (lang == "ru" && !has_cyrillic(reply) && reply.length() > 8) ||
        (lang == "ru" && has_english_leak(reply)) ||
        (lang != "ru" && has_cyrillic(reply));
    if (still_wrong) {
        reply = lang == "ru" ? ORACLE_CANNOT_RU : ORACLE_CANNOT_EN;
    } else {
        reply = fact_check(question, reply, lang);
    }

    // The LLM often contradicts the computed chart ("у вас Лагна Скорпион"
    // instead of the actual Taurus from the ephemeris). Two guarantees:
    //  1. On the first reply, rewrite any "Лагна <wrong>" phrase to the real one.
    //  2. When lagna is in question (or again on first reply), pin the exact
    //     computed value to the end so the answer is always correct.
    {
        const std::string sign = jyotish::sign_name(charter.ascendant.sign);
        const int deg = static_cast<int>(charter.ascendant.degree);
        // Sign name in the reply's own language — never the English one inside a
        // Russian answer (that is exactly how "Лагна Taurus" leaks into the text).
        const auto& t = jyotish::get(lang);
        auto sit = t.sign.find(sign);
        const std::string sign_loc = sit != t.sign.end() ? sit->second : sign;
        if (first_reply) {
            // The intro must name the Lagna correctly. The model often writes a
            // wrong sign after "Лагна" ("Лагна Скорпиона", "Лагна Pisces" ...).
            // Normalise every lagna phrase: whatever fragment follows
            // "Лагна "/"Lagna " up to punctuation else replace with the real sign.
            static const char* ru_signs[12][2] = {
                {"Овен", "Овна"}, {"Телец", "Тельца"}, {"Близнецы", "Близнецов"},
                {"Рак", "Рака"}, {"Лев", "Льва"}, {"Дева", "Девы"},
                {"Весы", "Весов"}, {"Скорпион", "Скорпиона"}, {"Стрелец", "Стрельца"},
                {"Козерог", "Козерога"}, {"Водолей", "Водолея"}, {"Рыбы", "Рыб"}
            };
            const std::string real_ru0 = std::string(ru_signs[static_cast<uint8_t>(charter.ascendant.sign)][0]);
            const std::string real_ru = lang == "ru" ? real_ru0 : sign_loc;
            for (const char* prefix_c : {"Лагна ", "Лагной ", "Lagna ", "лагуна ",
                                         "восходящий знак ", "Восходящий знак ", "восходящий ",
                                         "Восходящий ", "восходящей ", "Восходящей ",
                                         "восходящего ", "восходящему ", "восходящим ",
                                         "ascendant ", "Ascendant ", "rising sign ", "Rising sign "}) {
                const std::string prefix(prefix_c);
                size_t pos = 0;
                while ((pos = reply.find(prefix, pos)) != std::string::npos) {
                    size_t from = pos + prefix.size();
                    size_t end = reply.find_first_of("(),.;:!\n", from);
                    if (end == std::string::npos) end = reply.size();
                    std::string frag = reply.substr(from, end - from);
                    // Only the target-language sign name counts as correct; in a
                    // Russian answer "Лагна Taurus" must be normalised too.
                    bool already_ok = frag.find(real_ru) != std::string::npos;
                    if (!already_ok) {
                        std::string repl = prefix + real_ru;
                        reply.replace(pos, end - pos, repl);
                        pos += repl.size();
                    } else {
                        pos += prefix.size();
                    }
                }
            }
        }

        std::string q = lower_name(question);
        bool ask_lagna = q.find("лагн") != std::string::npos || q.find("восходящ") != std::string::npos ||
                         q.find("асценд") != std::string::npos || q.find("lagna") != std::string::npos;
        if (ask_lagna || first_reply) {
            const std::string note =
                lang == "ru"
                    ? "\n\n(Факт по эфемеридам: ваша лагна — " + sign_loc + ", " + std::to_string(deg) + "°.)"
                    : "\n\n(Ephemeris fact: your lagna is " + sign_loc + ", " + std::to_string(deg) + "°.)";
            reply += note;
        }
    }

    // Gender question ("я мальчик или девочка?", "какого я пола?") — answer
    // deterministically from the collected data instead of letting the LLM guess.
    std::string gender = !profile.empty() && profile.contains("gender") && !profile["gender"].is_null()
                             ? profile["gender"].get<std::string>() : std::string();
    if (!gender.empty()) {
        std::string q = lower_name(question);
        bool asks_gender = q.find("мальчик") != std::string::npos ||
                           q.find("девочк") != std::string::npos ||
                           q.find("какого я пола") != std::string::npos ||
                           q.find("какой я пол") != std::string::npos ||
                           q.find("мужчин") != std::string::npos ||
                           q.find("женщин") != std::string::npos ||
                           q.find("пол") != std::string::npos ||
                           q.find("boy") != std::string::npos || q.find("girl") != std::string::npos ||
                           q.find("gender") != std::string::npos;
        if (asks_gender) {
            const std::string note = (lang == "ru")
                ? "\n\n(Факт: по вашему имени и данным — вы " +
                      std::string(gender == "мужской" ? "мальчик" : "девочка") + ".)"
                : "\n\n(Fact: based on your name and data, you are a " +
                      std::string(gender == "female" ? "girl" : "boy") + ".)";
            if (reply.find(note) == std::string::npos) reply += note;
        }
    }

    // The LLM occasionally addresses the user by a wrong name it invented. The
    // confirmed name (if any) wins: replace other common name forms in address
    // position ("Алексей, ...") — and stray mentions — with the real name.
    std::string real = !profile.empty() && profile.contains("name") && !profile["name"].is_null()
                           ? profile["name"].get<std::string>() : std::string();
    if (!real.empty()) reply = enforce_user_name(reply, real);
    // The model sometimes emits a literal "[Ваше имя]"/"[Your Name]" placeholder
    // instead of addressing the user — substitute the confirmed name / drop it.
    reply = substitute_name_placeholders(reply, real);

    fix_common_ru_errors(reply);

    // Every substantive answer must end with exactly one VERBATIM pool quote.
    // Repair the tail before clipping so the budget cannot cut the quote.
    if (lang == "ru" || lang == "en")
        enforce_quote_finale(reply, lang);

    if (settings().max_reply_chars > 0 &&
        reply.size() > static_cast<size_t>(settings().max_reply_chars))
        reply = clip_reply(reply, static_cast<size_t>(settings().max_reply_chars));

    return {reply, info, messages};
}

} // namespace jyotish::oracle