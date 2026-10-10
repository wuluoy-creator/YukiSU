#include "boot_image_btf.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace ksud::boot::lkm_image {
namespace {

constexpr std::uint16_t kBtfMagic = 0xeb9f;
constexpr std::uint8_t kBtfVersion = 1;
constexpr std::size_t kBtfHeaderSize = 24;
constexpr std::array<std::uint8_t, 4> kBtfMagicBytes = {0x9f, 0xeb, 0x01, 0x00};
constexpr std::uint64_t kArm64PointerSize = 8;

constexpr std::uint8_t kBtfKindInt = 1;
constexpr std::uint8_t kBtfKindPtr = 2;
constexpr std::uint8_t kBtfKindArray = 3;
constexpr std::uint8_t kBtfKindStruct = 4;
constexpr std::uint8_t kBtfKindUnion = 5;
constexpr std::uint8_t kBtfKindEnum = 6;
constexpr std::uint8_t kBtfKindFwd = 7;
constexpr std::uint8_t kBtfKindTypedef = 8;
constexpr std::uint8_t kBtfKindVolatile = 9;
constexpr std::uint8_t kBtfKindConst = 10;
constexpr std::uint8_t kBtfKindRestrict = 11;
constexpr std::uint8_t kBtfKindFunc = 12;
constexpr std::uint8_t kBtfKindFuncProto = 13;
constexpr std::uint8_t kBtfKindVar = 14;
constexpr std::uint8_t kBtfKindDataSec = 15;
constexpr std::uint8_t kBtfKindFloat = 16;
constexpr std::uint8_t kBtfKindDeclTag = 17;
constexpr std::uint8_t kBtfKindTypeTag = 18;
constexpr std::uint8_t kBtfKindEnum64 = 19;

struct BtfType {
    std::uint8_t kind;
    std::uint16_t vlen;
    bool kind_flag;
    std::uint32_t name_offset;
    std::uint32_t size_or_type;
    std::size_t payload_offset;
};

bool checked_add(std::size_t left, std::size_t right, std::size_t& result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_mul(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

std::optional<std::uint16_t> read_u16(const std::uint8_t* data, std::size_t size,
                                      std::size_t offset) {
    std::size_t end = 0;
    if (data == nullptr || !checked_add(offset, 2, end) || end > size) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset]) |
                                      static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::optional<std::uint32_t> read_u32(const std::uint8_t* data, std::size_t size,
                                      std::size_t offset) {
    std::size_t end = 0;
    if (data == nullptr || !checked_add(offset, 4, end) || end > size) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(data[offset]) |
           static_cast<std::uint32_t>(data[offset + 1]) << 8 |
           static_cast<std::uint32_t>(data[offset + 2]) << 16 |
           static_cast<std::uint32_t>(data[offset + 3]) << 24;
}

bool ranges_overlap(std::size_t left_start, std::size_t left_end, std::size_t right_start,
                    std::size_t right_end) {
    return std::max(left_start, right_start) < std::min(left_end, right_end);
}

bool valid_utf8(std::string_view value) {
    const auto* data = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = data[index++];
        if (first <= 0x7f) {
            continue;
        }

        std::size_t continuation_count = 0;
        std::uint32_t codepoint = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation_count = 1;
            codepoint = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation_count = 2;
            codepoint = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation_count = 3;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (continuation_count > value.size() - index) {
            return false;
        }
        for (std::size_t count = 0; count < continuation_count; ++count) {
            const unsigned char next = data[index++];
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            codepoint = codepoint << 6 | (next & 0x3f);
        }
        if ((continuation_count == 2 && codepoint < 0x800) ||
            (continuation_count == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> payload_size(std::uint8_t kind, std::uint16_t vlen) {
    const auto count = static_cast<std::size_t>(vlen);
    switch (kind) {
    case kBtfKindInt:
    case kBtfKindVar:
    case kBtfKindDeclTag:
        return 4;
    case kBtfKindPtr:
    case kBtfKindFwd:
    case kBtfKindTypedef:
    case kBtfKindVolatile:
    case kBtfKindConst:
    case kBtfKindRestrict:
    case kBtfKindFunc:
    case kBtfKindFloat:
    case kBtfKindTypeTag:
        return 0;
    case kBtfKindArray:
        return 12;
    case kBtfKindStruct:
    case kBtfKindUnion:
    case kBtfKindDataSec:
    case kBtfKindEnum64: {
        std::size_t size = 0;
        return checked_mul(count, 12, size) ? std::optional<std::size_t>(size) : std::nullopt;
    }
    case kBtfKindEnum:
    case kBtfKindFuncProto: {
        std::size_t size = 0;
        return checked_mul(count, 8, size) ? std::optional<std::size_t>(size) : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

class ParsedBtf {
public:
    [[nodiscard]] static std::optional<ParsedBtf> parse(const std::uint8_t* data,
                                                        std::size_t data_size,
                                                        std::size_t file_offset) {
        const auto magic = read_u16(data, data_size, file_offset);
        std::size_t version_offset = 0;
        std::size_t flags_offset = 0;
        std::size_t header_length_offset = 0;
        if (!magic || !checked_add(file_offset, 2, version_offset) ||
            !checked_add(file_offset, 3, flags_offset) ||
            !checked_add(file_offset, 4, header_length_offset) || version_offset >= data_size ||
            flags_offset >= data_size) {
            return std::nullopt;
        }
        const auto header_length = read_u32(data, data_size, header_length_offset);
        if (*magic != kBtfMagic || data[version_offset] != kBtfVersion || data[flags_offset] != 0 ||
            !header_length || *header_length < kBtfHeaderSize) {
            return std::nullopt;
        }

        std::size_t header_end = 0;
        std::size_t type_offset_field = 0;
        std::size_t type_length_field = 0;
        std::size_t strings_offset_field = 0;
        std::size_t strings_length_field = 0;
        if (!checked_add(file_offset, *header_length, header_end) ||
            !checked_add(file_offset, 8, type_offset_field) ||
            !checked_add(file_offset, 12, type_length_field) ||
            !checked_add(file_offset, 16, strings_offset_field) ||
            !checked_add(file_offset, 20, strings_length_field)) {
            return std::nullopt;
        }
        const auto type_offset = read_u32(data, data_size, type_offset_field);
        const auto type_length = read_u32(data, data_size, type_length_field);
        const auto strings_offset = read_u32(data, data_size, strings_offset_field);
        const auto strings_length = read_u32(data, data_size, strings_length_field);
        if (!type_offset || !type_length || !strings_offset || !strings_length) {
            return std::nullopt;
        }

        std::size_t types_start = 0;
        std::size_t types_end = 0;
        std::size_t strings_start = 0;
        std::size_t strings_end = 0;
        if (!checked_add(header_end, *type_offset, types_start) ||
            !checked_add(types_start, *type_length, types_end) ||
            !checked_add(header_end, *strings_offset, strings_start) ||
            !checked_add(strings_start, *strings_length, strings_end) || header_end > data_size ||
            types_start < header_end || strings_start < header_end || types_end > data_size ||
            strings_end > data_size || *strings_length == 0 ||
            ranges_overlap(types_start, types_end, strings_start, strings_end) ||
            data[strings_start] != 0 || data[strings_end - 1] != 0) {
            return std::nullopt;
        }

        ParsedBtf parsed(data, data_size, file_offset, std::max(types_end, strings_end),
                         strings_start, strings_end);
        std::size_t position = types_start;
        while (position < types_end) {
            std::size_t type_header_end = 0;
            std::size_t info_offset = 0;
            std::size_t size_or_type_offset = 0;
            if (!checked_add(position, 12, type_header_end) || type_header_end > types_end ||
                !checked_add(position, 4, info_offset) ||
                !checked_add(position, 8, size_or_type_offset)) {
                return std::nullopt;
            }
            const auto name_offset = read_u32(data, data_size, position);
            const auto info = read_u32(data, data_size, info_offset);
            const auto size_or_type = read_u32(data, data_size, size_or_type_offset);
            if (!name_offset || !info || !size_or_type) {
                return std::nullopt;
            }
            const auto kind = static_cast<std::uint8_t>((*info >> 24) & 0x1f);
            const auto vlen = static_cast<std::uint16_t>(*info & 0xffff);
            const auto payload_length = payload_size(kind, vlen);
            std::size_t next = 0;
            if (!payload_length || !checked_add(type_header_end, *payload_length, next) ||
                next > types_end || !parsed.string(*name_offset)) {
                return std::nullopt;
            }
            parsed.types_.push_back(
                {kind, vlen, (*info >> 31) != 0, *name_offset, *size_or_type, type_header_end});
            position = next;
        }
        if (position != types_end || !parsed.validate_references()) {
            return std::nullopt;
        }
        return parsed;
    }

    [[nodiscard]] KernelBtf analyze(std::string& error) const {
        KernelBtf result{file_offset_, blob_end_ - file_offset_, types_.size(), std::nullopt};
        if (!load_info_layout(result.load_info, error)) {
            return result;
        }
        if (!validate_function_abis(error)) {
            return result;
        }
        return result;
    }

private:
    ParsedBtf(const std::uint8_t* data, std::size_t data_size, std::size_t file_offset,
              std::size_t blob_end, std::size_t strings_start, std::size_t strings_end)
        : data_(data),
          data_size_(data_size),
          file_offset_(file_offset),
          blob_end_(blob_end),
          strings_start_(strings_start),
          strings_end_(strings_end) {}

    [[nodiscard]] std::optional<std::string_view> string(std::uint32_t offset) const {
        std::size_t start = 0;
        if (!checked_add(strings_start_, static_cast<std::size_t>(offset), start) ||
            start >= strings_end_) {
            return std::nullopt;
        }
        std::size_t end = start;
        while (end < strings_end_ && data_[end] != 0) {
            ++end;
        }
        if (end == strings_end_) {
            return std::nullopt;
        }
        std::string_view value(reinterpret_cast<const char*>(data_ + start), end - start);
        return valid_utf8(value) ? std::optional<std::string_view>(value) : std::nullopt;
    }

    [[nodiscard]] const BtfType* type_by_id(std::uint32_t type_id) const {
        if (type_id == 0 || type_id > types_.size()) {
            return nullptr;
        }
        return &types_[static_cast<std::size_t>(type_id) - 1];
    }

    [[nodiscard]] bool valid_type_id(std::uint32_t type_id) const {
        return type_id == 0 || type_id <= types_.size();
    }

    [[nodiscard]] bool validate_references() const {
        for (const auto& type : types_) {
            if (!string(type.name_offset)) {
                return false;
            }
            switch (type.kind) {
            case kBtfKindPtr:
            case kBtfKindTypedef:
            case kBtfKindVolatile:
            case kBtfKindConst:
            case kBtfKindRestrict:
            case kBtfKindFunc:
            case kBtfKindFuncProto:
            case kBtfKindVar:
            case kBtfKindDeclTag:
            case kBtfKindTypeTag:
                if (!valid_type_id(type.size_or_type)) {
                    return false;
                }
                break;
            default:
                break;
            }

            if (type.kind == kBtfKindArray) {
                const auto element_type = read_u32(data_, data_size_, type.payload_offset);
                const auto index_type = read_u32(data_, data_size_, type.payload_offset + 4);
                if (!element_type || !index_type || !valid_type_id(*element_type) ||
                    !valid_type_id(*index_type)) {
                    return false;
                }
            } else if (type.kind == kBtfKindStruct || type.kind == kBtfKindUnion) {
                for (std::size_t index = 0; index < type.vlen; ++index) {
                    const std::size_t offset = type.payload_offset + (index * 12);
                    const auto name = read_u32(data_, data_size_, offset);
                    const auto member_type = read_u32(data_, data_size_, offset + 4);
                    if (!name || !member_type || !string(*name) || !valid_type_id(*member_type)) {
                        return false;
                    }
                }
            } else if (type.kind == kBtfKindEnum) {
                for (std::size_t index = 0; index < type.vlen; ++index) {
                    const auto name =
                        read_u32(data_, data_size_, type.payload_offset + (index * 8));
                    if (!name || !string(*name)) {
                        return false;
                    }
                }
            } else if (type.kind == kBtfKindFunc) {
                const auto* prototype = type_by_id(type.size_or_type);
                if (prototype == nullptr || prototype->kind != kBtfKindFuncProto) {
                    return false;
                }
            } else if (type.kind == kBtfKindFuncProto) {
                for (std::size_t index = 0; index < type.vlen; ++index) {
                    const std::size_t offset = type.payload_offset + (index * 8);
                    const auto name = read_u32(data_, data_size_, offset);
                    const auto parameter_type = read_u32(data_, data_size_, offset + 4);
                    if (!name || !parameter_type || !string(*name) ||
                        !valid_type_id(*parameter_type)) {
                        return false;
                    }
                }
            } else if (type.kind == kBtfKindDataSec) {
                for (std::size_t index = 0; index < type.vlen; ++index) {
                    const auto type_id =
                        read_u32(data_, data_size_, type.payload_offset + (index * 12));
                    const auto* variable = type_id ? type_by_id(*type_id) : nullptr;
                    if (variable == nullptr || variable->kind != kBtfKindVar) {
                        return false;
                    }
                }
            } else if (type.kind == kBtfKindEnum64) {
                for (std::size_t index = 0; index < type.vlen; ++index) {
                    const auto name =
                        read_u32(data_, data_size_, type.payload_offset + (index * 12));
                    if (!name || !string(*name)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<std::uint32_t> strip_modifiers(std::uint32_t type_id) const {
        for (std::size_t depth = 0; depth < types_.size(); ++depth) {
            const auto* type = type_by_id(type_id);
            if (type == nullptr) {
                return type_id == 0 ? std::optional<std::uint32_t>(0) : std::nullopt;
            }
            switch (type->kind) {
            case kBtfKindTypedef:
            case kBtfKindVolatile:
            case kBtfKindConst:
            case kBtfKindRestrict:
            case kBtfKindTypeTag:
                type_id = type->size_or_type;
                break;
            default:
                return type_id;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> type_size(std::uint32_t type_id,
                                                         std::size_t depth = 0) const {
        if (depth > 64) {
            return std::nullopt;
        }
        const auto stripped = strip_modifiers(type_id);
        const auto* type = stripped ? type_by_id(*stripped) : nullptr;
        if (type == nullptr) {
            return std::nullopt;
        }
        switch (type->kind) {
        case kBtfKindInt:
        case kBtfKindStruct:
        case kBtfKindUnion:
        case kBtfKindEnum:
        case kBtfKindFloat:
        case kBtfKindEnum64:
            return type->size_or_type;
        case kBtfKindPtr:
            return kArm64PointerSize;
        case kBtfKindArray: {
            const auto element_type = read_u32(data_, data_size_, type->payload_offset);
            const auto count = read_u32(data_, data_size_, type->payload_offset + 8);
            const auto element_size =
                element_type ? type_size(*element_type, depth + 1) : std::nullopt;
            if (!element_size || !count ||
                (*count != 0 &&
                 *element_size > std::numeric_limits<std::uint64_t>::max() / *count)) {
                return std::nullopt;
            }
            return *element_size * *count;
        }
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] bool points_to_named_struct(std::uint32_t type_id,
                                              std::string_view expected_name) const {
        const auto pointer_id = strip_modifiers(type_id);
        const auto* pointer = pointer_id ? type_by_id(*pointer_id) : nullptr;
        if (pointer == nullptr || pointer->kind != kBtfKindPtr) {
            return false;
        }
        const auto pointee_id = strip_modifiers(pointer->size_or_type);
        const auto* pointee = pointee_id ? type_by_id(*pointee_id) : nullptr;
        return pointee != nullptr && pointee->kind == kBtfKindStruct &&
               string(pointee->name_offset) == expected_name;
    }

    [[nodiscard]] std::optional<std::uint64_t> member_offset(const BtfType& structure,
                                                             std::size_t member_index) const {
        const auto raw =
            read_u32(data_, data_size_, structure.payload_offset + (member_index * 12) + 8);
        if (!raw || (structure.kind_flag && (*raw >> 24) != 0)) {
            return std::nullopt;
        }
        const auto bit_offset = structure.kind_flag ? *raw & 0x00ff'ffff : *raw;
        return bit_offset % 8 == 0
                   ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bit_offset / 8))
                   : std::nullopt;
    }

    [[nodiscard]] bool load_info_layout(std::optional<LoadInfoLayout>& result,
                                        std::string& error) const {
        std::size_t named_structs = 0;
        std::set<LoadInfoLayout> layouts;
        for (const auto& structure : types_) {
            if (structure.kind != kBtfKindStruct ||
                string(structure.name_offset) != std::string_view("load_info")) {
                continue;
            }
            ++named_structs;
            std::optional<std::uint64_t> hdr;
            std::optional<std::uint64_t> len;
            for (std::size_t index = 0; index < structure.vlen; ++index) {
                const std::size_t offset = structure.payload_offset + (index * 12);
                const auto name_offset = read_u32(data_, data_size_, offset);
                const auto name = name_offset ? string(*name_offset) : std::nullopt;
                if (!name || (*name != "hdr" && *name != "len")) {
                    continue;
                }
                const auto member_type = read_u32(data_, data_size_, offset + 4);
                if (!member_type) {
                    error = "BTF load_info member type is truncated";
                    return false;
                }
                if (type_size(*member_type) != kArm64PointerSize) {
                    error = "BTF struct load_info." + std::string(*name) + " is not eight bytes";
                    return false;
                }
                const auto byte_offset = member_offset(structure, index);
                if (!byte_offset) {
                    error = "BTF struct load_info." + std::string(*name) + " is not byte-aligned";
                    return false;
                }
                if (*name == "hdr") {
                    hdr = byte_offset;
                } else {
                    len = byte_offset;
                }
            }
            if (!hdr || !len) {
                continue;
            }

            const std::uint64_t structure_size = structure.size_or_type;
            if (*hdr > structure_size || structure_size - *hdr < kArm64PointerSize ||
                *len > structure_size || structure_size - *len < kArm64PointerSize) {
                error = "BTF struct load_info fields exceed the structure size";
                return false;
            }
            layouts.insert({structure_size, *hdr, *len});
        }

        if (named_structs == 0) {
            result = std::nullopt;
            return true;
        }
        if (layouts.empty()) {
            error = "BTF contains struct load_info without usable hdr/len members";
            return false;
        }
        if (layouts.size() != 1) {
            error = "BTF contains conflicting struct load_info layouts";
            return false;
        }
        result = *layouts.begin();
        return true;
    }

    [[nodiscard]] bool validate_function_abis(std::string& error) const {
        constexpr std::array<std::pair<std::string_view, std::uint16_t>, 7> expected = {{
            {"load_module", std::uint16_t{3}},
            {"vmalloc", std::uint16_t{1}},
            {"vmalloc_noprof", std::uint16_t{1}},
            {"memcpy", std::uint16_t{3}},
            {"kstrdup", std::uint16_t{2}},
            {"strndup_user", std::uint16_t{2}},
            {"memblock_reserve", std::uint16_t{2}},
        }};

        for (const auto& [name, expected_arity] : expected) {
            for (const auto& function : types_) {
                if (function.kind != kBtfKindFunc || string(function.name_offset) != name) {
                    continue;
                }
                const auto* prototype = type_by_id(function.size_or_type);
                if (prototype == nullptr) {
                    error = "BTF function " + std::string(name) + " has no prototype";
                    return false;
                }
                if (prototype->kind != kBtfKindFuncProto || prototype->vlen != expected_arity) {
                    error = "BTF function " + std::string(name) + " has " +
                            std::to_string(prototype->vlen) + " parameters; expected " +
                            std::to_string(expected_arity);
                    return false;
                }
                if (name == "load_module") {
                    const auto first_parameter =
                        read_u32(data_, data_size_, prototype->payload_offset + 4);
                    if (!first_parameter ||
                        !points_to_named_struct(*first_parameter, "load_info")) {
                        error = "BTF load_module first parameter is not struct load_info *";
                        return false;
                    }
                }
            }
        }
        return true;
    }

    const std::uint8_t* data_;
    std::size_t data_size_;
    std::size_t file_offset_;
    std::size_t blob_end_;
    std::size_t strings_start_;
    std::size_t strings_end_;
    std::vector<BtfType> types_;
};

}  // namespace

bool LoadInfoLayout::operator==(const LoadInfoLayout& other) const {
    return structure_size == other.structure_size && hdr_offset == other.hdr_offset &&
           len_offset == other.len_offset;
}

bool LoadInfoLayout::operator<(const LoadInfoLayout& other) const {
    if (structure_size != other.structure_size) {
        return structure_size < other.structure_size;
    }
    if (hdr_offset != other.hdr_offset) {
        return hdr_offset < other.hdr_offset;
    }
    return len_offset < other.len_offset;
}

BtfCandidate::BtfCandidate(KernelBtf result, std::string error)
    : result_(result), error_(std::move(error)) {}

std::size_t BtfCandidate::file_offset() const {
    return result_.file_offset;
}

std::size_t BtfCandidate::size() const {
    return result_.size;
}

std::size_t BtfCandidate::type_count() const {
    return result_.type_count;
}

bool BtfCandidate::inspect(KernelBtf& result, std::string* error) const {
    if (!error_.empty()) {
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }
    result = result_;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::vector<BtfCandidate> find_btf_candidates(const std::uint8_t* image, std::size_t image_size) {
    std::vector<BtfCandidate> candidates;
    if (image == nullptr || image_size < kBtfMagicBytes.size()) {
        return candidates;
    }

    std::size_t search_from = 0;
    while (search_from <= image_size - kBtfMagicBytes.size()) {
        std::size_t file_offset = image_size;
        for (std::size_t offset = search_from; offset <= image_size - kBtfMagicBytes.size();
             ++offset) {
            if (std::equal(kBtfMagicBytes.begin(), kBtfMagicBytes.end(), image + offset)) {
                file_offset = offset;
                break;
            }
        }
        if (file_offset == image_size) {
            break;
        }

        auto parsed = ParsedBtf::parse(image, image_size, file_offset);
        if (parsed) {
            std::string error;
            auto result = parsed->analyze(error);
            candidates.push_back(BtfCandidate(result, std::move(error)));
        }
        if (file_offset == std::numeric_limits<std::size_t>::max()) {
            break;
        }
        search_from = file_offset + 1;
    }
    return candidates;
}

}  // namespace ksud::boot::lkm_image
