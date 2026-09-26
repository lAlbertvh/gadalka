#include <jyotish/utf8.hpp>

namespace jyotish::utf8 {

std::string make_valid(const std::string& s) {
    const std::string REPL = "\xEF\xBF\xBD";  // U+FFFD replacement char
    std::string out;
    out.reserve(s.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char b0 = p[i];
        if (b0 < 0x80) { out.push_back(static_cast<char>(b0)); ++i; continue; }

        size_t len = 0;
        if (b0 >= 0xC2 && b0 <= 0xDF) len = 2;            // 2-byte
        else if (b0 >= 0xE0 && b0 <= 0xEF) len = 3;       // 3-byte
        else if (b0 >= 0xF0 && b0 <= 0xF4) len = 4;       // 4-byte

        bool ok = len != 0 && i + len <= n;
        if (ok) {
            for (size_t k = 1; k < len; ++k) {
                const unsigned char c = p[i + k];
                if (c < 0x80 || c > 0xBF) { ok = false; break; }
            }
        }
        if (ok) {
            const unsigned char c1 = p[i + 1];
            if (b0 == 0xE0 && c1 < 0xA0) ok = false;      // overlong
            if (b0 == 0xED && c1 > 0x9F) ok = false;      // surrogates
            if (b0 == 0xF0 && c1 < 0x90) ok = false;      // overlong
            if (b0 == 0xF4 && c1 > 0x8F) ok = false;      // > U+10FFFF
        }
        if (ok) { out.append(s, i, len); i += len; }
        else    { out += REPL; ++i; }                     // drop the bad lead byte
    }
    return out;
}

} // namespace jyotish::utf8