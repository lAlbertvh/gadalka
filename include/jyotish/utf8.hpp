#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <functional>

namespace jyotish::utf8 {

// Replace invalid UTF-8 sequences (truncated/overlong/out-of-range bytes that
// an LLM or a web snippet occasionally contains, e.g. a dangling 0xE2) with
// U+FFFD so the string always survives nlohmann::json serialization. Without
// it a bad byte would abort the whole JSON request/response with
// type_error.316 "invalid UTF-8 byte at index ...".
std::string make_valid(const std::string& s);

// Deeply sanitize every string inside a JSON document (objects, arrays and
// nested values) so any payload built from web snippets, LLM output or raw
// history can be dumped/sent safely.
inline void sanitize(nlohmann::json& v) {
    std::function<void(nlohmann::json&)> walk =
        [&walk](nlohmann::json& node) {
            if (node.is_string()) node = make_valid(node.get<std::string>());
            else if (node.is_object())
                for (auto it = node.begin(); it != node.end(); ++it) walk(it.value());
            else if (node.is_array())
                for (auto& e : node) walk(e);
        };
    walk(v);
}

} // namespace jyotish::utf8