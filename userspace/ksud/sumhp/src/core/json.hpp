#pragma once

#include "../../../../common/json.hpp"

namespace sumhp {
using JsonValue = json::Value;
inline bool parse_json(const std::string& input, JsonValue& out, std::string& error) {
    return json::parse_checked(input, out, error);
}
inline std::string stringify_json(const JsonValue& value, int indent = 0) {
    return json::dump(value, indent > 0 ? indent : -1);
}
inline std::string json_quote(const std::string& value) {
    return json::escape_string(value);
}
}  // namespace sumhp
