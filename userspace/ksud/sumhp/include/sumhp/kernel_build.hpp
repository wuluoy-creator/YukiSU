#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace sumhp {
namespace kernel_build_detail {
inline bool valid_text(std::string_view text) {
    return !text.empty() && text.size() <= 64 &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char byte) { return byte < 0x20 || byte == 0x7f; });
}

inline std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> result;
    while (!text.empty()) {
        const auto begin = text.find_first_not_of(' ');
        if (begin == std::string_view::npos)
            break;
        text.remove_prefix(begin);
        const auto end = text.find(' ');
        result.push_back(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end);
    }
    return result;
}

inline int decimal(std::string_view text) {
    if (text.empty() || text.size() > 4)
        return -1;
    int result = 0;
    for (const char byte : text) {
        if (byte < '0' || byte > '9')
            return -1;
        result = result * 10 + byte - '0';
    }
    return result;
}

inline bool valid_date(const std::vector<std::string_view>& fields, std::size_t begin) {
    if (fields.size() - begin != 6)
        return false;
    constexpr std::array<std::string_view, 7> weekdays = {"Sun", "Mon", "Tue", "Wed",
                                                          "Thu", "Fri", "Sat"};
    constexpr std::array<std::string_view, 12> months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    constexpr std::array<int, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (std::find(weekdays.begin(), weekdays.end(), fields[begin]) == weekdays.end())
        return false;
    const auto month = std::find(months.begin(), months.end(), fields[begin + 1]);
    if (month == months.end())
        return false;
    const auto year_text = fields[begin + 5];
    const int year = decimal(year_text);
    if (year_text.size() != 4 || year < 1970)
        return false;
    const int day = decimal(fields[begin + 2]);
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int max_day =
        days[month - months.begin()] + (month == months.begin() + 1 && leap ? 1 : 0);
    if (fields[begin + 2].size() > 2 || day < 1 || day > max_day)
        return false;
    const auto time = fields[begin + 3];
    if (time.size() != 8 || time[2] != ':' || time[5] != ':')
        return false;
    const int hour = decimal(time.substr(0, 2));
    const int minute = decimal(time.substr(3, 2));
    const int second = decimal(time.substr(6, 2));
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
        return false;
    const auto zone = fields[begin + 4];
    if (zone.empty())
        return false;
    if (zone.front() == '+' || zone.front() == '-') {
        return zone.size() == 5 && decimal(zone.substr(1, 2)) >= 0 &&
               decimal(zone.substr(1, 2)) <= 23 && decimal(zone.substr(3, 2)) >= 0 &&
               decimal(zone.substr(3, 2)) <= 59;
    }
    return zone.size() <= 8 && std::all_of(zone.begin(), zone.end(),
                                           [](char byte) { return byte >= 'A' && byte <= 'Z'; });
}
}  // namespace kernel_build_detail

// A convenience draft derived from the ROM date; it does not identify the
// stock kernel. Preserve the original build number and compiler-supplied flags.
inline std::string suggest_kernel_build_version(const std::string& original_version,
                                                const std::string& system_build_date) {
    using namespace kernel_build_detail;
    if (!valid_text(original_version) || !valid_text(system_build_date))
        return {};
    const auto original = words(original_version);
    const auto date = words(system_build_date);
    if (original.size() < 7 || !valid_date(original, original.size() - 6) || !valid_date(date, 0) ||
        original.front().size() < 2 || original.front().front() != '#' ||
        !std::all_of(original.front().begin() + 1, original.front().end(),
                     [](char byte) { return byte >= '0' && byte <= '9'; }))
        return {};
    const auto timestamp = original[original.size() - 6].data() - original_version.data();
    std::string suggestion = original_version.substr(0, timestamp);
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i != 0)
            suggestion += ' ';
        suggestion += date[i];
    }
    return valid_text(suggestion) ? suggestion : std::string{};
}
}  // namespace sumhp
