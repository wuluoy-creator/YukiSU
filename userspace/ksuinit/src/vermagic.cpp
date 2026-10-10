#include "vermagic.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <elf.h>

namespace ksuinit {

namespace {

constexpr std::string_view kVersionMagicPrefix = "version magic '";
constexpr std::string_view kVersionMagicSeparator = "' should be '";
constexpr std::string_view kVermagicKey = "vermagic=";
constexpr std::string_view kModinfoSectionName = ".modinfo";
constexpr size_t kMaximumVermagicLength = 4096;
constexpr size_t kMaximumSectionAlignment = 4096;

bool checked_add(size_t left, size_t right, size_t& result) {
    if (right > std::numeric_limits<size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_multiply(size_t left, size_t right, size_t& result) {
    if (left != 0 && right > std::numeric_limits<size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool range_is_valid(size_t offset, size_t size, size_t buffer_size) {
    size_t end = 0;
    return checked_add(offset, size, end) && end <= buffer_size;
}

template <typename T>
bool read_object(const std::vector<uint8_t>& buffer, size_t offset, T& value) {
    if (!range_is_valid(offset, sizeof(T), buffer.size())) {
        return false;
    }
    std::memcpy(&value, buffer.data() + offset, sizeof(T));
    return true;
}

template <typename T>
void write_object(std::vector<uint8_t>& buffer, size_t offset, const T& value) {
    std::memcpy(buffer.data() + offset, &value, sizeof(T));
}

bool to_size(uint64_t value, size_t& result) {
    if (value > std::numeric_limits<size_t>::max()) {
        return false;
    }
    result = static_cast<size_t>(value);
    return true;
}

bool align_up(size_t value, size_t alignment, size_t& result) {
    if (alignment == 0) {
        alignment = 1;
    }
    if ((alignment & (alignment - 1)) != 0) {
        return false;
    }

    size_t with_padding = 0;
    if (!checked_add(value, alignment - 1, with_padding)) {
        return false;
    }
    result = with_padding & ~(alignment - 1);
    return true;
}

bool is_valid_vermagic(std::string_view vermagic) {
    if (vermagic.empty() || vermagic.size() > kMaximumVermagicLength) {
        return false;
    }

    return std::none_of(vermagic.begin(), vermagic.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20 || byte > 0x7e || character == '\'';
    });
}

bool is_decimal(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

bool has_kernel_facility(std::string_view metadata) {
    const size_t priority_end = metadata.find(',');
    const size_t sequence_end = priority_end == std::string_view::npos
                                    ? priority_end
                                    : metadata.find(',', priority_end + 1);
    const size_t timestamp_end = sequence_end == std::string_view::npos
                                     ? sequence_end
                                     : metadata.find(',', sequence_end + 1);
    if (priority_end == std::string_view::npos || sequence_end == std::string_view::npos ||
        timestamp_end == std::string_view::npos) {
        return false;
    }

    const std::string_view priority_field = metadata.substr(0, priority_end);
    const std::string_view sequence_field =
        metadata.substr(priority_end + 1, sequence_end - priority_end - 1);
    const std::string_view timestamp_field =
        metadata.substr(sequence_end + 1, timestamp_end - sequence_end - 1);
    const size_t flags_start = timestamp_end + 1;
    const size_t flags_end = metadata.find(',', flags_start);
    const std::string_view flags_field =
        flags_end == std::string_view::npos ? metadata.substr(flags_start)
                                            : metadata.substr(flags_start, flags_end - flags_start);
    if (!is_decimal(priority_field) || !is_decimal(sequence_field) ||
        !is_decimal(timestamp_field) || flags_field.empty()) {
        return false;
    }

    unsigned int priority = 0;
    for (const char character : priority_field) {
        priority = priority * 10 + static_cast<unsigned int>(character - '0');
        if (priority > 191) {
            return false;
        }
    }
    return priority / 8 == 0;
}

bool section_name_equals(const std::vector<uint8_t>& module, const Elf64_Shdr& string_table,
                         Elf64_Word name_offset, std::string_view expected) {
    size_t table_offset = 0;
    size_t table_size = 0;
    if (!to_size(string_table.sh_offset, table_offset) ||
        !to_size(string_table.sh_size, table_size) || name_offset >= table_size) {
        return false;
    }

    const size_t name_position = table_offset + name_offset;
    const size_t remaining = table_size - name_offset;
    const auto* name = reinterpret_cast<const char*>(module.data() + name_position);
    const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', remaining));
    if (terminator == nullptr) {
        return false;
    }

    return std::string_view(name, static_cast<size_t>(terminator - name)) == expected;
}

}  // namespace

bool extract_vermagic_mismatch(std::string_view kmsg, VermagicMismatch& mismatch) {
    size_t line_end = kmsg.size();

    while (line_end > 0) {
        if (kmsg[line_end - 1] == '\n') {
            --line_end;
            continue;
        }

        const size_t newline = kmsg.rfind('\n', line_end - 1);
        const size_t line_start = newline == std::string_view::npos ? 0 : newline + 1;
        std::string_view record = kmsg.substr(line_start, line_end - line_start);
        if (!record.empty() && record.back() == '\r') {
            record.remove_suffix(1);
        }

        const size_t metadata_end = record.find(';');
        if (metadata_end == std::string_view::npos ||
            !has_kernel_facility(record.substr(0, metadata_end))) {
            if (line_start == 0) {
                break;
            }
            line_end = line_start - 1;
            continue;
        }
        const std::string_view message =
            metadata_end == std::string_view::npos ? record : record.substr(metadata_end + 1);

        const size_t prefix_position = message.find(kVersionMagicPrefix);
        if (prefix_position != std::string_view::npos && prefix_position >= 2 &&
            message.substr(prefix_position - 2, 2) == ": ") {
            const size_t value_start = prefix_position + kVersionMagicPrefix.size();
            const size_t separator_position = message.find(kVersionMagicSeparator, value_start);
            if (separator_position != std::string_view::npos) {
                const std::string_view reported =
                    message.substr(value_start, separator_position - value_start);
                const size_t required_start = separator_position + kVersionMagicSeparator.size();
                const size_t required_end = message.find('\'', required_start);
                if (required_end == message.size() - 1) {
                    const std::string_view candidate =
                        message.substr(required_start, required_end - required_start);
                    if (is_valid_vermagic(reported) && is_valid_vermagic(candidate)) {
                        mismatch.module_vermagic.assign(reported);
                        mismatch.required_vermagic.assign(candidate);
                        return true;
                    }
                }
            }
        }

        if (line_start == 0) {
            break;
        }
        line_end = line_start - 1;
    }

    return false;
}

bool replace_module_vermagic(std::vector<uint8_t>& module, std::string_view reported_vermagic,
                             std::string_view required_vermagic, std::string& error) {
    error.clear();
    if (!is_valid_vermagic(reported_vermagic) || !is_valid_vermagic(required_vermagic)) {
        error = "invalid reported or required vermagic";
        return false;
    }
    if (reported_vermagic == required_vermagic) {
        error = "reported and required vermagic are identical";
        return false;
    }

    Elf64_Ehdr elf_header{};
    if (!read_object(module, 0, elf_header)) {
        error = "module is too small for an ELF header";
        return false;
    }
    if (std::memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0) {
        error = "module has invalid ELF magic";
        return false;
    }
    if (elf_header.e_ident[EI_CLASS] != ELFCLASS64) {
        error = "module is not ELF64";
        return false;
    }
    if (elf_header.e_ident[EI_DATA] != ELFDATA2LSB) {
        error = "module is not little-endian";
        return false;
    }
    if (elf_header.e_ident[EI_VERSION] != EV_CURRENT || elf_header.e_version != EV_CURRENT ||
        elf_header.e_ehsize != sizeof(Elf64_Ehdr)) {
        error = "module has an unsupported ELF version";
        return false;
    }
    if (elf_header.e_type != ET_REL || elf_header.e_machine != EM_AARCH64) {
        error = "module is not an AArch64 relocatable object";
        return false;
    }
    if (elf_header.e_shentsize != sizeof(Elf64_Shdr)) {
        error = "module has an unsupported section-header size";
        return false;
    }
    if (elf_header.e_shnum == 0 || elf_header.e_shstrndx == SHN_UNDEF ||
        elf_header.e_shstrndx == SHN_XINDEX || elf_header.e_shstrndx >= elf_header.e_shnum) {
        error = "module has an unsupported section table";
        return false;
    }

    size_t section_table_offset = 0;
    size_t section_table_size = 0;
    if (!to_size(elf_header.e_shoff, section_table_offset) ||
        !checked_multiply(elf_header.e_shnum, sizeof(Elf64_Shdr), section_table_size) ||
        !range_is_valid(section_table_offset, section_table_size, module.size())) {
        error = "module section table is outside the image";
        return false;
    }

    size_t section_name_header_delta = 0;
    size_t section_name_header_offset = 0;
    if (!checked_multiply(elf_header.e_shstrndx, sizeof(Elf64_Shdr), section_name_header_delta) ||
        !checked_add(section_table_offset, section_name_header_delta, section_name_header_offset)) {
        error = "module section-name table offset overflow";
        return false;
    }

    Elf64_Shdr section_name_table{};
    if (!read_object(module, section_name_header_offset, section_name_table)) {
        error = "module section-name table header is outside the image";
        return false;
    }

    size_t section_name_table_offset = 0;
    size_t section_name_table_size = 0;
    if (section_name_table.sh_type != SHT_STRTAB ||
        !to_size(section_name_table.sh_offset, section_name_table_offset) ||
        !to_size(section_name_table.sh_size, section_name_table_size) ||
        !range_is_valid(section_name_table_offset, section_name_table_size, module.size())) {
        error = "module section-name table is invalid";
        return false;
    }

    Elf64_Shdr modinfo{};
    size_t modinfo_header_offset = 0;
    size_t modinfo_sections = 0;
    for (size_t index = 0; index < elf_header.e_shnum; ++index) {
        size_t header_delta = 0;
        size_t header_offset = 0;
        Elf64_Shdr section{};
        if (!checked_multiply(index, sizeof(Elf64_Shdr), header_delta) ||
            !checked_add(section_table_offset, header_delta, header_offset) ||
            !read_object(module, header_offset, section)) {
            error = "module section header is outside the image";
            return false;
        }

        if (section_name_equals(module, section_name_table, section.sh_name, kModinfoSectionName)) {
            ++modinfo_sections;
            if (modinfo_sections != 1) {
                error = "module has multiple .modinfo sections";
                return false;
            }
            if (section.sh_type != SHT_PROGBITS) {
                error = "module .modinfo section has an unsupported type";
                return false;
            }
            modinfo = section;
            modinfo_header_offset = header_offset;
        }
    }
    if (modinfo_sections == 0) {
        error = "module has no .modinfo section";
        return false;
    }

    size_t modinfo_offset = 0;
    size_t modinfo_size = 0;
    if (!to_size(modinfo.sh_offset, modinfo_offset) || !to_size(modinfo.sh_size, modinfo_size) ||
        modinfo_size == 0 || !range_is_valid(modinfo_offset, modinfo_size, module.size())) {
        error = "module .modinfo section is outside the image";
        return false;
    }

    size_t alignment = 0;
    if (!to_size(modinfo.sh_addralign, alignment) || alignment > kMaximumSectionAlignment) {
        error = "module .modinfo alignment is too large";
        return false;
    }
    if (alignment == 0) {
        alignment = 1;
    }
    if ((alignment & (alignment - 1)) != 0 || modinfo_offset % alignment != 0) {
        error = "module .modinfo alignment is invalid";
        return false;
    }

    const std::string replacement = std::string(kVermagicKey) + std::string(required_vermagic);
    size_t vermagic_entries = 0;
    size_t vermagic_entry_offset = 0;
    size_t vermagic_entry_size = 0;
    size_t entry_start = modinfo_offset;
    const size_t modinfo_end = modinfo_offset + modinfo_size;
    if (module[modinfo_end - 1] != 0) {
        error = "module .modinfo section is not NUL-terminated";
        return false;
    }
    while (entry_start < modinfo_end) {
        const auto* entry = module.data() + entry_start;
        const size_t remaining = modinfo_end - entry_start;
        const auto* terminator = static_cast<const uint8_t*>(std::memchr(entry, '\0', remaining));
        const size_t entry_size =
            terminator == nullptr ? remaining : static_cast<size_t>(terminator - entry);

        if (entry_size != 0) {
            const std::string_view entry_view(reinterpret_cast<const char*>(entry), entry_size);
            if (entry_view.substr(0, kVermagicKey.size()) == kVermagicKey) {
                ++vermagic_entries;
                const std::string_view existing_vermagic = entry_view.substr(kVermagicKey.size());
                if (existing_vermagic != reported_vermagic) {
                    error = "kernel-reported vermagic does not match module .modinfo";
                    return false;
                }
                if (vermagic_entries == 1) {
                    vermagic_entry_offset = entry_start;
                    vermagic_entry_size = entry_size;
                }
            }
        }

        if (terminator == nullptr) {
            break;
        }
        entry_start += entry_size + 1;
    }

    if (vermagic_entries == 0) {
        error = "module .modinfo has no vermagic entry";
        return false;
    }
    if (vermagic_entries != 1) {
        error = "module .modinfo has multiple vermagic entries";
        return false;
    }

    const size_t entry_end = vermagic_entry_offset + vermagic_entry_size + 1;
    size_t rebuilt_size = modinfo_size - (vermagic_entry_size + 1);
    if (!checked_add(rebuilt_size, replacement.size() + 1, rebuilt_size)) {
        error = "rebuilt .modinfo size overflow";
        return false;
    }

    std::vector<uint8_t> rebuilt_modinfo;
    rebuilt_modinfo.reserve(rebuilt_size);
    rebuilt_modinfo.insert(rebuilt_modinfo.end(), module.begin() + modinfo_offset,
                           module.begin() + vermagic_entry_offset);
    rebuilt_modinfo.insert(rebuilt_modinfo.end(), replacement.begin(), replacement.end());
    rebuilt_modinfo.push_back(0);
    rebuilt_modinfo.insert(rebuilt_modinfo.end(), module.begin() + entry_end,
                           module.begin() + modinfo_end);

    size_t new_offset = 0;
    if (!align_up(module.size(), alignment, new_offset)) {
        error = "module .modinfo alignment is invalid";
        return false;
    }
    size_t final_size = 0;
    if (!checked_add(new_offset, rebuilt_modinfo.size(), final_size) ||
        final_size > module.max_size() ||
        rebuilt_modinfo.size() > std::numeric_limits<Elf64_Xword>::max() ||
        new_offset > std::numeric_limits<Elf64_Off>::max()) {
        error = "rebuilt .modinfo does not fit ELF64";
        return false;
    }

    module.resize(new_offset, 0);
    module.insert(module.end(), rebuilt_modinfo.begin(), rebuilt_modinfo.end());

    modinfo.sh_offset = static_cast<Elf64_Off>(new_offset);
    modinfo.sh_size = static_cast<Elf64_Xword>(rebuilt_modinfo.size());
    write_object(module, modinfo_header_offset, modinfo);
    return true;
}

}  // namespace ksuinit
