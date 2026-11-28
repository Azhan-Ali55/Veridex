#ifndef JSON_UTIL_HPP
#define JSON_UTIL_HPP

#include "json.hpp"

#include <string>

namespace json_util {

// Return obj[key] if it is a JSON string, otherwise "".
//
// Missing keys, JSON null and non-string values all collapse to the empty
// string. This matters for the Federal Register corpus, where `abstract` is
// null on roughly a quarter of the records.
inline std::string stringField(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

} // namespace json_util

#endif // JSON_UTIL_HPP