#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ksuinit {

struct ModuleSymbol {
    size_t offset;
    // References the module image; do not resize it while these views are in use.
    std::string_view name;
};

// Validate the ELF tables before exposing any symbols to the loader. On failure,
// symbols is empty and the module is unchanged.
bool collect_undefined_symbols(const std::vector<uint8_t>& module,
                               std::vector<ModuleSymbol>& symbols, std::string& error);

// Returns a view into line with the same compiler suffix normalization used by
// the loader. Hidden (zero) addresses are never considered resolved symbols.
bool parse_kallsyms_line(std::string_view line, std::string_view& name, uint64_t& address);

}  // namespace ksuinit
