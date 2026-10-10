#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ksud {

std::optional<std::vector<std::string>> parse_xperm_set(std::string_view input);

}  // namespace ksud
