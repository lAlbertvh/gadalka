#pragma once

#include <string>
#include <vector>

namespace jyotish::wisdom {

// Theme of a wisdom item. THEMES maps life area -> default theme.
enum class Kind { Gita, Parable, Story, Song };

struct Item {
    Kind kind;
    std::string theme;
    std::string ru;
    std::string en;
};

inline std::string kind_name(Kind k) {
    switch (k) {
        case Kind::Gita: return "quote";
        case Kind::Parable: return "parable";
        case Kind::Story: return "story";
        case Kind::Song: return "song";
    }
    return "quote";
}

// Vedic tradition wisdom: Gita verses, Upanishad parables, devotion stories and songs.
const std::vector<Item>& items();

// Map a life area (general/career/love/health/family/spirituality) to a theme.
std::string theme_for_area(const std::string& area);

// Build the "Inspiration from the Vedic tradition" block injected into prompts.
// Deterministic per (areas, seed, lang): picks one item of each Kind.
std::string build_wisdom_block(const std::vector<std::string>& areas,
                               const std::string& seed,
                               const std::string& lang = "ru");

} // namespace jyotish::wisdom