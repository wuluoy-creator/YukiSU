#include "module_symbols.hpp"
#include "vermagic.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <elf.h>

#define CHECK(expression)                                                 \
    do {                                                                  \
        if (!(expression)) {                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
            std::exit(1);                                                 \
        }                                                                 \
    } while (false)

namespace {

constexpr size_t kSections = 64;
constexpr size_t kSymbols = 384;
constexpr size_t kStrings = 480;
constexpr size_t kSectionNames = 512;
constexpr size_t kModinfo = 576;

template <typename T>
T get(const std::vector<uint8_t>& module, size_t offset) {
    T value{};
    std::memcpy(&value, module.data() + offset, sizeof(value));
    return value;
}

template <typename T>
void put(std::vector<uint8_t>& module, size_t offset, const T& value) {
    std::memcpy(module.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> make_module() {
    std::vector<uint8_t> module(640);
    Elf64_Ehdr header{};
    std::memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_version = EV_CURRENT;
    header.e_ehsize = sizeof(header);
    header.e_type = ET_REL;
    header.e_machine = EM_AARCH64;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shoff = kSections;
    header.e_shnum = 5;
    header.e_shstrndx = 3;
    put(module, 0, header);

    constexpr char names[] = "\0target\0unresolved\0defined\0";
    constexpr char section_names[] = "\0.symtab\0.strtab\0.shstrtab\0.modinfo\0";
    constexpr char modinfo[] = "license=GPL\0vermagic=old SMP\0depends=\0";
    std::memcpy(module.data() + kStrings, names, sizeof(names));
    std::memcpy(module.data() + kSectionNames, section_names, sizeof(section_names));
    std::memcpy(module.data() + kModinfo, modinfo, sizeof(modinfo));

    Elf64_Shdr section{};
    section.sh_type = SHT_SYMTAB;
    section.sh_offset = kSymbols;
    section.sh_size = 4 * sizeof(Elf64_Sym);
    section.sh_entsize = sizeof(Elf64_Sym);
    section.sh_link = 2;
    section.sh_info = 1;
    section.sh_name = 1;
    put(module, kSections + sizeof(section), section);
    section = {};
    section.sh_type = SHT_STRTAB;
    section.sh_offset = kStrings;
    section.sh_size = sizeof(names);
    section.sh_name = 9;
    put(module, kSections + 2 * sizeof(section), section);
    section.sh_offset = kSectionNames;
    section.sh_size = sizeof(section_names);
    section.sh_name = 17;
    put(module, kSections + 3 * sizeof(section), section);
    section = {};
    section.sh_type = SHT_PROGBITS;
    section.sh_offset = kModinfo;
    section.sh_size = sizeof(modinfo);
    section.sh_name = 27;
    section.sh_addralign = 1;
    put(module, kSections + 4 * sizeof(section), section);

    Elf64_Sym symbol{};
    symbol.st_name = 1;
    symbol.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    put(module, kSymbols + sizeof(symbol), symbol);
    symbol.st_name = 8;
    put(module, kSymbols + 2 * sizeof(symbol), symbol);
    symbol.st_name = 19;
    symbol.st_shndx = 4;
    symbol.st_value = 123;
    put(module, kSymbols + 3 * sizeof(symbol), symbol);
    return module;
}

template <typename F>
void rejects(F&& mutate) {
    auto module = make_module();
    mutate(module);
    const auto original = module;
    std::vector<ksuinit::ModuleSymbol> symbols{{0, "stale"}};
    std::string error;
    CHECK(!ksuinit::collect_undefined_symbols(module, symbols, error));
    CHECK(!error.empty());
    CHECK(symbols.empty());
    CHECK(module == original);
}

template <typename F>
void rejects_header(F&& mutate) {
    rejects([&](auto& module) {
        auto header = get<Elf64_Ehdr>(module, 0);
        mutate(header);
        put(module, 0, header);
    });
}

template <typename F>
void rejects_section(size_t index, F&& mutate) {
    rejects([&](auto& module) {
        const size_t offset = kSections + index * sizeof(Elf64_Shdr);
        auto section = get<Elf64_Shdr>(module, offset);
        mutate(section);
        put(module, offset, section);
    });
}

void test_module_symbols() {
    auto module = make_module();
    const auto original = module;
    std::vector<ksuinit::ModuleSymbol> symbols;
    std::string error;
    CHECK(ksuinit::collect_undefined_symbols(module, symbols, error));
    CHECK(error.empty());
    CHECK(symbols.size() == 2);
    CHECK(symbols[0].name == "target");
    CHECK(symbols[0].offset == kSymbols + sizeof(Elf64_Sym));
    CHECK(symbols[1].name == "unresolved");
    CHECK(module == original);

    rejects([](auto& image) { image.resize(sizeof(Elf64_Ehdr) - 1); });
    rejects_header([](auto& h) { h.e_ident[EI_MAG0] = 0; });
    rejects_header([](auto& h) { h.e_ident[EI_CLASS] = ELFCLASS32; });
    rejects_header([](auto& h) { h.e_ident[EI_DATA] = ELFDATA2MSB; });
    rejects_header([](auto& h) { h.e_ident[EI_VERSION] = 0; });
    rejects_header([](auto& h) { h.e_version = 0; });
    rejects_header([](auto& h) { h.e_ehsize = 0; });
    rejects_header([](auto& h) { h.e_machine = EM_X86_64; });
    rejects_header([](auto& h) { h.e_type = ET_EXEC; });
    rejects_header([](auto& h) { h.e_shnum = 0; });
    rejects_header([](auto& h) { h.e_shentsize = 1; });
    rejects_header([](auto& h) { h.e_shoff = 0; });
    rejects_header([](auto& h) { h.e_shoff = std::numeric_limits<uint64_t>::max(); });
    rejects_header([](auto& h) { h.e_shnum = 65535; });
    rejects_section(1, [](auto& s) { s.sh_type = SHT_NULL; });
    rejects_section(1, [](auto& s) { s.sh_link = 5; });
    rejects_section(1, [](auto& s) { s.sh_entsize = 0; });
    rejects_section(1, [](auto& s) { s.sh_size = 1; });
    rejects_section(1, [](auto& s) { s.sh_size = 0; });
    rejects_section(1, [](auto& s) { s.sh_info = 5; });
    rejects_section(1, [](auto& s) { s.sh_offset = std::numeric_limits<uint64_t>::max(); });
    rejects_section(1, [](auto& s) { s.sh_size = std::numeric_limits<uint64_t>::max() - 7; });
    rejects_section(1, [](auto& s) { s.sh_offset = 0; });
    rejects_section(1, [](auto& s) { s.sh_offset = kSections; });
    rejects_section(1, [](auto& s) { s.sh_offset = kStrings; });
    rejects_section(2, [](auto& s) { s.sh_type = SHT_PROGBITS; });
    rejects_section(2, [](auto& s) { s.sh_size = 0; });
    rejects_section(2, [](auto& s) { s.sh_offset = std::numeric_limits<uint64_t>::max(); });
    rejects_section(2, [](auto& s) { s.sh_size = std::numeric_limits<uint64_t>::max(); });
    rejects([](auto& image) { image[kStrings] = 'x'; });
    rejects([](auto& image) {
        const auto strings = get<Elf64_Shdr>(image, kSections + 2 * sizeof(Elf64_Shdr));
        image[kStrings + strings.sh_size - 1] = 'x';
    });
    rejects([](auto& image) {
        auto symbol = get<Elf64_Sym>(image, kSymbols + sizeof(Elf64_Sym));
        symbol.st_name = std::numeric_limits<uint32_t>::max();
        put(image, kSymbols + sizeof(Elf64_Sym), symbol);
    });
    // Tables need not be naturally aligned in the input buffer for safe reads.
    auto unaligned = make_module();
    auto table = get<Elf64_Shdr>(unaligned, kSections + sizeof(Elf64_Shdr));
    std::memmove(unaligned.data() + kSymbols + 1, unaligned.data() + kSymbols, table.sh_size);
    ++table.sh_offset;
    put(unaligned, kSections + sizeof(Elf64_Shdr), table);
    // Move the strings out of the now-overlapping final byte as well.
    const auto strings = get<Elf64_Shdr>(module, kSections + 2 * sizeof(Elf64_Shdr));
    std::memcpy(unaligned.data() + kStrings + 1, module.data() + kStrings, strings.sh_size);
    auto moved_strings = strings;
    ++moved_strings.sh_offset;
    put(unaligned, kSections + 2 * sizeof(Elf64_Shdr), moved_strings);
    CHECK(ksuinit::collect_undefined_symbols(unaligned, symbols, error));
    CHECK(symbols.size() == 2 && symbols[0].name == "target");

    // Deterministic malformed input coverage, useful with ASan/UBSan builds.
    uint32_t state = 0x12345678;
    for (size_t iteration = 0; iteration < 10000; ++iteration) {
        auto image = original;
        for (size_t change = 0; change < 4; ++change) {
            state = state * 1664525u + 1013904223u;
            const size_t offset = state % image.size();
            state = state * 1664525u + 1013904223u;
            image[offset] ^= static_cast<uint8_t>(state >> 24);
        }
        const bool valid = ksuinit::collect_undefined_symbols(image, symbols, error);
        CHECK(valid || symbols.empty());
        for (const auto& symbol : symbols) {
            CHECK(symbol.offset + sizeof(Elf64_Sym) <= image.size());
            CHECK(!symbol.name.empty());
        }
    }
}

void test_kallsyms() {
    std::string_view name;
    uint64_t address = 0;
    CHECK(ksuinit::parse_kallsyms_line("ffffffc012345678 T target", name, address));
    CHECK(name == "target" && address == 0xffffffc012345678ULL);
    CHECK(ksuinit::parse_kallsyms_line("  1\tt\ttarget.llvm.123  [module]\r", name, address));
    CHECK(name == "target" && address == 1);
    CHECK(ksuinit::parse_kallsyms_line("a W target$123", name, address));
    CHECK(name == "target" && address == 10);
    for (const char* line :
         {"", "1", "1 T", "1 TT target", "0000000000000000 T target", "-1 T target", "+1 T target",
          "xyz T target", "1z T target", "fffffffffffffffff T target", "1 T $suffix"}) {
        CHECK(!ksuinit::parse_kallsyms_line(line, name, address));
    }
}

void test_vermagic() {
    ksuinit::VermagicMismatch mismatch;
    CHECK(ksuinit::extract_vermagic_mismatch(
        "3,1,1,-;kernelsu: version magic 'old SMP' should be 'new SMP PREEMPT'\n", mismatch));
    CHECK(mismatch.module_vermagic == "old SMP");
    CHECK(mismatch.required_vermagic == "new SMP PREEMPT");
    CHECK(!ksuinit::extract_vermagic_mismatch(
        "11,1,1,-;kernelsu: version magic 'old SMP' should be 'new SMP'\n", mismatch));
    auto image = make_module();
    std::string error;
    CHECK(ksuinit::replace_module_vermagic(image, "old SMP", "new SMP PREEMPT", error));
    const auto section = get<Elf64_Shdr>(image, kSections + 4 * sizeof(Elf64_Shdr));
    const std::string_view modinfo(reinterpret_cast<const char*>(image.data() + section.sh_offset),
                                   section.sh_size);
    CHECK(modinfo.find("vermagic=new SMP PREEMPT") != std::string_view::npos);
    CHECK(modinfo.find("license=GPL") != std::string_view::npos);
    CHECK(modinfo.find("depends=") != std::string_view::npos);
    std::vector<ksuinit::ModuleSymbol> symbols;
    CHECK(ksuinit::collect_undefined_symbols(image, symbols, error));
    CHECK(symbols.size() == 2);
}

}  // namespace

int main() {
    test_module_symbols();
    test_kallsyms();
    test_vermagic();
    std::puts("ksuinit module symbol and vermagic regressions passed");
}
