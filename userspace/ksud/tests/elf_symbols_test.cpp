#include "../src/elf_symbols.hpp"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {

template <typename T>
void put(std::vector<uint8_t>& image, size_t offset, const T& value) {
    std::memcpy(image.data() + offset, &value, sizeof(value));
}

template <typename Ehdr, typename Shdr, typename Sym>
void check_symbols() {
    // Deliberately unaligned sections exercise memcpy-based ELF access.
    Ehdr header{};
    std::memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = sizeof(Ehdr) == sizeof(Elf64_Ehdr) ? ELFCLASS64 : ELFCLASS32;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_version = EV_CURRENT;
    header.e_type = ET_REL;
    header.e_ehsize = sizeof(Ehdr);
    header.e_shentsize = sizeof(Shdr);
    header.e_shnum = 3;
    header.e_shoff = sizeof(Ehdr) + 1;
    Shdr symbols{};
    symbols.sh_type = SHT_SYMTAB;
    symbols.sh_link = 2;
    symbols.sh_offset = header.e_shoff + 3 * sizeof(Shdr) + 1;
    symbols.sh_size = 4 * sizeof(Sym);
    symbols.sh_entsize = sizeof(Sym);
    Shdr strings{};
    strings.sh_type = SHT_STRTAB;
    strings.sh_offset = symbols.sh_offset + symbols.sh_size;
    const char names[] = "\0printk\0defined";
    strings.sh_size = sizeof(names);
    std::vector<uint8_t> image(strings.sh_offset + strings.sh_size);
    put(image, 0, header);
    put(image, header.e_shoff + sizeof(Shdr), symbols);
    put(image, header.e_shoff + 2 * sizeof(Shdr), strings);
    std::memcpy(image.data() + strings.sh_offset, names, sizeof(names));
    Sym symbol{};
    symbol.st_name = 1;
    symbol.st_shndx = SHN_UNDEF;
    put(image, symbols.sh_offset + sizeof(Sym), symbol);
    put(image, symbols.sh_offset + 2 * sizeof(Sym), symbol);
    symbol.st_shndx = 1;
    symbol.st_name = 8;
    put(image, symbols.sh_offset + 3 * sizeof(Sym), symbol);

    const auto parse = [](const std::vector<uint8_t>& bytes) {
        return ksud::kernelsu_loader::collect_undefined_symbols<Ehdr, Shdr, Sym>(bytes);
    };
    const auto result = parse(image);
    assert(result && result->size() == 1);
    const std::vector<size_t> expected = {symbols.sh_offset + sizeof(Sym),
                                          symbols.sh_offset + 2 * sizeof(Sym)};
    assert(result->at("printk") == expected);
    // Reject every truncation, including a string without its final terminator.
    for (size_t size = 0; size < image.size(); ++size) {
        assert(!parse(std::vector<uint8_t>(image.begin(), image.begin() + size)));
    }
    const auto bad_header = [&](const Ehdr& bad) {
        auto bytes = image;
        put(bytes, 0, bad);
        assert(!parse(bytes));
    };
    auto bad = header;
    bad.e_shoff = std::numeric_limits<decltype(bad.e_shoff)>::max();
    bad_header(bad);
    bad = header;
    bad.e_shentsize = 1;
    bad_header(bad);
    bad = header;
    bad.e_ident[EI_DATA] = ELFDATA2MSB;
    bad_header(bad);
    bad = header;
    bad.e_type = ET_EXEC;
    bad_header(bad);
    bad = header;
    bad.e_shnum = 0;
    bad_header(bad);
    const auto bad_section = [&](const Shdr& section, size_t index) {
        auto bytes = image;
        put(bytes, header.e_shoff + index * sizeof(Shdr), section);
        assert(!parse(bytes));
    };
    auto section = symbols;
    section.sh_link = header.e_shnum;
    bad_section(section, 1);
    section = symbols;
    section.sh_entsize = 0;
    bad_section(section, 1);
    section = symbols;
    section.sh_size -= 1;
    bad_section(section, 1);
    section = symbols;
    section.sh_offset = std::numeric_limits<decltype(section.sh_offset)>::max();
    bad_section(section, 1);
    section = symbols;
    section.sh_offset = header.e_shoff;
    bad_section(section, 1);
    section = strings;
    section.sh_offset = symbols.sh_offset;
    bad_section(section, 2);
    section = strings;
    section.sh_type = SHT_PROGBITS;
    bad_section(section, 2);
    section = strings;
    section.sh_size = 0;
    bad_section(section, 2);
    auto bytes = image;
    symbol.st_shndx = SHN_UNDEF;
    symbol.st_name = strings.sh_size;
    put(bytes, symbols.sh_offset + sizeof(Sym), symbol);
    assert(!parse(bytes));
    bytes = image;
    std::memset(bytes.data() + strings.sh_offset + 1, 'x', strings.sh_size - 1);
    assert(!parse(bytes));
    bytes = image;
    symbol.st_name = 0;
    put(bytes, symbols.sh_offset + sizeof(Sym), symbol);
    put(bytes, symbols.sh_offset + 2 * sizeof(Sym), symbol);
    const auto empty = parse(bytes);
    assert(empty && empty->empty());
}

}  // namespace

int main() {
    check_symbols<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>();
    check_symbols<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>();
    puts("ELF symbol bounds tests passed (32-bit and 64-bit)");
}
