#pragma once

#include <elf.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ksud::kernelsu_loader {

// Offsets remain valid even for unaligned ELF sections. Never form typed
// pointers into an image supplied by a file.
using UndefinedSymbols = std::unordered_map<std::string, std::vector<size_t>>;

template <typename Ehdr, typename Shdr, typename Sym>
std::optional<UndefinedSymbols> collect_undefined_symbols(const std::vector<uint8_t>& image) {
    const auto contains = [&image](uint64_t offset, uint64_t size) {
        return offset <= image.size() && size <= image.size() - offset;
    };
    Ehdr header{};
    if (!contains(0, sizeof(header))) {
        return std::nullopt;
    }
    std::memcpy(&header, image.data(), sizeof(header));
    const auto expected_class = sizeof(Ehdr) == sizeof(Elf64_Ehdr) ? ELFCLASS64 : ELFCLASS32;
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != expected_class || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT ||
        header.e_type != ET_REL || header.e_ehsize != sizeof(Ehdr) ||
        header.e_shentsize != sizeof(Shdr) || header.e_shnum == 0 ||
        header.e_shoff < sizeof(Ehdr) ||
        !contains(header.e_shoff, static_cast<uint64_t>(header.e_shnum) * sizeof(Shdr))) {
        return std::nullopt;
    }

    const auto read_section = [&image, &header](size_t index) {
        Shdr section{};
        std::memcpy(&section, image.data() + header.e_shoff + index * sizeof(Shdr),
                    sizeof(section));
        return section;
    };
    for (size_t index = 0; index < header.e_shnum; ++index) {
        const Shdr symbols = read_section(index);
        if (symbols.sh_type != SHT_SYMTAB) {
            continue;
        }
        if (symbols.sh_link >= header.e_shnum || symbols.sh_entsize != sizeof(Sym) ||
            symbols.sh_size % sizeof(Sym) != 0 || symbols.sh_size < sizeof(Sym) ||
            symbols.sh_info > symbols.sh_size / sizeof(Sym) ||
            !contains(symbols.sh_offset, symbols.sh_size)) {
            return std::nullopt;
        }
        const Shdr strings = read_section(symbols.sh_link);
        if (strings.sh_type != SHT_STRTAB || strings.sh_size == 0 ||
            !contains(strings.sh_offset, strings.sh_size)) {
            return std::nullopt;
        }
        // Every range is already bounded by image.size(), so these additions
        // cannot overflow. Symbol writes must not change ELF headers or names.
        const auto overlaps_symbols = [&symbols](uint64_t offset, uint64_t size) {
            return symbols.sh_offset < offset + size &&
                   offset < symbols.sh_offset + symbols.sh_size;
        };
        if (overlaps_symbols(0, sizeof(Ehdr)) ||
            overlaps_symbols(header.e_shoff, uint64_t{header.e_shnum} * sizeof(Shdr)) ||
            overlaps_symbols(strings.sh_offset, strings.sh_size)) {
            return std::nullopt;
        }
        UndefinedSymbols unresolved;
        const auto* names = reinterpret_cast<const char*>(image.data() + strings.sh_offset);
        if (names[0] != '\0' || names[strings.sh_size - 1] != '\0') {
            return std::nullopt;
        }
        for (size_t symbol_index = 1; symbol_index < symbols.sh_size / sizeof(Sym);
             ++symbol_index) {
            const size_t offset = symbols.sh_offset + symbol_index * sizeof(Sym);
            Sym symbol{};
            std::memcpy(&symbol, image.data() + offset, sizeof(symbol));
            if (symbol.st_name >= strings.sh_size) {
                return std::nullopt;
            }
            if (symbol.st_shndx != SHN_UNDEF) {
                continue;
            }
            const char* name = names + symbol.st_name;
            const auto* end =
                static_cast<const char*>(std::memchr(name, '\0', strings.sh_size - symbol.st_name));
            if (end == nullptr) {
                return std::nullopt;
            }
            if (end != name) {
                unresolved[std::string(name, end)].push_back(offset);
            }
        }
        return unresolved;
    }
    return std::nullopt;
}

}  // namespace ksud::kernelsu_loader
