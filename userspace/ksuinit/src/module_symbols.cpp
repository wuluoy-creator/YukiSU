#include "module_symbols.hpp"

#include <charconv>
#include <cstring>

#include <elf.h>

namespace ksuinit {
namespace {

bool valid_range(uint64_t offset, uint64_t length, size_t size) {
    return offset <= size && length <= size - static_cast<size_t>(offset);
}

template <typename T>
T read_object(const std::vector<uint8_t>& module, size_t offset) {
    T value{};
    std::memcpy(&value, module.data() + offset, sizeof(value));
    return value;
}

bool ranges_overlap(uint64_t left, uint64_t left_size, uint64_t right, uint64_t right_size) {
    return left_size != 0 && right_size != 0 && left < right + right_size &&
           right < left + left_size;
}

std::string_view next_token(std::string_view& rest) {
    constexpr std::string_view whitespace = " \t\r\n\f\v";
    const size_t start = rest.find_first_not_of(whitespace);
    if (start == std::string_view::npos) {
        rest = {};
        return {};
    }
    const size_t end = rest.find_first_of(whitespace, start);
    const std::string_view token = rest.substr(start, end - start);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
    return token;
}

}  // namespace

bool collect_undefined_symbols(const std::vector<uint8_t>& module,
                               std::vector<ModuleSymbol>& symbols, std::string& error) {
    symbols.clear();
    error.clear();
    if (module.size() < sizeof(Elf64_Ehdr)) {
        error = "module is too small for an ELF header";
        return false;
    }
    const auto header = read_object<Elf64_Ehdr>(module, 0);
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT ||
        header.e_ehsize != sizeof(Elf64_Ehdr) || header.e_type != ET_REL ||
        header.e_machine != EM_AARCH64) {
        error = "module is not an AArch64 ELF64 little-endian relocatable object";
        return false;
    }
    const size_t section_table_size = size_t{header.e_shnum} * sizeof(Elf64_Shdr);
    if (header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shnum == 0 ||
        header.e_shoff < sizeof(Elf64_Ehdr) ||
        !valid_range(header.e_shoff, section_table_size, module.size())) {
        error = "module section table is invalid";
        return false;
    }

    Elf64_Shdr symbol_table{};
    bool found = false;
    for (size_t index = 0; index < header.e_shnum; ++index) {
        const auto section = read_object<Elf64_Shdr>(
            module, static_cast<size_t>(header.e_shoff) + (index * sizeof(Elf64_Shdr)));
        if (section.sh_type == SHT_SYMTAB) {
            symbol_table = section;
            found = true;
            break;
        }
    }
    if (!found || symbol_table.sh_entsize != sizeof(Elf64_Sym) ||
        symbol_table.sh_size < sizeof(Elf64_Sym) || symbol_table.sh_size % sizeof(Elf64_Sym) != 0 ||
        !valid_range(symbol_table.sh_offset, symbol_table.sh_size, module.size()) ||
        symbol_table.sh_link >= header.e_shnum ||
        symbol_table.sh_info > symbol_table.sh_size / sizeof(Elf64_Sym)) {
        error = "module symbol table is invalid";
        return false;
    }

    const auto string_table =
        read_object<Elf64_Shdr>(module, static_cast<size_t>(header.e_shoff) +
                                            (size_t{symbol_table.sh_link} * sizeof(Elf64_Shdr)));
    if (string_table.sh_type != SHT_STRTAB || string_table.sh_size == 0 ||
        !valid_range(string_table.sh_offset, string_table.sh_size, module.size())) {
        error = "module symbol string table is invalid";
        return false;
    }
    // Patching a symbol must never overwrite headers or names still being read.
    if (ranges_overlap(symbol_table.sh_offset, symbol_table.sh_size, 0, sizeof(Elf64_Ehdr)) ||
        ranges_overlap(symbol_table.sh_offset, symbol_table.sh_size, header.e_shoff,
                       section_table_size) ||
        ranges_overlap(symbol_table.sh_offset, symbol_table.sh_size, string_table.sh_offset,
                       string_table.sh_size)) {
        error = "module symbol table overlaps ELF metadata";
        return false;
    }

    const auto* strings = reinterpret_cast<const char*>(module.data() + string_table.sh_offset);
    if (strings[0] != '\0' || strings[string_table.sh_size - 1] != '\0') {
        error = "module symbol string table is not NUL-terminated";
        return false;
    }
    const size_t count = static_cast<size_t>(symbol_table.sh_size / sizeof(Elf64_Sym));
    // Validate all names first so failures leave no partially collected results.
    for (size_t index = 0; index < count; ++index) {
        const auto symbol = read_object<Elf64_Sym>(
            module, static_cast<size_t>(symbol_table.sh_offset) + (index * sizeof(Elf64_Sym)));
        if (symbol.st_name >= string_table.sh_size) {
            error = "module symbol name is outside the string table";
            return false;
        }
    }
    for (size_t index = 1; index < count; ++index) {
        const size_t offset =
            static_cast<size_t>(symbol_table.sh_offset) + (index * sizeof(Elf64_Sym));
        const auto symbol = read_object<Elf64_Sym>(module, offset);
        if (symbol.st_shndx == SHN_UNDEF && strings[symbol.st_name] != '\0') {
            // The validated final NUL guarantees this bounded name terminates.
            symbols.push_back({offset, std::string_view(strings + symbol.st_name)});
        }
    }
    return true;
}

bool parse_kallsyms_line(std::string_view line, std::string_view& name, uint64_t& address) {
    name = {};
    address = 0;
    const std::string_view address_text = next_token(line);
    const std::string_view type = next_token(line);
    const std::string_view symbol_name = next_token(line);
    if (address_text.empty() || type.size() != 1 || symbol_name.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(
        address_text.data(), address_text.data() + address_text.size(), address, 16);
    if (error != std::errc{} || end != address_text.data() + address_text.size() || address == 0) {
        return false;
    }

    size_t suffix = symbol_name.find('$');
    if (suffix == std::string_view::npos) {
        suffix = symbol_name.find(".llvm.");
    }
    name = symbol_name.substr(0, suffix);
    return !name.empty();
}

}  // namespace ksuinit
