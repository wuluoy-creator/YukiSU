#pragma once

#include <limits>
#include <string_view>

namespace ksud {

constexpr bool is_supported_kernel_version(unsigned major, unsigned minor) {
    return major > 6 || (major == 6 && minor >= 1);
}

constexpr bool is_supported_kmi(std::string_view kmi) {
    std::string_view::size_type cursor = 0;
    const auto parse_number = [&](unsigned& value) constexpr {
        const auto begin = cursor;
        value = 0;
        while (cursor < kmi.size() && kmi[cursor] >= '0' && kmi[cursor] <= '9') {
            const unsigned digit = static_cast<unsigned>(kmi[cursor] - '0');
            if (value > (std::numeric_limits<unsigned>::max() - digit) / 10)
                return false;
            value = value * 10 + digit;
            ++cursor;
        }
        return cursor != begin;
    };

    if (kmi.substr(0, 7) == "android") {
        cursor = 7;
        unsigned android_version = 0;
        if (!parse_number(android_version) || cursor == kmi.size() || kmi[cursor] != '-')
            return false;
        ++cursor;
    }

    unsigned major = 0;
    unsigned minor = 0;
    if (!parse_number(major) || cursor == kmi.size() || kmi[cursor] != '.')
        return false;
    ++cursor;
    return parse_number(minor) && cursor == kmi.size() && is_supported_kernel_version(major, minor);
}

}  // namespace ksud
