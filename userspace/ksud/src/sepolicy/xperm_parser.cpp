#include "xperm_parser.hpp"

#include <algorithm>
#include <cctype>

namespace ksud {

namespace {

bool is_xperm_char(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_' || value == '-';
}

std::string_view trim_space(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))
        input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))
        input.remove_suffix(1);
    return input;
}

}  // namespace

std::optional<std::vector<std::string>> parse_xperm_set(std::string_view input) {
    input = trim_space(input);
    if (input.empty())
        return std::nullopt;

    if (input.front() != '{') {
        if (!std::all_of(input.begin(), input.end(), is_xperm_char))
            return std::nullopt;
        return std::vector<std::string>{std::string(input)};
    }

    if (input.size() < 2 || input.back() != '}')
        return std::nullopt;
    input = input.substr(1, input.size() - 2);

    std::vector<std::string> values;
    while (true) {
        input = trim_space(input);
        if (input.empty())
            break;

        size_t length = 0;
        while (length < input.size() && is_xperm_char(input[length]))
            ++length;
        if (length == 0)
            return std::nullopt;
        values.emplace_back(input.substr(0, length));
        input.remove_prefix(length);
        if (!input.empty() && !std::isspace(static_cast<unsigned char>(input.front())))
            return std::nullopt;
    }

    if (values.empty())
        return std::nullopt;
    return values;
}

}  // namespace ksud
