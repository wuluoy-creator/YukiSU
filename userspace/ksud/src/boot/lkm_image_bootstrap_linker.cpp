#include "lkm_image_bootstrap_linker.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace ksud::boot::lkm_image {
namespace {

constexpr std::uint16_t kEtRel = 1;
constexpr std::uint16_t kMachineAarch64 = 183;
constexpr std::uint32_t kShtProgbits = 1;
constexpr std::uint32_t kShtSymtab = 2;
constexpr std::uint32_t kShtStrtab = 3;
constexpr std::uint32_t kShtRela = 4;
constexpr std::uint32_t kShtNobits = 8;
constexpr std::uint32_t kShtRel = 9;
constexpr std::uint64_t kShfWrite = 1;
constexpr std::uint64_t kShfAlloc = 2;
constexpr std::uint64_t kShfExecInstr = 4;
constexpr std::uint16_t kShnUndef = 0;
constexpr std::uint16_t kShnAbs = 0xfff1;
constexpr std::uint32_t kRAbs64 = 257;
constexpr std::uint32_t kRAbs32 = 258;
constexpr std::uint32_t kRAdrPrelPgHi21 = 275;
constexpr std::uint32_t kRAddAbsLo12Nc = 277;
constexpr std::uint32_t kRJump26 = 282;
constexpr std::uint32_t kRCall26 = 283;

// Internal ELF records intentionally remain aggregate-like for bounded parsing.
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
struct Section {
    std::string name;
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::size_t link = 0;
    std::size_t info = 0;
    std::size_t alignment = 1;
    std::size_t entry_size = 0;
    std::size_t output_offset = std::numeric_limits<std::size_t>::max();

    [[nodiscard]] bool loadable() const {
        return output_offset != std::numeric_limits<std::size_t>::max();
    }
};

struct Symbol {
    std::string name;
    std::uint64_t value = 0;
    std::uint16_t section = kShnUndef;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

bool set_error(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool range_ok(std::size_t data_size, std::size_t offset, std::size_t size) {
    return offset <= data_size && size <= data_size - offset;
}

bool read_u16(const std::uint8_t* data, std::size_t size, std::size_t offset,
              std::uint16_t* value) {
    if (!range_ok(size, offset, 2))
        return false;
    *value = static_cast<std::uint16_t>(data[offset]) |
             (static_cast<std::uint16_t>(data[offset + 1]) << 8);
    return true;
}

bool read_u32(const std::uint8_t* data, std::size_t size, std::size_t offset,
              std::uint32_t* value) {
    if (!range_ok(size, offset, 4))
        return false;
    *value = static_cast<std::uint32_t>(data[offset]) |
             (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return true;
}

bool read_u64(const std::uint8_t* data, std::size_t size, std::size_t offset,
              std::uint64_t* value) {
    if (!range_ok(size, offset, 8))
        return false;
    std::uint64_t result = 0;
    for (unsigned int index = 0; index < 8; ++index) {
        result |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8);
    }
    *value = result;
    return true;
}

bool read_i64(const std::uint8_t* data, std::size_t size, std::size_t offset, std::int64_t* value) {
    std::uint64_t raw = 0;
    if (!read_u64(data, size, offset, &raw))
        return false;
    *value = static_cast<std::int64_t>(raw);
    return true;
}

bool write_u32(std::vector<std::uint8_t>* output, std::size_t offset, std::uint32_t value) {
    if (output == nullptr || !range_ok(output->size(), offset, 4))
        return false;
    (*output)[offset] = static_cast<std::uint8_t>(value);
    (*output)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    (*output)[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    (*output)[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    return true;
}

bool write_u64(std::vector<std::uint8_t>* output, std::size_t offset, std::uint64_t value) {
    if (output == nullptr || !range_ok(output->size(), offset, 8))
        return false;
    for (unsigned int index = 0; index < 8; ++index) {
        (*output)[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
    return true;
}

bool align_up(std::size_t value, std::size_t alignment, std::size_t* result) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return false;
    if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1))
        return false;
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

bool checked_address_add(std::uint64_t base, unsigned __int128 offset, std::uint64_t* result) {
    const auto maximum = static_cast<unsigned __int128>(std::numeric_limits<std::uint64_t>::max());
    const unsigned __int128 value = static_cast<unsigned __int128>(base) + offset;
    if (value > maximum)
        return false;
    *result = static_cast<std::uint64_t>(value);
    return true;
}

bool read_string(const std::uint8_t* data, std::size_t data_size, std::size_t table_offset,
                 std::size_t table_size, std::uint32_t string_offset, std::string* result) {
    if (string_offset >= table_size || !range_ok(data_size, table_offset, table_size))
        return false;
    const std::size_t start = table_offset + string_offset;
    const std::size_t end = table_offset + table_size;
    std::size_t position = start;
    while (position < end && data[position] != 0)
        ++position;
    if (position == end)
        return false;
    result->assign(reinterpret_cast<const char*>(data + start), position - start);
    return true;
}

bool apply_relocation(std::vector<std::uint8_t>* output, std::size_t output_offset,
                      std::uint64_t place_address, std::uint32_t type, std::uint64_t symbol_value,
                      std::int64_t addend, std::string* error) {
    const __int128 value = static_cast<__int128>(symbol_value) + addend;
    auto fail = [&](const char* message) { return set_error(error, message); };
    switch (type) {
    case kRAbs64:
        if ((output_offset & 7U) != 0 || value < 0 ||
            value > static_cast<__int128>(std::numeric_limits<std::uint64_t>::max()))
            return fail("invalid ABS64 relocation");
        return write_u64(output, output_offset, static_cast<std::uint64_t>(value)) ||
               fail("ABS64 relocation is outside output");
    case kRAbs32:
        if ((output_offset & 3U) != 0 || value < 0 ||
            value > static_cast<__int128>(std::numeric_limits<std::uint32_t>::max()))
            return fail("invalid ABS32 relocation");
        return write_u32(output, output_offset, static_cast<std::uint32_t>(value)) ||
               fail("ABS32 relocation is outside output");
    case kRCall26:
    case kRJump26: {
        const __int128 delta = value - static_cast<__int128>(place_address);
        if ((delta % 4) != 0)  // NOLINT(readability-magic-numbers)
            return fail("branch relocation is not instruction-aligned");
        const __int128 immediate = delta / 4;
        if (immediate < -(static_cast<__int128>(1) << 25) ||
            immediate >= (static_cast<__int128>(1) << 25))
            return fail("branch relocation is outside the AArch64 range");
        std::uint32_t instruction = 0;
        if (!read_u32(output->data(), output->size(), output_offset, &instruction))
            return fail("branch relocation is outside output");
        const std::uint32_t opcode = type == kRCall26 ? 0x94000000U : 0x14000000U;
        if ((instruction & 0xfc000000U) != opcode)
            return fail("branch relocation does not target a branch instruction");
        const std::uint32_t encoded =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(immediate) & 0x03ffffffU);
        return write_u32(output, output_offset, opcode | encoded) ||
               fail("branch relocation is outside output");
    }
    case kRAdrPrelPgHi21: {
        if (value < 0)
            return fail("ADRP relocation target is negative");
        const std::uint64_t target = static_cast<std::uint64_t>(value);
        const __int128 target_page = static_cast<__int128>(target & ~0xfffULL);
        const __int128 place_page = static_cast<__int128>(place_address & ~0xfffULL);
        const __int128 page_delta = (target_page - place_page) / 4096;
        if (page_delta < -(static_cast<__int128>(1) << 20) ||
            page_delta >= (static_cast<__int128>(1) << 20))
            return fail("ADRP relocation is outside the AArch64 range");
        std::uint32_t instruction = 0;
        if (!read_u32(output->data(), output->size(), output_offset, &instruction))
            return fail("ADRP relocation is outside output");
        if ((instruction & 0x9f000000U) != 0x90000000U)
            return fail("ADRP relocation does not target an ADRP instruction");
        const std::uint64_t immediate = static_cast<std::uint64_t>(page_delta) & 0x1fffffU;
        const std::uint32_t immlo = static_cast<std::uint32_t>((immediate & 3U) << 29);
        const std::uint32_t immhi = static_cast<std::uint32_t>(((immediate >> 2) & 0x7ffffU) << 5);
        return write_u32(output, output_offset, (instruction & ~0x60ffffe0U) | immlo | immhi) ||
               fail("ADRP relocation is outside output");
    }
    case kRAddAbsLo12Nc: {
        if (value < 0)
            return fail("ADD relocation target is negative");
        std::uint32_t instruction = 0;
        if (!read_u32(output->data(), output->size(), output_offset, &instruction))
            return fail("ADD relocation is outside output");
        if ((instruction & 0x7f400000U) != 0x11000000U)
            return fail("ADD relocation does not target ADD immediate");
        const std::uint32_t immediate =
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(value) & 0xfffU) << 10);
        return write_u32(output, output_offset, (instruction & ~0x003ffc00U) | immediate) ||
               fail("ADD relocation is outside output");
    }
    default:
        return set_error(error, "unsupported AArch64 bootstrap relocation");
    }
}

class ParsedBootstrap {
public:
    bool parse(const BootstrapObjectView& object, std::string* error) {
        data_ = object.data;
        size_ = object.size;
        if (data_ == nullptr || size_ < 64 || data_[0] != 0x7f || data_[1] != 'E' ||
            data_[2] != 'L' || data_[3] != 'F')
            return set_error(error, "embedded bootstrap is not an ELF file");
        if (data_[4] != 2 || data_[5] != 1 || data_[6] != 1)
            return set_error(error, "embedded bootstrap must be ELF64 little-endian");
        std::uint16_t type = 0;
        std::uint16_t machine = 0;
        std::uint32_t version = 0;
        if (!read_u16(data_, size_, 16, &type) || !read_u16(data_, size_, 18, &machine) ||
            !read_u32(data_, size_, 20, &version) || type != kEtRel || machine != kMachineAarch64 ||
            version != 1)
            return set_error(error, "embedded bootstrap has an unsupported ELF header");

        std::uint64_t section_offset_u64 = 0;
        std::uint16_t header_size = 0;
        std::uint16_t program_count = 0;
        std::uint16_t section_entry_size = 0;
        std::uint16_t section_count = 0;
        std::uint16_t section_names_index = 0;
        if (!read_u64(data_, size_, 40, &section_offset_u64) ||
            !read_u16(data_, size_, 52, &header_size) ||
            !read_u16(data_, size_, 56, &program_count) ||
            !read_u16(data_, size_, 58, &section_entry_size) ||
            !read_u16(data_, size_, 60, &section_count) ||
            !read_u16(data_, size_, 62, &section_names_index))
            return set_error(error, "embedded bootstrap ELF header is truncated");
        if (header_size < 64 || program_count != 0 || section_entry_size != 64 ||
            section_count == 0 || section_names_index >= section_count)
            return set_error(error, "embedded bootstrap section table is malformed");
        if (section_offset_u64 > std::numeric_limits<std::size_t>::max())
            return set_error(error, "embedded bootstrap section offset is too large");
        const std::size_t section_offset = static_cast<std::size_t>(section_offset_u64);
        if (section_count > (std::numeric_limits<std::size_t>::max() - section_offset) / 64 ||
            !range_ok(size_, section_offset, static_cast<std::size_t>(section_count) * 64))
            return set_error(error, "embedded bootstrap section table is outside the object");

        sections_.clear();
        sections_.reserve(section_count);
        std::vector<std::uint32_t> name_offsets;
        name_offsets.reserve(section_count);
        for (std::size_t index = 0; index < section_count; ++index) {
            const std::size_t offset = section_offset + (index * 64);
            std::uint32_t name_offset = 0;
            std::uint32_t section_type = 0;
            std::uint64_t flags = 0;
            std::uint64_t data_offset_u64 = 0;
            std::uint64_t section_size_u64 = 0;
            std::uint32_t link = 0;
            std::uint32_t info = 0;
            std::uint64_t alignment_u64 = 0;
            std::uint64_t entry_size_u64 = 0;
            if (!read_u32(data_, size_, offset, &name_offset) ||
                !read_u32(data_, size_, offset + 4, &section_type) ||
                !read_u64(data_, size_, offset + 8, &flags) ||
                !read_u64(data_, size_, offset + 24, &data_offset_u64) ||
                !read_u64(data_, size_, offset + 32, &section_size_u64) ||
                !read_u32(data_, size_, offset + 40, &link) ||
                !read_u32(data_, size_, offset + 44, &info) ||
                !read_u64(data_, size_, offset + 48, &alignment_u64) ||
                !read_u64(data_, size_, offset + 56, &entry_size_u64))
                return set_error(error, "embedded bootstrap section header is truncated");
            if (data_offset_u64 > std::numeric_limits<std::size_t>::max() ||
                section_size_u64 > std::numeric_limits<std::size_t>::max() ||
                alignment_u64 > std::numeric_limits<std::size_t>::max() ||
                entry_size_u64 > std::numeric_limits<std::size_t>::max())
                return set_error(error, "embedded bootstrap section size is too large");
            if (alignment_u64 != 0 && (alignment_u64 & (alignment_u64 - 1)) != 0)
                return set_error(error,
                                 "embedded bootstrap section alignment is not a power of two");
            Section section;
            section.type = section_type;
            section.flags = flags;
            section.offset = static_cast<std::size_t>(data_offset_u64);
            section.size = static_cast<std::size_t>(section_size_u64);
            section.link = link;
            section.info = info;
            section.alignment = std::max<std::size_t>(1, static_cast<std::size_t>(alignment_u64));
            section.entry_size = static_cast<std::size_t>(entry_size_u64);
            if (section.type != kShtNobits && !range_ok(size_, section.offset, section.size))
                return set_error(error, "embedded bootstrap section is outside the object");
            sections_.push_back(std::move(section));
            name_offsets.push_back(name_offset);
        }

        const Section& names = sections_[section_names_index];
        if (names.type != kShtStrtab || !range_ok(size_, names.offset, names.size))
            return set_error(error, "embedded bootstrap section names are malformed");
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            std::string name;
            if (!read_string(data_, size_, names.offset, names.size, name_offsets[index], &name))
                return set_error(error, "embedded bootstrap section name is malformed");
            sections_[index].name = std::move(name);
        }

        text_index_ = find_section(kTextSection);
        rodata_index_ = find_section(kRodataSection);
        if (text_index_ == npos() || rodata_index_ == npos())
            return set_error(error, "embedded bootstrap loadable sections are missing");
        const Section& text = sections_[text_index_];
        const Section& rodata = sections_[rodata_index_];
        if (text.type != kShtProgbits ||
            (text.flags & (kShfAlloc | kShfExecInstr)) != (kShfAlloc | kShfExecInstr) ||
            (text.flags & kShfWrite) != 0 || rodata.type != kShtProgbits ||
            (rodata.flags & kShfAlloc) == 0 || (rodata.flags & (kShfWrite | kShfExecInstr)) != 0)
            return set_error(error, "embedded bootstrap section flags are unsafe");
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            const Section& section = sections_[index];
            if (section.size != 0 && (section.flags & kShfAlloc) != 0 && index != text_index_ &&
                index != rodata_index_)
                return set_error(error,
                                 "embedded bootstrap has an unsupported allocatable section");
        }
        if (!align_up(text.size, rodata.alignment, &rodata_output_offset_))
            return set_error(error, "embedded bootstrap section layout overflow");
        if (rodata_output_offset_ > std::numeric_limits<std::size_t>::max() - rodata.size)
            return set_error(error, "embedded bootstrap image size overflow");
        image_size_ = rodata_output_offset_ + rodata.size;
        if (image_size_ == 0)
            return set_error(error, "embedded bootstrap has no loadable bytes");
        sections_[text_index_].output_offset = 0;
        sections_[rodata_index_].output_offset = rodata_output_offset_;

        std::vector<std::size_t> symbol_tables;
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            if (sections_[index].type == kShtSymtab)
                symbol_tables.push_back(index);
        }
        if (symbol_tables.size() != 1)
            return set_error(error, "embedded bootstrap must contain one symbol table");
        symbol_table_index_ = symbol_tables[0];
        const Section& symbol_table = sections_[symbol_table_index_];
        if (symbol_table.entry_size != 24 || symbol_table.size % 24 != 0 ||
            symbol_table.link >= sections_.size())
            return set_error(error, "embedded bootstrap symbol table is malformed");
        const Section& symbol_names = sections_[symbol_table.link];
        if (symbol_names.type != kShtStrtab ||
            !range_ok(size_, symbol_names.offset, symbol_names.size))
            return set_error(error, "embedded bootstrap symbol names are malformed");
        std::uint32_t local_count = 0;
        if (!read_u32(data_, size_, section_offset + (symbol_table_index_ * 64) + 44,
                      &local_count) ||
            local_count > symbol_table.size / 24)
            return set_error(error, "embedded bootstrap symbol table local count is invalid");
        symbols_.clear();
        symbols_.reserve(symbol_table.size / 24);
        for (std::size_t index = 0; index < symbol_table.size / 24; ++index) {
            const std::size_t offset = symbol_table.offset + (index * 24);
            std::uint32_t name_offset = 0;
            std::uint16_t section_index = 0;
            std::uint64_t value = 0;
            if (!read_u32(data_, size_, offset, &name_offset) ||
                !read_u16(data_, size_, offset + 6, &section_index) ||
                !read_u64(data_, size_, offset + 8, &value))
                return set_error(error, "embedded bootstrap symbol is truncated");
            Symbol symbol;
            if (!read_string(data_, size_, symbol_names.offset, symbol_names.size, name_offset,
                             &symbol.name))
                return set_error(error, "embedded bootstrap symbol name is malformed");
            if (section_index != kShnUndef && section_index != kShnAbs &&
                section_index >= sections_.size())
                return set_error(error, "embedded bootstrap symbol section is invalid");
            if (section_index != kShnUndef && section_index != kShnAbs &&
                value > sections_[section_index].size)
                return set_error(error, "embedded bootstrap symbol is outside its section");
            symbol.value = value;
            symbol.section = section_index;
            symbols_.push_back(std::move(symbol));
        }

        relocation_sections_.clear();
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            const Section& section = sections_[index];
            if (section.type == kShtRel)
                return set_error(error, "embedded bootstrap uses unsupported REL relocations");
            if (section.type != kShtRela)
                continue;
            if (section.entry_size != 24 || section.size % 24 != 0 ||
                section.link != symbol_table_index_ || section.info >= sections_.size() ||
                !sections_[section.info].loadable())
                return set_error(error, "embedded bootstrap relocation section is malformed");
            relocation_sections_.push_back(index);
        }
        if (relocation_sections_.empty())
            return set_error(error, "embedded bootstrap has no relocations");
        parsed_ = true;
        return true;
    }

    bool link(std::uint64_t code_address, const std::vector<BootstrapDefinition>& definitions,
              LinkedBootstrap* result, std::string* error) const {
        if (!parsed_ || result == nullptr)
            return set_error(error, "bootstrap object is not parsed");
        for (const Section& section : sections_) {
            if (!section.loadable())
                continue;
            std::uint64_t section_address = 0;
            if (!checked_address_add(code_address, section.output_offset, &section_address) ||
                section_address % section.alignment != 0)
                return set_error(error, "bootstrap section alignment is invalid");
        }
        std::vector<std::uint8_t> output(image_size_, 0);
        for (const Section& section : sections_) {
            if (!section.loadable())
                continue;
            if (!range_ok(size_, section.offset, section.size) ||
                !range_ok(output.size(), section.output_offset, section.size))
                return set_error(error, "bootstrap loadable section is outside its image");
            std::copy(data_ + section.offset, data_ + section.offset + section.size,
                      output.begin() + static_cast<std::ptrdiff_t>(section.output_offset));
        }

        std::unordered_map<std::string, std::uint64_t> definition_map;
        for (const auto& definition : definitions)
            definition_map[std::string(definition.name)] = definition.value;
        auto symbol_value = [&](std::size_t symbol_index, std::uint64_t place,
                                std::uint64_t* value) -> bool {
            if (symbol_index >= symbols_.size())
                return false;
            const Symbol& symbol = symbols_[symbol_index];
            if (symbol.section == kShnUndef) {
                auto it = definition_map.find(symbol.name);
                if (it == definition_map.end())
                    return false;
                *value = it->second;
                return true;
            }
            if (symbol.section == kShnAbs) {
                *value = symbol.value;
                return true;
            }
            if (symbol.section >= sections_.size() || !sections_[symbol.section].loadable())
                return false;
            const Section& section = sections_[symbol.section];
            std::uint64_t section_address = 0;
            std::uint64_t symbol_address = 0;
            if (symbol.value > section.size ||
                !checked_address_add(code_address, section.output_offset, &section_address) ||
                !checked_address_add(section_address, symbol.value, &symbol_address))
                return false;
            *value = symbol_address;
            (void)place;
            return true;
        };
        auto named_value = [&](std::string_view name, std::uint64_t* value) -> bool {
            std::size_t match = npos();
            for (std::size_t index = 0; index < symbols_.size(); ++index) {
                if (symbols_[index].name != name)
                    continue;
                if (match != npos())
                    return false;
                match = index;
            }
            if (match == npos() || symbols_[match].section == kShnUndef)
                return false;
            return symbol_value(match, code_address, value);
        };

        const std::uint64_t text_address = code_address;
        for (const std::size_t relocation_index : relocation_sections_) {
            const Section& relocation = sections_[relocation_index];
            const Section& target_section = sections_[relocation.info];
            if (!target_section.loadable())
                return set_error(error, "bootstrap relocation target is not loadable");
            for (std::size_t index = 0; index < relocation.size / 24; ++index) {
                const std::size_t offset = relocation.offset + (index * 24);
                std::uint64_t target_offset_u64 = 0;
                std::uint64_t info = 0;
                std::int64_t addend = 0;
                if (!read_u64(data_, size_, offset, &target_offset_u64) ||
                    !read_u64(data_, size_, offset + 8, &info) ||
                    !read_i64(data_, size_, offset + 16, &addend) ||
                    target_offset_u64 > std::numeric_limits<std::size_t>::max())
                    return set_error(error, "bootstrap relocation is truncated");
                const std::size_t target_offset = static_cast<std::size_t>(target_offset_u64);
                const std::uint32_t type = static_cast<std::uint32_t>(info);
                const std::size_t symbol_index = static_cast<std::size_t>(info >> 32);
                std::size_t width = 0;
                switch (type) {
                case kRAbs64:
                    width = 8;
                    break;
                case kRAbs32:
                case kRCall26:
                case kRJump26:
                case kRAdrPrelPgHi21:
                case kRAddAbsLo12Nc:
                    width = 4;
                    break;
                default:
                    return set_error(error, "unsupported AArch64 bootstrap relocation");
                }
                if (!range_ok(target_section.size, target_offset, width) ||
                    target_section.output_offset > output.size() ||
                    target_offset > output.size() - target_section.output_offset ||
                    width > output.size() - target_section.output_offset - target_offset)
                    return set_error(error, "bootstrap relocation is outside its section");
                const std::size_t output_offset = target_section.output_offset + target_offset;
                std::uint64_t section_address = 0;
                std::uint64_t place = 0;
                if (!checked_address_add(code_address, target_section.output_offset,
                                         &section_address) ||
                    !checked_address_add(section_address, target_offset, &place))
                    return set_error(error, "bootstrap relocation address overflow");
                std::uint64_t symbol = 0;
                if (!symbol_value(symbol_index, place, &symbol))
                    return set_error(error, "bootstrap relocation references an undefined symbol");
                if (!apply_relocation(&output, output_offset, place, type, symbol, addend, error))
                    return false;
            }
        }

        if (!named_value(kEntrySymbol, &result->entry_address) ||
            !named_value(kReserveWrapperSymbol, &result->reserve_wrapper_address) ||
            !named_value(kStrndupAdapterSymbol, &result->strndup_adapter_address))
            return set_error(error, "bootstrap entry symbols are missing or ambiguous");
        result->data = std::move(output);
        (void)text_address;
        return true;
    }

    [[nodiscard]] std::size_t image_size() const { return image_size_; }

private:
    static constexpr std::size_t npos() { return std::numeric_limits<std::size_t>::max(); }

    [[nodiscard]] std::size_t find_section(std::string_view name) const {
        std::size_t result = npos();
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            if (sections_[index].name != name)
                continue;
            if (result != npos())
                return npos();
            result = index;
        }
        return result;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
    std::vector<std::size_t> relocation_sections_;
    std::size_t text_index_ = npos();
    std::size_t rodata_index_ = npos();
    std::size_t symbol_table_index_ = npos();
    std::size_t rodata_output_offset_ = 0;
    std::size_t image_size_ = 0;
    bool parsed_ = false;
};

}  // namespace

bool link_bootstrap(const BootstrapObjectView& object, std::uint64_t code_address,
                    const std::vector<BootstrapDefinition>& definitions, LinkedBootstrap* linked,
                    std::string* error) {
    ParsedBootstrap parsed;
    if (!parsed.parse(object, error))
        return false;
    return parsed.link(code_address, definitions, linked, error);
}

std::size_t bootstrap_image_size(const BootstrapObjectView& object, std::string* error) {
    ParsedBootstrap parsed;
    if (!parsed.parse(object, error))
        return 0;
    return parsed.image_size();
}

}  // namespace ksud::boot::lkm_image
