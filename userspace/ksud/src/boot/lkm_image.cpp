#include "lkm_image.hpp"

#include "../utils.hpp"
#include "boot_image_btf.hpp"
#include "lkm_image_bootstrap_linker.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

#include <mbedtls/sha256.h>

namespace ksud::boot::lkm_image {
namespace {

constexpr std::size_t kImageSizeOffset = 0x10;
constexpr std::array<std::uint8_t, 8> kCapsuleMagic = {'K', 'S', 'U', 'L', 'K', 'M', '1', 0};
constexpr std::uint32_t kCapsuleVersion = 1;
constexpr std::size_t kCapsuleAlignment = 4096;
constexpr std::size_t kCapsuleHeaderSize = 96;
constexpr std::size_t kCapsuleFixupEntrySize = 16;
constexpr std::uint64_t kCapsuleFixupFlag = 1;
constexpr std::uint64_t kCapsuleRestoreFlag = 2;
constexpr std::array<std::uint8_t, 8> kRestoreMetadataMagic = {
    'K', 'S', 'U', 'R', 'S', 'T', '2', 0,
};
constexpr std::uint32_t kRestoreMetadataVersion = 1;
constexpr std::size_t kRestoreMetadataHeaderSize = 48;
constexpr std::size_t kRestoreRecordHeaderSize = 16;
constexpr std::size_t kMinimumLoadInfoStorageSize = 256;
constexpr std::size_t kMaximumLoadInfoStorageSize = 4096;
constexpr std::size_t kTextCaveAlignment = 16;
constexpr std::size_t kTextCavePreferredAlignment = 4096;

constexpr std::uint32_t kShtSymtab = 2;
constexpr std::uint32_t kShtStrtab = 3;
constexpr std::uint32_t kShtNobits = 8;
constexpr std::uint16_t kEtRel = 1;
constexpr std::uint16_t kMachineAarch64 = 183;
constexpr std::uint16_t kShnUndef = 0;
constexpr std::uint32_t kLoadModeRamdisk = 1;
constexpr std::uint32_t kLoadModeImagePatch = 2;

template <typename T>
Result<T> failure(ErrorCode code, std::string message) {
    return Result<T>::failure(code, std::move(message));
}

template <typename T>
Result<T> propagate(const Error& error) {
    return Result<T>::failure(error.code, error.message);
}

bool range_ok(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t size) {
    return offset <= data.size() && size <= data.size() - offset;
}

bool read_u16(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t* value) {
    if (!range_ok(data, offset, 2))
        return false;
    *value = static_cast<std::uint16_t>(data[offset]) |
             (static_cast<std::uint16_t>(data[offset + 1]) << 8);
    return true;
}

bool read_u32(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t* value) {
    if (!range_ok(data, offset, 4))
        return false;
    *value = static_cast<std::uint32_t>(data[offset]) |
             (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return true;
}

bool read_u64(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint64_t* value) {
    if (!range_ok(data, offset, 8))
        return false;
    std::uint64_t result = 0;
    for (unsigned int index = 0; index < 8; ++index)
        result |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8);
    *value = result;
    return true;
}

bool write_u32(std::vector<std::uint8_t>* data, std::size_t offset, std::uint32_t value) {
    if (data == nullptr || !range_ok(*data, offset, 4))
        return false;
    (*data)[offset] = static_cast<std::uint8_t>(value);
    (*data)[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    (*data)[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    (*data)[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    return true;
}

bool write_u64(std::vector<std::uint8_t>* data, std::size_t offset, std::uint64_t value) {
    if (data == nullptr || !range_ok(*data, offset, 8))
        return false;
    for (unsigned int index = 0; index < 8; ++index)
        (*data)[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    return true;
}

std::optional<std::size_t> align_up_checked(std::size_t value, std::size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > std::numeric_limits<std::size_t>::max() - (alignment - 1))
        return std::nullopt;
    return (value + alignment - 1) & ~(alignment - 1);
}

struct RequiredSymbols {
    MapSymbol image_base;
    MapSymbol image_end;
    MapSymbol text_start;
    MapSymbol text_end;
    MapSymbol linux_banner;
    MapSymbol arm64_memblock_init;
    MapSymbol memblock_reserve;
    MapSymbol memstart_addr;
    MapSymbol kimage_voffset;
    MapSymbol kernel_init;
    MapSymbol async_synchronize_full;
    MapSymbol load_module;
    MapSymbol strndup_user;
    MapSymbol vmalloc;
    MapSymbol memcpy;
    MapSymbol kstrdup;
};

Result<RequiredSymbols> resolve_required_symbols(const SymbolMap& symbols) {
    auto resolve = [&symbols](const char* name) -> Result<MapSymbol> {
        return symbols.resolve(name);
    };
    RequiredSymbols result;
    auto get = [&](const char* name, MapSymbol* destination) -> Result<void> {
        auto value = resolve(name);
        if (!value)
            return Result<void>::failure(
                value.error().code,
                std::string("required symbol ") + name + ": " + value.error().message);
        *destination = std::move(value.value());
        return Result<void>::success();
    };
    for (const auto& item :
         {std::pair{"_text", &result.image_base}, std::pair{"_end", &result.image_end},
          std::pair{"_stext", &result.text_start}, std::pair{"_etext", &result.text_end},
          std::pair{"linux_banner", &result.linux_banner},
          std::pair{"arm64_memblock_init", &result.arm64_memblock_init},
          std::pair{"memblock_reserve", &result.memblock_reserve},
          std::pair{"memstart_addr", &result.memstart_addr},
          std::pair{"kimage_voffset", &result.kimage_voffset},
          std::pair{"kernel_init", &result.kernel_init},
          std::pair{"async_synchronize_full", &result.async_synchronize_full},
          std::pair{"load_module", &result.load_module},
          std::pair{"strndup_user", &result.strndup_user}, std::pair{"memcpy", &result.memcpy},
          std::pair{"kstrdup", &result.kstrdup}}) {
        auto status = get(item.first, item.second);
        if (!status)
            return failure<RequiredSymbols>(status.error().code, status.error().message);
    }
    auto vmalloc = symbols.resolve("vmalloc");
    if (!vmalloc)
        vmalloc = symbols.resolve("vmalloc_noprof");
    if (!vmalloc)
        return failure<RequiredSymbols>(
            vmalloc.error().code,
            "required symbol vmalloc/vmalloc_noprof: " + vmalloc.error().message);
    result.vmalloc = std::move(vmalloc.value());
    return Result<RequiredSymbols>::success(std::move(result));
}

std::uint64_t image_base(const RequiredSymbols& symbols) {
    return symbols.image_base.address;
}

Result<void> validate_required_bounds(const RequiredSymbols& symbols, std::size_t image_size) {
    const std::uint64_t base = image_base(symbols);
    const auto check = [&](const char* name, const MapSymbol& symbol,
                           bool allow_end) -> Result<void> {
        if (symbol.address < base)
            return Result<void>::failure(
                ErrorCode::kMalformedArm64Image,
                std::string("required symbol ") + name + " is below _text");
        const std::uint64_t offset = symbol.address - base;
        std::uint64_t maximum = image_size;
        if (!allow_end && image_size != 0)
            maximum = image_size - 1;
        if (offset > maximum)
            return Result<void>::failure(
                ErrorCode::kMalformedArm64Image,
                std::string("required symbol ") + name + " is outside the ARM64 Image");
        return Result<void>::success();
    };
    if (symbols.image_end.address < base || symbols.image_end.address - base != image_size)
        return Result<void>::failure(ErrorCode::kMalformedArm64Image,
                                     "_end - _text does not match ARM64 image_size");
    const std::array<std::pair<const char*, const MapSymbol*>, 15> entries = {
        std::pair{"_text", &symbols.image_base},
        std::pair{"_stext", &symbols.text_start},
        std::pair{"_etext", &symbols.text_end},
        std::pair{"linux_banner", &symbols.linux_banner},
        std::pair{"arm64_memblock_init", &symbols.arm64_memblock_init},
        std::pair{"memblock_reserve", &symbols.memblock_reserve},
        std::pair{"memstart_addr", &symbols.memstart_addr},
        std::pair{"kimage_voffset", &symbols.kimage_voffset},
        std::pair{"kernel_init", &symbols.kernel_init},
        std::pair{"async_synchronize_full", &symbols.async_synchronize_full},
        std::pair{"load_module", &symbols.load_module},
        std::pair{"strndup_user", &symbols.strndup_user},
        std::pair{"vmalloc", &symbols.vmalloc},
        std::pair{"memcpy", &symbols.memcpy},
        std::pair{"kstrdup", &symbols.kstrdup},
    };
    for (const auto& entry : entries) {
        auto status = check(entry.first, *entry.second, false);
        if (!status)
            return status;
    }
    return check("_end", symbols.image_end, true);
}

struct CallSite {
    std::size_t file_offset = 0;
    std::uint64_t address = 0;
    std::uint64_t target = 0;
};

struct PatchSites {
    CallSite async_call;
    CallSite strndup_call;
    CallSite memblock_reserve_call;
    std::uint64_t page_offset = 0;
};

std::optional<std::uint64_t> decode_bl_target(std::uint32_t instruction,
                                              std::uint64_t source_address) {
    if ((instruction & 0xfc000000U) != 0x94000000U)
        return std::nullopt;
    std::int64_t immediate = static_cast<std::int64_t>(instruction & 0x03ffffffU);
    if ((immediate & (1LL << 25)) != 0)
        immediate |= ~0x03ffffffLL;
    const __int128 target =
        static_cast<__int128>(source_address) + (static_cast<__int128>(immediate) * 4);
    if (target < 0 || target > std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    return static_cast<std::uint64_t>(target);
}

Result<std::uint32_t> encode_bl(std::uint64_t source_address, std::uint64_t target_address) {
    const __int128 displacement = static_cast<__int128>(target_address) - source_address;
    if ((displacement & 3) != 0)
        return failure<std::uint32_t>(ErrorCode::kUnsupported, "ARM64 BL target is not aligned");
    const __int128 immediate = displacement / 4;
    if (immediate < -(static_cast<__int128>(1) << 25) ||
        immediate >= (static_cast<__int128>(1) << 25))
        return failure<std::uint32_t>(ErrorCode::kUnsupported, "ARM64 BL target is out of range");
    return Result<std::uint32_t>::success(
        0x94000000U |
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(immediate) & 0x03ffffffU));
}

Result<std::size_t> address_to_offset(std::uint64_t address, std::uint64_t base,
                                      std::size_t file_size, const std::string& label) {
    if (address < base || address - base > std::numeric_limits<std::size_t>::max())
        return failure<std::size_t>(ErrorCode::kOutOfRange, label + " is below the Image base");
    const std::size_t offset = static_cast<std::size_t>(address - base);
    if (offset > file_size || file_size - offset < 4)
        return failure<std::size_t>(ErrorCode::kOutOfRange, label + " is outside the Image");
    return Result<std::size_t>::success(offset);
}

Result<std::pair<std::size_t, std::size_t>> function_scan_range(
    const std::vector<std::uint8_t>& image, const SymbolMap& symbols, std::uint64_t base,
    const MapSymbol& function, std::size_t maximum_size) {
    auto start_result = address_to_offset(function.address, base, image.size(), function.name);
    if (!start_result)
        return propagate<std::pair<std::size_t, std::size_t>>(start_result.error());
    const std::size_t start = start_result.value();
    std::uint64_t end_address = function.address + maximum_size;
    if (end_address < function.address)
        return failure<std::pair<std::size_t, std::size_t>>(ErrorCode::kOverflow,
                                                            "function scan range overflow");
    if (auto next = symbols.next_address(function.address))
        end_address = std::min(end_address, *next);
    std::size_t end = image.size();
    if (end_address >= base) {
        const std::uint64_t delta = end_address - base;
        if (delta < end)
            end = static_cast<std::size_t>(delta);
    }
    end = std::min(end, image.size());
    if (end <= start)
        return failure<std::pair<std::size_t, std::size_t>>(ErrorCode::kMalformedArm64Image,
                                                            "cannot establish function scan range");
    return Result<std::pair<std::size_t, std::size_t>>::success({start, end});
}

Result<CallSite> find_unique_direct_call(const std::vector<std::uint8_t>& image,
                                         const SymbolMap& symbols, std::uint64_t base,
                                         const MapSymbol& function,
                                         const std::set<std::uint64_t>& targets, const char* label,
                                         std::size_t maximum_size) {
    auto range = function_scan_range(image, symbols, base, function, maximum_size);
    if (!range)
        return propagate<CallSite>(range.error());
    std::vector<CallSite> matches;
    for (std::size_t offset = range.value().first; offset + 4 <= range.value().second;
         offset += 4) {
        std::uint32_t instruction = 0;
        if (!read_u32(image, offset, &instruction))
            break;
        auto target = decode_bl_target(instruction, base + offset);
        if (target && targets.find(*target) != targets.end())
            matches.push_back({offset, base + offset, *target});
    }
    if (matches.size() != 1)
        return failure<CallSite>(ErrorCode::kAmbiguous, std::string(label) +
                                                            " expected one direct BL, found " +
                                                            std::to_string(matches.size()));
    return Result<CallSite>::success(matches[0]);
}

std::optional<std::array<std::uint32_t, 3>> decode_sub_shifted_register(std::uint32_t instruction) {
    if ((instruction & 0xffe0fc00U) != 0xcb000000U)
        return std::nullopt;
    return std::array<std::uint32_t, 3>{instruction & 31U, (instruction >> 5) & 31U,
                                        (instruction >> 16) & 31U};
}

Result<CallSite> find_memblock_call(const std::vector<std::uint8_t>& image,
                                    const SymbolMap& symbols, std::uint64_t base,
                                    const MapSymbol& caller, const std::set<std::uint64_t>& targets,
                                    std::size_t maximum_size) {
    auto range = function_scan_range(image, symbols, base, caller, maximum_size);
    if (!range)
        return propagate<CallSite>(range.error());
    std::vector<CallSite> direct;
    std::vector<CallSite> semantic;
    for (std::size_t offset = range.value().first; offset + 4 <= range.value().second;
         offset += 4) {
        std::uint32_t instruction = 0;
        if (!read_u32(image, offset, &instruction))
            break;
        auto target = decode_bl_target(instruction, base + offset);
        if (!target || targets.find(*target) == targets.end())
            continue;
        const CallSite call{offset, base + offset, *target};
        direct.push_back(call);
        if (offset < range.value().first + 8)
            continue;
        std::uint32_t first_instruction = 0;
        std::uint32_t second_instruction = 0;
        if (!read_u32(image, offset - 8, &first_instruction) ||
            !read_u32(image, offset - 4, &second_instruction))
            continue;
        auto first = decode_sub_shifted_register(first_instruction);
        auto second = decode_sub_shifted_register(second_instruction);
        if (first && second && (*first)[0] == 1 && (*second)[0] == 0 &&
            (*first)[2] == (*second)[1] && (*first)[2] != 31)
            semantic.push_back(call);
    }
    if (semantic.size() != 1)
        return failure<CallSite>(
            ErrorCode::kAmbiguous,
            "cannot uniquely identify arm64_memblock_init memblock_reserve call");
    return Result<CallSite>::success(semantic[0]);
}

std::uint64_t rotate_right_width(std::uint64_t value, std::uint32_t shift, std::uint32_t width) {
    const std::uint64_t mask =
        width == 64 ? std::numeric_limits<std::uint64_t>::max() : ((1ULL << width) - 1);
    shift %= width;
    if (shift == 0)
        return value & mask;
    return ((value >> shift) | (value << (width - shift))) & mask;
}

std::optional<std::array<std::uint64_t, 3>> decode_orr_immediate(std::uint32_t instruction) {
    if ((instruction >> 31) != 1 || (instruction & 0x7f800000U) != 0x32000000U)
        return std::nullopt;
    const std::uint32_t n = (instruction >> 22) & 1U;
    const std::uint32_t immr = (instruction >> 16) & 0x3fU;
    const std::uint32_t imms = (instruction >> 10) & 0x3fU;
    const std::uint32_t source = (n << 6) | ((~imms) & 0x3fU);
    if (source == 0)
        return std::nullopt;
    std::uint32_t length = 0;
    for (std::uint32_t bit = 0; bit < 7; ++bit) {
        if ((source >> bit) != 0)
            length = bit;
    }
    if (length < 1)
        return std::nullopt;
    const std::uint32_t levels = (1U << length) - 1;
    const std::uint32_t size = imms & levels;
    const std::uint32_t rotate = immr & levels;
    if (size == levels)
        return std::nullopt;
    const std::uint32_t width = 1U << length;
    const std::uint64_t element = rotate_right_width((1ULL << (size + 1)) - 1, rotate, width);
    std::uint64_t immediate = 0;
    for (std::uint32_t bit = 0; bit < 64; bit += width)
        immediate |= element << bit;
    return std::array<std::uint64_t, 3>{immediate, (instruction >> 5) & 31U, instruction & 31U};
}

Result<std::uint64_t> infer_page_offset(const std::vector<std::uint8_t>& image,
                                        const SymbolMap& symbols, std::uint64_t base,
                                        const MapSymbol& function, std::size_t maximum_size) {
    auto range = function_scan_range(image, symbols, base, function, maximum_size);
    if (!range)
        return propagate<std::uint64_t>(range.error());
    std::set<std::uint64_t> candidates;
    for (std::size_t offset = range.value().first; offset + 4 <= range.value().second;
         offset += 4) {
        std::uint32_t instruction = 0;
        if (!read_u32(image, offset, &instruction))
            break;
        auto decoded = decode_orr_immediate(instruction);
        if (!decoded || (*decoded)[1] != (*decoded)[2])
            continue;
        const std::uint64_t immediate = (*decoded)[0];
        const std::uint64_t magnitude = 0ULL - immediate;
        if ((immediate >> 63) == 1 && magnitude != 0 && (magnitude & (magnitude - 1)) == 0) {
            unsigned int trailing = 0;
            std::uint64_t temp = magnitude;
            while ((temp & 1) == 0) {
                ++trailing;
                temp >>= 1;
            }
            if (trailing >= 36 && trailing <= 52 && (base & immediate) == immediate)
                candidates.insert(immediate);
        }
    }
    if (candidates.size() != 1)
        return failure<std::uint64_t>(ErrorCode::kAmbiguous, "cannot uniquely recover PAGE_OFFSET");
    return Result<std::uint64_t>::success(*candidates.begin());
}

std::set<std::uint64_t> symbol_addresses(const SymbolMap& symbols,
                                         std::initializer_list<const char*> names) {
    std::set<std::uint64_t> result;
    for (const char* name : names) {
        for (const auto& symbol : symbols.variants(name))
            result.insert(symbol.address);
    }
    return result;
}

Result<PatchSites> analyze_patch_sites(const std::vector<std::uint8_t>& image,
                                       const SymbolMap& symbols, const RequiredSymbols& required) {
    const std::uint64_t base = image_base(required);
    auto async_targets =
        symbol_addresses(symbols, {"async_synchronize_full", "async_synchronize_cookie_domain"});
    async_targets.insert(required.async_synchronize_full.address);
    auto strndup_targets = symbol_addresses(symbols, {"strndup_user"});
    strndup_targets.insert(required.strndup_user.address);
    auto memblock_targets = symbol_addresses(symbols, {"memblock_reserve"});
    memblock_targets.insert(required.memblock_reserve.address);
    auto async = find_unique_direct_call(image, symbols, base, required.kernel_init, async_targets,
                                         "kernel_init async call", 0x4000);
    if (!async)
        return propagate<PatchSites>(async.error());
    auto strndup =
        find_unique_direct_call(image, symbols, base, required.load_module, strndup_targets,
                                "load_module strndup_user call", 0x10000);
    if (!strndup)
        return propagate<PatchSites>(strndup.error());
    auto memblock = find_memblock_call(image, symbols, base, required.arm64_memblock_init,
                                       memblock_targets, 0x8000);
    if (!memblock)
        return propagate<PatchSites>(memblock.error());
    auto page_offset =
        infer_page_offset(image, symbols, base, required.arm64_memblock_init, 0x8000);
    if (!page_offset)
        return propagate<PatchSites>(page_offset.error());
    if (page_offset.value() < (1ULL << 63) || (page_offset.value() & 0xfff) != 0)
        return failure<PatchSites>(ErrorCode::kMalformedArm64Image,
                                   "recovered PAGE_OFFSET is invalid");
    return Result<PatchSites>::success(
        {async.value(), strndup.value(), memblock.value(), page_offset.value()});
}

struct KernelMetadata {
    RecoveredKallsyms kallsyms;
    std::optional<KernelBtf> btf;
};

Result<KernelMetadata> recover_kernel_metadata(const std::vector<std::uint8_t>& image) {
    auto image_info = parse_arm64_image(image.data(), image.size());
    if (!image_info)
        return propagate<KernelMetadata>(image_info.error());

    const auto kallsyms_candidates = recover_arm64_kallsyms_candidates(image.data(), image.size());
    if (kallsyms_candidates.empty())
        return failure<KernelMetadata>(
            ErrorCode::kKallsymsNotFound,
            "cannot recover GKI kallsyms from ARM64 Image; CONFIG_KALLSYMS_ALL is required");
    const auto btf_candidates = find_btf_candidates(image);

    struct BoundaryMatch {
        std::size_t kallsyms_index;
        std::size_t btf_index;
    };
    std::vector<BoundaryMatch> matches;
    for (std::size_t kallsyms_index = 0; kallsyms_index < kallsyms_candidates.size();
         ++kallsyms_index) {
        const auto& symbols = kallsyms_candidates[kallsyms_index].symbols;
        auto image_base_result = symbols.resolve("_text");
        auto btf_start_result = symbols.resolve("__start_BTF");
        auto btf_stop_result = symbols.resolve("__stop_BTF");
        if (!image_base_result || !btf_start_result || !btf_stop_result)
            continue;
        if (btf_start_result.value().address < image_base_result.value().address ||
            btf_stop_result.value().address < btf_start_result.value().address)
            continue;
        const std::uint64_t offset =
            btf_start_result.value().address - image_base_result.value().address;
        const std::uint64_t size =
            btf_stop_result.value().address - btf_start_result.value().address;
        for (std::size_t btf_index = 0; btf_index < btf_candidates.size(); ++btf_index) {
            if (offset == btf_candidates[btf_index].file_offset() &&
                size == btf_candidates[btf_index].size())
                matches.push_back({kallsyms_index, btf_index});
        }
    }
    if (matches.size() > 1)
        return failure<KernelMetadata>(ErrorCode::kAmbiguous,
                                       "cannot uniquely match vmlinux BTF to GKI kallsyms");

    if (matches.size() == 1) {
        KernelMetadata result{kallsyms_candidates[matches[0].kallsyms_index], std::nullopt};
        KernelBtf btf{};
        std::string btf_error;
        if (!btf_candidates[matches[0].btf_index].inspect(btf, &btf_error))
            return failure<KernelMetadata>(ErrorCode::kMalformedArm64Image,
                                           "selected vmlinux BTF is unusable: " + btf_error);
        result.btf = btf;
        return Result<KernelMetadata>::success(std::move(result));
    }

    if (kallsyms_candidates.size() != 1)
        return failure<KernelMetadata>(
            ErrorCode::kAmbiguous,
            "cannot uniquely recover GKI kallsyms and no BTF boundary matches");
    return Result<KernelMetadata>::success({kallsyms_candidates.front(), std::nullopt});
}

Result<std::pair<std::string, GkiAbiInfo>> recover_gki_abi(const std::vector<std::uint8_t>& image,
                                                           std::uint64_t image_base_address,
                                                           const MapSymbol& linux_banner,
                                                           const std::optional<KernelBtf>& btf) {
    auto offset_result = address_to_offset(linux_banner.address, image_base_address, image.size(),
                                           linux_banner.name);
    if (!offset_result)
        return propagate<std::pair<std::string, GkiAbiInfo>>(offset_result.error());
    const std::size_t offset = offset_result.value();
    const std::size_t limit = std::min(image.size(), offset + 1024);
    std::size_t end = offset;
    while (end < limit && image[end] != 0)
        ++end;
    if (end == limit)
        return failure<std::pair<std::string, GkiAbiInfo>>(ErrorCode::kMalformedArm64Image,
                                                           "linux_banner is not a bounded string");
    const std::string banner(reinterpret_cast<const char*>(image.data() + offset), end - offset);
    constexpr const char* prefix = "Linux version ";
    if (banner.compare(0, std::strlen(prefix), prefix) != 0)
        return failure<std::pair<std::string, GkiAbiInfo>>(
            ErrorCode::kUnsupported, "linux_banner does not contain a kernel release");
    const std::size_t release_start = std::strlen(prefix);
    const std::size_t release_end = banner.find_first_of(" \t\r\n", release_start);
    const std::string release = banner.substr(release_start, release_end == std::string::npos
                                                                 ? std::string::npos
                                                                 : release_end - release_start);
    const std::size_t first_dot = release.find('.');
    const std::size_t second_dot =
        first_dot == std::string::npos ? std::string::npos : release.find('.', first_dot + 1);
    unsigned int major = 0;
    unsigned int minor = 0;
    const auto parse_component = [](std::string_view component, unsigned int* output) {
        const char* const begin = component.data();
        const char* const end = begin + component.size();
        const auto parsed = std::from_chars(begin, end, *output);
        return !component.empty() && parsed.ec == std::errc{} && parsed.ptr == end;
    };
    if (first_dot == std::string::npos || second_dot == std::string::npos ||
        second_dot + 1 >= release.size() ||
        !parse_component(std::string_view(release).substr(0, first_dot), &major) ||
        !parse_component(
            std::string_view(release).substr(first_dot + 1, second_dot - first_dot - 1), &minor))
        return failure<std::pair<std::string, GkiAbiInfo>>(ErrorCode::kUnsupported,
                                                           "kernel release is malformed");
    const bool supported_series = major == 6 && (minor == 1 || minor == 6 || minor == 12);
    if (!supported_series)
        return failure<std::pair<std::string, GkiAbiInfo>>(ErrorCode::kUnsupported,
                                                           "unsupported GKI kernel series");

    GkiAbiInfo abi;
    if (btf) {
        if (btf->load_info) {
            const auto& layout = *btf->load_info;
            if (layout.structure_size == 0 || layout.structure_size > kMaximumLoadInfoStorageSize)
                return failure<std::pair<std::string, GkiAbiInfo>>(
                    ErrorCode::kMalformedArm64Image, "BTF load_info structure is too large");
            const std::uint64_t storage_base =
                std::max<std::uint64_t>(layout.structure_size, kMinimumLoadInfoStorageSize);
            const std::uint64_t aligned = (storage_base + 15) & ~15ULL;
            if (aligned < kMinimumLoadInfoStorageSize || aligned > kMaximumLoadInfoStorageSize)
                return failure<std::pair<std::string, GkiAbiInfo>>(
                    ErrorCode::kMalformedArm64Image, "BTF load_info storage size is invalid");
            abi.load_info_structure_size = layout.structure_size;
            abi.load_info_storage_size = aligned;
            abi.load_info_hdr_offset = layout.hdr_offset;
            abi.load_info_len_offset = layout.len_offset;
        }
    }
    const std::uint64_t structure_size =
        abi.load_info_structure_size.value_or(abi.load_info_storage_size);
    if (abi.load_info_storage_size == 0 ||
        abi.load_info_storage_size > kMaximumLoadInfoStorageSize ||
        (abi.load_info_storage_size & 15) != 0 || structure_size > abi.load_info_storage_size ||
        (abi.load_info_hdr_offset & 7) != 0 || (abi.load_info_len_offset & 7) != 0 ||
        abi.load_info_hdr_offset + 8 > structure_size ||
        abi.load_info_len_offset + 8 > structure_size)
        return failure<std::pair<std::string, GkiAbiInfo>>(ErrorCode::kMalformedArm64Image,
                                                           "invalid GKI load_info ABI");
    return Result<std::pair<std::string, GkiAbiInfo>>::success({release, abi});
}

bool is_text_boundary_symbol(const std::string& name) {
    return name == "_etext" || name.rfind("__stop_", 0) == 0 ||
           name.find("___stop_") != std::string::npos ||
           (name.size() >= 9 && name.compare(name.size() - 9, 9, "_text_end") == 0);
}

Result<std::pair<std::size_t, std::size_t>> find_text_tail_cave(
    const std::vector<std::uint8_t>& image, std::size_t image_size, const SymbolMap& symbols,
    std::uint64_t base, const MapSymbol& text_start, const MapSymbol& text_end,
    std::size_t required_size) {
    if (text_start.address < base || text_end.address < base)
        return failure<std::pair<std::size_t, std::size_t>>(ErrorCode::kMalformedArm64Image,
                                                            "text boundary is below _text");
    const std::size_t start = static_cast<std::size_t>(text_start.address - base);
    const std::size_t end = static_cast<std::size_t>(text_end.address - base);
    if (end <= start || end > image_size || end > image.size() || required_size == 0)
        return failure<std::pair<std::size_t, std::size_t>>(
            ErrorCode::kMalformedArm64Image, "cannot establish permanent-text range");
    std::size_t zero_start = end;
    while (zero_start > start && image[zero_start - 1] == 0)
        --zero_start;
    std::vector<std::size_t> candidates;
    const auto preferred_result = align_up_checked(zero_start, kTextCavePreferredAlignment);
    const auto aligned_result = align_up_checked(zero_start, kTextCaveAlignment);
    if (!preferred_result || !aligned_result)
        return failure<std::pair<std::size_t, std::size_t>>(ErrorCode::kOverflow,
                                                            "text cave alignment overflow");
    const std::size_t preferred = *preferred_result;
    const std::size_t aligned = *aligned_result;
    if (preferred < end)
        candidates.push_back(preferred);
    if (std::find(candidates.begin(), candidates.end(), aligned) == candidates.end())
        candidates.push_back(aligned);
    for (const std::size_t candidate : candidates) {
        if (candidate > end || required_size > end - candidate)
            continue;
        bool occupied = false;
        for (const auto& symbol : symbols.entries()) {
            if (symbol.address < base)
                continue;
            const std::uint64_t symbol_offset_u64 = symbol.address - base;
            if (symbol_offset_u64 >= candidate && symbol_offset_u64 < end &&
                !is_text_boundary_symbol(symbol.name)) {
                occupied = true;
                break;
            }
        }
        if (occupied || std::any_of(image.begin() + static_cast<std::ptrdiff_t>(candidate),
                                    image.begin() + static_cast<std::ptrdiff_t>(end),
                                    [](std::uint8_t byte) { return byte != 0; }))
            continue;
        return Result<std::pair<std::size_t, std::size_t>>::success({candidate, end - candidate});
    }
    return failure<std::pair<std::size_t, std::size_t>>(ErrorCode::kUnsupported,
                                                        "cannot find a proven-zero text cave");
}

bool ranges_overlap(std::size_t left_start, std::size_t left_end, std::size_t right_start,
                    std::size_t right_end) {
    return std::max(left_start, right_start) < std::min(left_end, right_end);
}

Result<void> check_non_overlapping(
    const std::vector<std::tuple<std::size_t, std::size_t, const char*>>& ranges) {
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        for (std::size_t other = index + 1; other < ranges.size(); ++other) {
            if (ranges_overlap(std::get<0>(ranges[index]), std::get<1>(ranges[index]),
                               std::get<0>(ranges[other]), std::get<1>(ranges[other])))
                return Result<void>::failure(ErrorCode::kMalformedArm64Image,
                                             std::string(std::get<2>(ranges[index])) +
                                                 " overlaps " + std::get<2>(ranges[other]));
        }
    }
    return Result<void>::success();
}

struct Fixup {
    std::size_t symbol_file_offset = 0;
    std::uint64_t kernel_offset = 0;
};

struct Capsule {
    std::vector<std::uint8_t> data;
    std::size_t file_offset = 0;
    std::size_t image_size = 0;
    std::size_t module_offset = 0;
    std::size_t fixup_offset = 0;
};

struct RestoreRecord {
    std::size_t offset = 0;
    std::vector<std::uint8_t> bytes;
};

struct RestoreMetadata {
    std::size_t original_file_size = 0;
    std::size_t original_image_size = 0;
    std::vector<RestoreRecord> records;
};

struct ElfSection {
    std::uint32_t type = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::size_t link = 0;
    std::size_t entry_size = 0;
};

std::optional<std::string> read_c_string(const std::vector<std::uint8_t>& data, std::size_t offset,
                                         std::size_t limit) {
    if (offset >= limit || limit > data.size())
        return std::nullopt;
    std::size_t end = offset;
    while (end < limit && data[end] != 0)
        ++end;
    if (end == limit)
        return std::nullopt;
    return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
}

Result<std::vector<ElfSection>> parse_module_sections(const std::vector<std::uint8_t>& module) {
    if (module.size() < 64 || module[0] != 0x7f || module[1] != 'E' || module[2] != 'L' ||
        module[3] != 'F' || module[4] != 2 || module[5] != 1)
        return failure<std::vector<ElfSection>>(ErrorCode::kInvalidArgument,
                                                "module is not a little-endian ELF64 object");
    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    if (!read_u16(module, 16, &type) || !read_u16(module, 18, &machine) || type != kEtRel ||
        machine != kMachineAarch64)
        return failure<std::vector<ElfSection>>(ErrorCode::kUnsupported,
                                                "module must be an AArch64 ET_REL object");
    std::uint64_t section_offset_u64 = 0;
    std::uint16_t section_entry_size = 0;
    std::uint16_t section_count = 0;
    if (!read_u64(module, 40, &section_offset_u64) || !read_u16(module, 58, &section_entry_size) ||
        !read_u16(module, 60, &section_count) ||
        section_offset_u64 > std::numeric_limits<std::size_t>::max() || section_entry_size < 64 ||
        section_count == 0)
        return failure<std::vector<ElfSection>>(ErrorCode::kInvalidArgument,
                                                "module section table is malformed");
    const std::size_t section_offset = static_cast<std::size_t>(section_offset_u64);
    if (section_count >
            (std::numeric_limits<std::size_t>::max() - section_offset) / section_entry_size ||
        !range_ok(module, section_offset,
                  static_cast<std::size_t>(section_count) * section_entry_size))
        return failure<std::vector<ElfSection>>(ErrorCode::kOutOfRange,
                                                "module section table is outside the object");
    std::vector<ElfSection> sections;
    sections.reserve(section_count);
    for (std::size_t index = 0; index < section_count; ++index) {
        const std::size_t offset = section_offset + (index * section_entry_size);
        std::uint32_t section_type = 0;
        std::uint64_t data_offset_u64 = 0;
        std::uint64_t section_size_u64 = 0;
        std::uint32_t link = 0;
        std::uint64_t entry_size_u64 = 0;
        if (!read_u32(module, offset + 4, &section_type) ||
            !read_u64(module, offset + 24, &data_offset_u64) ||
            !read_u64(module, offset + 32, &section_size_u64) ||
            !read_u32(module, offset + 40, &link) ||
            !read_u64(module, offset + 56, &entry_size_u64) ||
            data_offset_u64 > std::numeric_limits<std::size_t>::max() ||
            section_size_u64 > std::numeric_limits<std::size_t>::max() ||
            entry_size_u64 > std::numeric_limits<std::size_t>::max())
            return failure<std::vector<ElfSection>>(ErrorCode::kInvalidArgument,
                                                    "module section header is malformed");
        const ElfSection section{section_type, static_cast<std::size_t>(data_offset_u64),
                                 static_cast<std::size_t>(section_size_u64), link,
                                 static_cast<std::size_t>(entry_size_u64)};
        if (section.type != kShtNobits && !range_ok(module, section.offset, section.size))
            return failure<std::vector<ElfSection>>(ErrorCode::kOutOfRange,
                                                    "module section is outside the object");
        sections.push_back(section);
    }
    return Result<std::vector<ElfSection>>::success(std::move(sections));
}

Result<std::pair<std::vector<Fixup>, std::vector<std::string>>> collect_module_fixups(
    const std::vector<std::uint8_t>& module, const SymbolMap& symbols,
    std::uint64_t image_base_address, std::size_t image_size) {
    auto parsed_sections = parse_module_sections(module);
    if (!parsed_sections)
        return propagate<std::pair<std::vector<Fixup>, std::vector<std::string>>>(
            parsed_sections.error());
    const auto& sections = parsed_sections.value();

    std::vector<Fixup> fixups;
    std::set<std::string> unresolved;
    std::set<std::size_t> seen_offsets;
    for (const auto& symbol_section : sections) {
        if (symbol_section.type != kShtSymtab)
            continue;
        if (symbol_section.entry_size < 24 ||
            symbol_section.size % symbol_section.entry_size != 0 ||
            symbol_section.link >= sections.size())
            return failure<std::pair<std::vector<Fixup>, std::vector<std::string>>>(
                ErrorCode::kInvalidArgument, "module symbol table is malformed");
        const auto& string_section = sections[symbol_section.link];
        if (string_section.type != kShtStrtab ||
            !range_ok(module, string_section.offset, string_section.size))
            return failure<std::pair<std::vector<Fixup>, std::vector<std::string>>>(
                ErrorCode::kInvalidArgument, "module symbol string table is malformed");
        const std::size_t string_end = string_section.offset + string_section.size;
        for (std::size_t index = 1; index < symbol_section.size / symbol_section.entry_size;
             ++index) {
            const std::size_t symbol_offset =
                symbol_section.offset + (index * symbol_section.entry_size);
            std::uint16_t section_index = 0;
            std::uint32_t name_offset = 0;
            if (!read_u16(module, symbol_offset + 6, &section_index) ||
                !read_u32(module, symbol_offset, &name_offset) || section_index != kShnUndef)
                continue;
            if (name_offset >= string_section.size)
                continue;
            if (name_offset > std::numeric_limits<std::size_t>::max() - string_section.offset)
                continue;
            const auto name =
                read_c_string(module, string_section.offset + name_offset, string_end);
            if (!name || name->empty())
                continue;
            auto target = symbols.resolve_module_symbol(*name);
            if (!target || target->address < image_base_address ||
                target->address - image_base_address >= image_size) {
                unresolved.insert(*name);
                continue;
            }
            if (symbol_offset > std::numeric_limits<std::uint32_t>::max())
                return failure<std::pair<std::vector<Fixup>, std::vector<std::string>>>(
                    ErrorCode::kOverflow, "module symbol offset does not fit capsule format");
            if (seen_offsets.insert(symbol_offset).second)
                fixups.push_back({symbol_offset, target->address - image_base_address});
        }
    }
    if (fixups.empty() && unresolved.empty())
        return failure<std::pair<std::vector<Fixup>, std::vector<std::string>>>(
            ErrorCode::kInvalidArgument, "module has no undefined symbols");
    return Result<std::pair<std::vector<Fixup>, std::vector<std::string>>>::success(
        {std::move(fixups), std::vector<std::string>(unresolved.begin(), unresolved.end())});
}

Result<std::vector<std::uint8_t>> module_sha256(const std::vector<std::uint8_t>& module) {
    std::array<std::uint8_t, 32> digest{};
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    const int start_status = mbedtls_sha256_starts(&context, 0);
    const int update_status = start_status == 0
                                  ? mbedtls_sha256_update(&context, module.data(), module.size())
                                  : start_status;
    const int finish_status =
        update_status == 0 ? mbedtls_sha256_finish(&context, digest.data()) : update_status;
    mbedtls_sha256_free(&context);
    if (finish_status != 0)
        return failure<std::vector<std::uint8_t>>(ErrorCode::kUnsupported,
                                                  "cannot calculate module SHA-256");
    return Result<std::vector<std::uint8_t>>::success(
        std::vector<std::uint8_t>(digest.begin(), digest.end()));
}

Result<std::vector<std::uint8_t>> serialize_restore_metadata(const RestoreMetadata& metadata) {
    if (metadata.original_file_size > metadata.original_image_size)
        return failure<std::vector<std::uint8_t>>(
            ErrorCode::kInvalidArgument, "restore metadata has an invalid original image size");

    std::size_t records_size = 0;
    for (const auto& record : metadata.records) {
        if (record.offset > metadata.original_file_size ||
            record.bytes.size() > metadata.original_file_size - record.offset)
            return failure<std::vector<std::uint8_t>>(
                ErrorCode::kOutOfRange, "restore metadata record is outside the original Image");
        if (record.bytes.size() >
            std::numeric_limits<std::size_t>::max() - kRestoreRecordHeaderSize)
            return failure<std::vector<std::uint8_t>>(ErrorCode::kOverflow,
                                                      "restore metadata record is too large");
        const auto record_span =
            align_up_checked(kRestoreRecordHeaderSize + record.bytes.size(), 16);
        if (!record_span || records_size > std::numeric_limits<std::size_t>::max() - *record_span)
            return failure<std::vector<std::uint8_t>>(ErrorCode::kOverflow,
                                                      "restore metadata size overflow");
        records_size += *record_span;
    }
    if (metadata.records.size() > std::numeric_limits<std::uint64_t>::max())
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOverflow,
                                                  "restore metadata record count overflow");
    if (kRestoreMetadataHeaderSize > std::numeric_limits<std::size_t>::max() - records_size)
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOverflow,
                                                  "restore metadata size overflow");
    const auto blob_size = align_up_checked(kRestoreMetadataHeaderSize + records_size, 16);
    if (!blob_size)
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOverflow,
                                                  "restore metadata size overflow");

    std::vector<std::uint8_t> blob(*blob_size, 0);
    std::copy(kRestoreMetadataMagic.begin(), kRestoreMetadataMagic.end(), blob.begin());
    if (!write_u32(&blob, 8, kRestoreMetadataVersion) ||
        !write_u32(&blob, 12, kRestoreMetadataHeaderSize) ||
        !write_u64(&blob, 16, metadata.original_file_size) ||
        !write_u64(&blob, 24, metadata.original_image_size) ||
        !write_u64(&blob, 32, metadata.records.size()) || !write_u64(&blob, 40, *blob_size))
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOutOfRange,
                                                  "restore metadata header is outside its buffer");

    std::size_t cursor = kRestoreMetadataHeaderSize;
    for (const auto& record : metadata.records) {
        if (!write_u64(&blob, cursor, record.offset) ||
            !write_u64(&blob, cursor + 8, record.bytes.size()) ||
            !range_ok(blob, cursor + kRestoreRecordHeaderSize, record.bytes.size()))
            return failure<std::vector<std::uint8_t>>(
                ErrorCode::kOutOfRange, "restore metadata record is outside its buffer");
        std::copy(record.bytes.begin(), record.bytes.end(),
                  blob.begin() + static_cast<std::ptrdiff_t>(cursor + kRestoreRecordHeaderSize));
        const auto record_span =
            align_up_checked(kRestoreRecordHeaderSize + record.bytes.size(), 16);
        if (!record_span)
            return failure<std::vector<std::uint8_t>>(ErrorCode::kOverflow,
                                                      "restore metadata record alignment overflow");
        cursor += *record_span;
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(blob));
}

Result<RestoreMetadata> parse_restore_metadata(const std::vector<std::uint8_t>& data,
                                               std::size_t offset, std::size_t capsule_size) {
    if (offset > capsule_size || capsule_size > data.size() ||
        capsule_size - offset < kRestoreMetadataHeaderSize)
        return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                        "restore metadata header is truncated");
    if (!std::equal(kRestoreMetadataMagic.begin(), kRestoreMetadataMagic.end(),
                    data.begin() + static_cast<std::ptrdiff_t>(offset)))
        return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                        "restore metadata magic is invalid");
    std::uint32_t version = 0;
    std::uint32_t header_size = 0;
    std::uint64_t original_file_size = 0;
    std::uint64_t original_image_size = 0;
    std::uint64_t record_count = 0;
    std::uint64_t blob_size = 0;
    if (!read_u32(data, offset + 8, &version) || !read_u32(data, offset + 12, &header_size) ||
        !read_u64(data, offset + 16, &original_file_size) ||
        !read_u64(data, offset + 24, &original_image_size) ||
        !read_u64(data, offset + 32, &record_count) || !read_u64(data, offset + 40, &blob_size) ||
        version != kRestoreMetadataVersion || header_size != kRestoreMetadataHeaderSize ||
        blob_size < kRestoreMetadataHeaderSize || blob_size > capsule_size - offset ||
        original_file_size > original_image_size ||
        original_file_size > std::numeric_limits<std::size_t>::max() ||
        original_image_size > std::numeric_limits<std::size_t>::max() ||
        record_count > std::numeric_limits<std::size_t>::max())
        return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                        "restore metadata header is invalid");

    const std::size_t native_blob_size = static_cast<std::size_t>(blob_size);
    const std::size_t records_end = offset + native_blob_size;
    std::size_t cursor = offset + kRestoreMetadataHeaderSize;
    RestoreMetadata metadata{static_cast<std::size_t>(original_file_size),
                             static_cast<std::size_t>(original_image_size),
                             {}};
    metadata.records.reserve(static_cast<std::size_t>(record_count));
    for (std::size_t index = 0; index < static_cast<std::size_t>(record_count); ++index) {
        if (cursor > records_end || records_end - cursor < kRestoreRecordHeaderSize)
            return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                            "restore metadata record header is truncated");
        std::uint64_t record_offset = 0;
        std::uint64_t record_size = 0;
        if (!read_u64(data, cursor, &record_offset) || !read_u64(data, cursor + 8, &record_size) ||
            record_offset > original_file_size ||
            record_size > original_file_size - record_offset ||
            record_offset > std::numeric_limits<std::size_t>::max() ||
            record_size > std::numeric_limits<std::size_t>::max())
            return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                            "restore metadata record range is invalid");
        const std::size_t native_offset = static_cast<std::size_t>(record_offset);
        const std::size_t native_size = static_cast<std::size_t>(record_size);
        const auto record_span = align_up_checked(kRestoreRecordHeaderSize + native_size, 16);
        if (!record_span || *record_span > records_end - cursor)
            return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                            "restore metadata record exceeds its blob");
        if (!range_ok(data, cursor + kRestoreRecordHeaderSize, native_size))
            return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                            "restore metadata record data is truncated");
        metadata.records.push_back(
            {native_offset,
             std::vector<std::uint8_t>(
                 data.begin() + static_cast<std::ptrdiff_t>(cursor + kRestoreRecordHeaderSize),
                 data.begin() + static_cast<std::ptrdiff_t>(cursor + kRestoreRecordHeaderSize +
                                                            native_size))});
        cursor += *record_span;
    }
    if (cursor > records_end)
        return failure<RestoreMetadata>(ErrorCode::kMalformedArm64Image,
                                        "restore metadata cursor exceeds its blob");
    return Result<RestoreMetadata>::success(std::move(metadata));
}

Result<Capsule> build_capsule(std::size_t image_size, const std::vector<std::uint8_t>& module,
                              const std::vector<Fixup>& fixups,
                              const RestoreMetadata* restore_metadata = nullptr) {
    if (fixups.size() > (std::numeric_limits<std::size_t>::max() / 16))
        return failure<Capsule>(ErrorCode::kOverflow, "fixup table size overflow");
    std::vector<std::uint8_t> restore_blob;
    if (restore_metadata) {
        auto serialized = serialize_restore_metadata(*restore_metadata);
        if (!serialized)
            return propagate<Capsule>(serialized.error());
        restore_blob = std::move(serialized.value());
    }
    const auto capsule_offset_result = align_up_checked(image_size, 16);
    const auto module_padded_result = align_up_checked(module.size(), 16);
    if (!capsule_offset_result || !module_padded_result)
        return failure<Capsule>(ErrorCode::kOverflow, "capsule alignment overflow");
    const std::size_t capsule_offset = *capsule_offset_result;
    const std::size_t module_relative_offset = kCapsuleHeaderSize;
    const std::size_t module_padded = *module_padded_result;
    if (module_relative_offset > std::numeric_limits<std::size_t>::max() - module_padded)
        return failure<Capsule>(ErrorCode::kOverflow, "capsule module offset overflow");
    const std::size_t fixup_relative_offset = module_relative_offset + module_padded;
    const std::size_t fixup_size = fixups.size() * 16;
    if (fixup_relative_offset > std::numeric_limits<std::size_t>::max() - fixup_size)
        return failure<Capsule>(ErrorCode::kOverflow, "capsule fixup offset overflow");
    const std::size_t fixup_end = fixup_relative_offset + fixup_size;
    const auto restore_relative_offset_result =
        restore_metadata ? align_up_checked(fixup_end, 16) : std::optional<std::size_t>{0};
    if (!restore_relative_offset_result)
        return failure<Capsule>(ErrorCode::kOverflow, "restore metadata offset overflow");
    const std::size_t restore_relative_offset = *restore_relative_offset_result;
    if (restore_metadata &&
        restore_blob.size() > std::numeric_limits<std::size_t>::max() - restore_relative_offset)
        return failure<Capsule>(ErrorCode::kOverflow, "restore metadata size overflow");
    const std::size_t content_end =
        restore_metadata ? restore_relative_offset + restore_blob.size() : fixup_end;
    if (capsule_offset > std::numeric_limits<std::size_t>::max() - content_end)
        return failure<Capsule>(ErrorCode::kOverflow, "capsule image size overflow");
    const auto new_image_size_result =
        align_up_checked(capsule_offset + content_end, kCapsuleAlignment);
    if (!new_image_size_result)
        return failure<Capsule>(ErrorCode::kOverflow, "capsule image size overflow");
    const std::size_t new_image_size = *new_image_size_result;
    const std::size_t capsule_size = new_image_size - capsule_offset;
    std::vector<std::uint8_t> data(capsule_size, 0);
    std::copy(kCapsuleMagic.begin(), kCapsuleMagic.end(), data.begin());
    const std::uint64_t flags =
        (fixups.empty() ? 0 : kCapsuleFixupFlag) | (restore_metadata ? kCapsuleRestoreFlag : 0);
    if (!write_u32(&data, 8, kCapsuleVersion) || !write_u32(&data, 12, kCapsuleHeaderSize) ||
        !write_u64(&data, 16, capsule_size) || !write_u64(&data, 24, module_relative_offset) ||
        !write_u64(&data, 32, module.size()) || !write_u64(&data, 40, fixup_relative_offset) ||
        !write_u64(&data, 48, fixups.size()) || !write_u64(&data, 56, flags))
        return failure<Capsule>(ErrorCode::kOutOfRange, "capsule header is outside its buffer");
    auto digest = module_sha256(module);
    if (!digest)
        return propagate<Capsule>(digest.error());
    std::copy(digest.value().begin(), digest.value().end(), data.begin() + 64);
    if (!module.empty())
        std::copy(module.begin(), module.end(), data.begin() + module_relative_offset);
    for (std::size_t index = 0; index < fixups.size(); ++index) {
        const std::size_t offset = fixup_relative_offset + (index * 16);
        if (!write_u32(&data, offset,
                       static_cast<std::uint32_t>(fixups[index].symbol_file_offset)) ||
            !write_u32(&data, offset + 4, 0) ||
            !write_u64(&data, offset + 8, fixups[index].kernel_offset))
            return failure<Capsule>(ErrorCode::kOutOfRange, "capsule fixup is outside its buffer");
    }
    if (restore_metadata) {
        if (!range_ok(data, restore_relative_offset, restore_blob.size()))
            return failure<Capsule>(ErrorCode::kOutOfRange,
                                    "restore metadata is outside the capsule");
        std::copy(restore_blob.begin(), restore_blob.end(),
                  data.begin() + static_cast<std::ptrdiff_t>(restore_relative_offset));
    }
    return Result<Capsule>::success({std::move(data), capsule_offset, new_image_size,
                                     capsule_offset + module_relative_offset,
                                     capsule_offset + fixup_relative_offset});
}

std::uint64_t capsule_magic_value() {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < kCapsuleMagic.size(); ++index)
        value |= static_cast<std::uint64_t>(kCapsuleMagic[index]) << (index * 8);
    return value;
}

struct ParsedCapsule {
    std::size_t file_offset = 0;
    std::size_t capsule_size = 0;
    std::size_t module_offset = 0;
    std::size_t module_size = 0;
    std::size_t fixup_offset = 0;
    std::size_t fixup_count = 0;
    std::uint64_t flags = 0;
    std::optional<RestoreMetadata> restore_metadata;
};

Result<ParsedCapsule> find_capsule(const std::vector<std::uint8_t>& image, std::size_t image_size) {
    std::optional<ParsedCapsule> result;
    if (image_size < kCapsuleHeaderSize)
        return failure<ParsedCapsule>(ErrorCode::kInvalidArgument, "direct-LKM capsule is missing");
    for (std::size_t offset = 64; offset <= image_size - kCapsuleHeaderSize; offset += 16) {
        if (!std::equal(kCapsuleMagic.begin(), kCapsuleMagic.end(),
                        image.begin() + static_cast<std::ptrdiff_t>(offset)))
            continue;
        std::uint32_t version = 0;
        std::uint32_t header_size = 0;
        std::uint64_t capsule_size = 0;
        std::uint64_t module_offset = 0;
        std::uint64_t module_size = 0;
        std::uint64_t fixup_offset = 0;
        std::uint64_t fixup_count = 0;
        std::uint64_t flags = 0;
        if (!read_u32(image, offset + 8, &version) || !read_u32(image, offset + 12, &header_size) ||
            !read_u64(image, offset + 16, &capsule_size) ||
            !read_u64(image, offset + 24, &module_offset) ||
            !read_u64(image, offset + 32, &module_size) ||
            !read_u64(image, offset + 40, &fixup_offset) ||
            !read_u64(image, offset + 48, &fixup_count) || !read_u64(image, offset + 56, &flags) ||
            version != kCapsuleVersion || header_size != kCapsuleHeaderSize ||
            (flags & ~(kCapsuleFixupFlag | kCapsuleRestoreFlag)) != 0 ||
            capsule_size < kCapsuleHeaderSize ||
            capsule_size > std::numeric_limits<std::size_t>::max() ||
            module_offset < kCapsuleHeaderSize || module_offset > capsule_size ||
            module_size > capsule_size - module_offset || fixup_offset < module_offset ||
            fixup_offset > capsule_size ||
            fixup_count > (capsule_size - fixup_offset) / kCapsuleFixupEntrySize ||
            module_offset % 16 != 0 || fixup_offset % 16 != 0)
            continue;
        const std::size_t capsule_size_native = static_cast<std::size_t>(capsule_size);
        if (capsule_size_native > image_size || offset > image_size - capsule_size_native ||
            offset + capsule_size_native != image_size ||
            fixup_offset > std::numeric_limits<std::size_t>::max() ||
            fixup_count > std::numeric_limits<std::size_t>::max())
            continue;

        ParsedCapsule candidate{offset,
                                capsule_size_native,
                                static_cast<std::size_t>(module_offset),
                                static_cast<std::size_t>(module_size),
                                static_cast<std::size_t>(fixup_offset),
                                static_cast<std::size_t>(fixup_count),
                                flags,
                                std::nullopt};
        if ((flags & kCapsuleRestoreFlag) != 0) {
            const std::size_t fixup_end =
                candidate.fixup_offset + (candidate.fixup_count * kCapsuleFixupEntrySize);
            const auto restore_relative_offset = align_up_checked(fixup_end, 16);
            if (!restore_relative_offset || *restore_relative_offset >= candidate.capsule_size)
                continue;
            auto restore =
                parse_restore_metadata(image, candidate.file_offset + *restore_relative_offset,
                                       candidate.file_offset + candidate.capsule_size);
            if (!restore)
                continue;
            candidate.restore_metadata = std::move(restore.value());
        }
        if (result)
            return failure<ParsedCapsule>(ErrorCode::kAmbiguous,
                                          "multiple direct-LKM capsules found");
        result = std::move(candidate);
    }
    if (!result)
        return failure<ParsedCapsule>(ErrorCode::kInvalidArgument,
                                      "direct-LKM capsule is missing or malformed");
    return Result<ParsedCapsule>::success(std::move(*result));
}

// A direct image patch extends the Image to exactly image_size and stores the
// capsule at its tail. A raw Image stops before the BSS that image_size
// accounts for, so any size mismatch means the capsule is simply absent rather
// than damaged.
Result<ParsedCapsule> locate_capsule(const std::vector<std::uint8_t>& image) {
    auto image_info = parse_arm64_image(image.data(), image.size());
    if (!image_info)
        return propagate<ParsedCapsule>(image_info.error());
    const std::size_t image_size = image_info.value().image_size;
    if (image.size() != image_size)
        return failure<ParsedCapsule>(ErrorCode::kInvalidArgument,
                                      "Image carries no direct-LKM capsule");
    return find_capsule(image, image_size);
}

// Offsets inside an injected bootstrap: its two entry points, the literal pool
// slots and the code span. Images produced by earlier ZySU builds use the
// legacy layout, so both are recognized.
struct BootstrapLayout {
    std::size_t reserve_wrapper;
    std::size_t strndup_adapter;
    std::size_t wrapper_add;
    std::size_t magic;
    std::size_t capsule_offset;
    std::size_t capsule_size;
    std::size_t module_offset;
    std::size_t module_size;
    std::size_t fixup_offset;
    std::size_t fixup_count;
    std::size_t load_info_size;
    std::size_t load_info_hdr;
    std::size_t load_info_len;
    std::size_t reserve_extension;
    std::size_t read_span;
    std::size_t code_size;
    bool legacy;
};

constexpr BootstrapLayout kBootstrapCurrent{0x1a0, 0x1b0, 0x1a8, 0x1f8, 0x1e8, 0x208,
                                            0x210, 0x218, 0x220, 0x228, 0x230, 0x238,
                                            0x240, 0x248, 0x258, 0x263, false};
constexpr BootstrapLayout kBootstrapLegacy{0x1f0, 0x200, 0x1f8, 0x250, 0x240, 0x260,
                                           0x268, 0x270, 0x278, 0x280, 0x290, 0x298,
                                           0x2a0, 0x2a8, 0x2b4, 0x2b4, true};
constexpr std::array<const BootstrapLayout*, 2> kBootstrapLayouts{&kBootstrapCurrent,
                                                                  &kBootstrapLegacy};
constexpr std::uint32_t kReserveWrapperAdd = 0x8b080021U;

struct BootstrapMatch {
    std::size_t pool = 0;
    const BootstrapLayout* layout = nullptr;
};

bool bootstrap_pool_matches(const std::vector<std::uint8_t>& image, std::size_t pool,
                            std::size_t capsule_offset, const BootstrapLayout& layout) {
    std::uint64_t magic = 0;
    std::uint32_t wrapper_add = 0;
    return capsule_offset >= layout.read_span && pool <= capsule_offset - layout.read_span &&
           read_u64(image, pool + layout.magic, &magic) &&
           read_u32(image, pool + layout.wrapper_add, &wrapper_add) &&
           magic == capsule_magic_value() && wrapper_add == kReserveWrapperAdd;
}

Result<BootstrapMatch> single_bootstrap_match(std::vector<BootstrapMatch> candidates) {
    std::sort(candidates.begin(), candidates.end(),
              [](const BootstrapMatch& left, const BootstrapMatch& right) {
                  return std::tie(left.pool, left.layout->legacy) <
                         std::tie(right.pool, right.layout->legacy);
              });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](const BootstrapMatch& left, const BootstrapMatch& right) {
                                     return left.pool == right.pool &&
                                            left.layout->legacy == right.layout->legacy;
                                 }),
                     candidates.end());
    if (candidates.size() != 1)
        return failure<BootstrapMatch>(ErrorCode::kUnsupported,
                                       "cannot locate the existing direct-LKM bootstrap");
    return Result<BootstrapMatch>::success(candidates.front());
}

// Locate the bootstrap without kallsyms: the literal pool carries the capsule
// magic, the reserve wrapper's add instruction and the capsule offset, so a
// linear scan for the magic plus those two cross-checks pins it down.
Result<BootstrapMatch> find_bootstrap_by_magic(const std::vector<std::uint8_t>& image,
                                               std::size_t capsule_offset) {
    const std::array<std::uint8_t, 8>& magic = kCapsuleMagic;
    if (capsule_offset < magic.size())
        return failure<BootstrapMatch>(ErrorCode::kUnsupported,
                                       "patched Image is too small to hold a bootstrap");
    std::vector<BootstrapMatch> candidates;
    for (std::size_t offset = 64; offset <= capsule_offset - magic.size(); offset += 4) {
        if (!std::equal(magic.begin(), magic.end(),
                        image.begin() + static_cast<std::ptrdiff_t>(offset)))
            continue;
        for (const BootstrapLayout* layout : kBootstrapLayouts) {
            if (offset < layout->magic)
                continue;
            const std::size_t pool = offset - layout->magic;
            std::uint64_t recorded_capsule = 0;
            if (bootstrap_pool_matches(image, pool, capsule_offset, *layout) &&
                read_u64(image, pool + layout->capsule_offset, &recorded_capsule) &&
                recorded_capsule == capsule_offset)
                candidates.push_back({pool, layout});
        }
    }
    return single_bootstrap_match(std::move(candidates));
}

// Walk a module's undefined symbols exactly the way collect_module_fixups does
// and hand each symbol table entry to the visitor.
template <typename Visit>
Result<void> for_each_undefined_symbol(const std::vector<std::uint8_t>& module,
                                       const Visit& visit) {
    auto parsed_sections = parse_module_sections(module);
    if (!parsed_sections)
        return propagate<void>(parsed_sections.error());
    const auto& sections = parsed_sections.value();
    for (const auto& symbol_section : sections) {
        if (symbol_section.type != kShtSymtab)
            continue;
        if (symbol_section.entry_size < 24 ||
            symbol_section.size % symbol_section.entry_size != 0 ||
            symbol_section.link >= sections.size())
            return failure<void>(ErrorCode::kInvalidArgument, "module symbol table is malformed");
        const auto& string_section = sections[symbol_section.link];
        if (string_section.type != kShtStrtab ||
            !range_ok(module, string_section.offset, string_section.size))
            return failure<void>(ErrorCode::kInvalidArgument,
                                 "module symbol string table is malformed");
        const std::size_t string_end = string_section.offset + string_section.size;
        for (std::size_t index = 1; index < symbol_section.size / symbol_section.entry_size;
             ++index) {
            const std::size_t symbol_offset =
                symbol_section.offset + (index * symbol_section.entry_size);
            std::uint16_t section_index = 0;
            std::uint32_t name_offset = 0;
            if (!read_u16(module, symbol_offset + 6, &section_index) ||
                !read_u32(module, symbol_offset, &name_offset) || section_index != kShnUndef)
                continue;
            if (name_offset >= string_section.size ||
                name_offset > std::numeric_limits<std::size_t>::max() - string_section.offset)
                continue;
            const auto name =
                read_c_string(module, string_section.offset + name_offset, string_end);
            if (!name || name->empty())
                continue;
            if (auto status = visit(symbol_offset, *name); !status)
                return status;
        }
    }
    return Result<void>::success();
}

// Rebuild the symbol resolution the previous injection performed. The capsule
// stores the module it injected plus that module's fixup table, so pairing the
// table against the module's own symbol names recovers name -> kernel offset
// without touching kallsyms.
struct CapsuleSymbolOffsets {
    std::map<std::string, std::uint64_t> resolved;
    std::set<std::string> unresolved;
};

Result<CapsuleSymbolOffsets> recover_capsule_symbol_offsets(const std::vector<std::uint8_t>& image,
                                                            const ParsedCapsule& capsule) {
    using Offsets = CapsuleSymbolOffsets;
    if ((capsule.flags & kCapsuleFixupFlag) == 0 || capsule.fixup_count == 0)
        return failure<Offsets>(ErrorCode::kUnsupported, "existing capsule has no fixup table");
    const std::size_t module_start = capsule.file_offset + capsule.module_offset;
    if (!range_ok(image, module_start, capsule.module_size) || capsule.module_size == 0)
        return failure<Offsets>(ErrorCode::kOutOfRange, "existing capsule module is out of range");
    const std::vector<std::uint8_t> previous_module(
        image.begin() + static_cast<std::ptrdiff_t>(module_start),
        image.begin() + static_cast<std::ptrdiff_t>(module_start + capsule.module_size));
    auto digest = module_sha256(previous_module);
    if (!digest)
        return propagate<Offsets>(digest.error());
    if (!range_ok(image, capsule.file_offset + 64, digest.value().size()) ||
        !std::equal(digest.value().begin(), digest.value().end(),
                    image.begin() + static_cast<std::ptrdiff_t>(capsule.file_offset + 64)))
        return failure<Offsets>(ErrorCode::kUnsupported,
                                "existing capsule module digest does not match");

    std::map<std::size_t, std::uint64_t> by_symbol_offset;
    for (std::size_t index = 0; index < capsule.fixup_count; ++index) {
        const std::size_t entry =
            capsule.file_offset + capsule.fixup_offset + (index * kCapsuleFixupEntrySize);
        std::uint32_t symbol_offset = 0;
        std::uint32_t reserved = 0;
        std::uint64_t kernel_offset = 0;
        if (!read_u32(image, entry, &symbol_offset) || !read_u32(image, entry + 4, &reserved) ||
            !read_u64(image, entry + 8, &kernel_offset) || reserved != 0)
            return failure<Offsets>(ErrorCode::kMalformedArm64Image,
                                    "existing capsule fixup entry is malformed");
        if (!by_symbol_offset.emplace(symbol_offset, kernel_offset).second)
            return failure<Offsets>(ErrorCode::kAmbiguous,
                                    "existing capsule fixup table has duplicate entries");
    }

    Offsets offsets;
    std::size_t matched = 0;
    const auto status = for_each_undefined_symbol(
        previous_module, [&](std::size_t symbol_offset, const std::string& name) -> Result<void> {
            const auto entry = by_symbol_offset.find(symbol_offset);
            if (entry == by_symbol_offset.end()) {
                if (offsets.resolved.count(name) != 0)
                    return Result<void>::failure(
                        ErrorCode::kAmbiguous,
                        "existing capsule resolves only some uses of " + name);
                offsets.unresolved.insert(name);
                return Result<void>::success();
            }
            ++matched;
            if (offsets.unresolved.count(name) != 0)
                return Result<void>::failure(
                    ErrorCode::kAmbiguous,
                    "existing capsule both resolves and leaves " + name + " unresolved");
            const auto existing = offsets.resolved.emplace(name, entry->second);
            if (!existing.second && existing.first->second != entry->second)
                return Result<void>::failure(ErrorCode::kAmbiguous,
                                             "existing capsule maps " + name + " twice");
            return Result<void>::success();
        });
    if (!status)
        return propagate<Offsets>(status.error());
    if (matched != by_symbol_offset.size())
        return failure<Offsets>(ErrorCode::kUnsupported,
                                "existing capsule fixup table does not match its module");
    return Result<Offsets>::success(std::move(offsets));
}

struct ReusedFixups {
    std::vector<Fixup> fixups;
    std::vector<std::string> unresolved;
};

// Resolve the replacement module against the recovered map. A symbol the
// previous module never referenced cannot be answered from the capsule, so the
// caller has to fall back to a full kallsyms recovery.
Result<ReusedFixups> collect_module_fixups_from_offsets(const std::vector<std::uint8_t>& module,
                                                        const CapsuleSymbolOffsets& offsets) {
    ReusedFixups result;
    std::set<std::string> unresolved;
    std::set<std::size_t> seen_offsets;
    const auto status = for_each_undefined_symbol(
        module, [&](std::size_t symbol_offset, const std::string& name) -> Result<void> {
            const auto entry = offsets.resolved.find(name);
            if (entry == offsets.resolved.end()) {
                if (offsets.unresolved.count(name) != 0) {
                    unresolved.insert(name);
                    return Result<void>::success();
                }
                return Result<void>::failure(
                    ErrorCode::kUnsupported,
                    "replacement module needs the unmapped symbol " + name);
            }
            if (symbol_offset > std::numeric_limits<std::uint32_t>::max())
                return Result<void>::failure(ErrorCode::kOverflow,
                                             "module symbol offset does not fit capsule format");
            if (seen_offsets.insert(symbol_offset).second)
                result.fixups.push_back({symbol_offset, entry->second});
            return Result<void>::success();
        });
    if (!status)
        return propagate<ReusedFixups>(status.error());
    if (result.fixups.empty())
        return failure<ReusedFixups>(ErrorCode::kUnsupported,
                                     "replacement module resolves no kernel symbols");
    result.unresolved.assign(unresolved.begin(), unresolved.end());
    return Result<ReusedFixups>::success(std::move(result));
}

// Swap the module inside an existing capsule in place. The kernel bytes outside
// the capsule are untouched, so every value the bootstrap already holds stays
// valid except the capsule geometry.
Result<InjectionResult> replace_capsule_module_in_place(
    const std::vector<std::uint8_t>& patched_image, const std::vector<std::uint8_t>& module,
    const ParsedCapsule& capsule) {
    auto bootstrap = find_bootstrap_by_magic(patched_image, capsule.file_offset);
    if (!bootstrap)
        return propagate<InjectionResult>(bootstrap.error());
    const BootstrapLayout& layout = *bootstrap.value().layout;
    const std::size_t pool = bootstrap.value().pool;

    auto offsets = recover_capsule_symbol_offsets(patched_image, capsule);
    if (!offsets)
        return propagate<InjectionResult>(offsets.error());
    auto fixups = collect_module_fixups_from_offsets(module, offsets.value());
    if (!fixups)
        return propagate<InjectionResult>(fixups.error());

    const RestoreMetadata* restore_metadata =
        capsule.restore_metadata ? &*capsule.restore_metadata : nullptr;
    auto new_capsule =
        build_capsule(capsule.file_offset, module, fixups.value().fixups, restore_metadata);
    if (!new_capsule)
        return propagate<InjectionResult>(new_capsule.error());
    if (new_capsule.value().file_offset != capsule.file_offset)
        return failure<InjectionResult>(ErrorCode::kUnsupported,
                                        "rebuilt capsule does not start where the old one did");

    std::vector<std::uint8_t> image(
        patched_image.begin(),
        patched_image.begin() + static_cast<std::ptrdiff_t>(new_capsule.value().file_offset));
    image.insert(image.end(), new_capsule.value().data.begin(), new_capsule.value().data.end());
    if (image.size() != new_capsule.value().image_size ||
        !write_u64(&image, kImageSizeOffset, new_capsule.value().image_size))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "repatched Image size is inconsistent");

    // Only the capsule geometry moved. load_info and gfp_kernel describe the
    // kernel itself and are left exactly as the original injection wrote them.
    const std::uint64_t reserve_extension =
        new_capsule.value().image_size - new_capsule.value().file_offset;
    const bool updated =
        write_u64(&image, pool + layout.capsule_offset, new_capsule.value().file_offset) &&
        write_u64(&image, pool + layout.capsule_size, new_capsule.value().data.size()) &&
        write_u64(&image, pool + layout.module_offset,
                  new_capsule.value().module_offset - new_capsule.value().file_offset) &&
        write_u64(&image, pool + layout.module_size, module.size()) &&
        write_u64(&image, pool + layout.fixup_offset,
                  new_capsule.value().fixup_offset - new_capsule.value().file_offset) &&
        write_u64(&image, pool + layout.fixup_count, fixups.value().fixups.size()) &&
        write_u64(&image, pool + layout.reserve_extension, reserve_extension);
    if (!updated)
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "existing direct-LKM bootstrap literal pool is truncated");

    InjectionReport report;
    report.reused_metadata = true;
    report.fixup_count = fixups.value().fixups.size();
    report.unresolved = std::move(fixups.value().unresolved);
    report.image_size = new_capsule.value().image_size;
    std::uint64_t storage_size = 0;
    std::uint64_t hdr_offset = 0;
    std::uint64_t len_offset = 0;
    if (!read_u64(image, pool + layout.load_info_size, &storage_size) ||
        !read_u64(image, pool + layout.load_info_hdr, &hdr_offset) ||
        !read_u64(image, pool + layout.load_info_len, &len_offset))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "existing direct-LKM bootstrap literal pool is truncated");
    report.gki_abi.load_info_storage_size = storage_size;
    report.gki_abi.load_info_hdr_offset = hdr_offset;
    report.gki_abi.load_info_len_offset = len_offset;
    return Result<InjectionResult>::success({std::move(image), std::move(report)});
}

Result<InjectionResult> inject_image_impl(const std::vector<std::uint8_t>& original_image,
                                          const std::vector<std::uint8_t>& module) {
    auto image_info = parse_arm64_image(original_image.data(), original_image.size());
    if (!image_info)
        return propagate<InjectionResult>(image_info.error());
    const std::size_t image_size = image_info.value().image_size;
    if (original_image.size() > image_size)
        return failure<InjectionResult>(
            ErrorCode::kUnsupported,
            "input contains bytes beyond ARM64 image_size; appended metadata is unsupported");

    auto metadata = recover_kernel_metadata(original_image);
    if (!metadata)
        return propagate<InjectionResult>(metadata.error());
    auto required = resolve_required_symbols(metadata.value().kallsyms.symbols);
    if (!required)
        return propagate<InjectionResult>(required.error());
    auto bounds = validate_required_bounds(required.value(), image_size);
    if (!bounds)
        return propagate<InjectionResult>(bounds.error());
    const std::uint64_t base = image_base(required.value());
    auto abi_result =
        recover_gki_abi(original_image, base, required.value().linux_banner, metadata.value().btf);
    if (!abi_result)
        return propagate<InjectionResult>(abi_result.error());
    const std::string kernel_release = abi_result.value().first;
    const GkiAbiInfo abi = abi_result.value().second;
    auto sites =
        analyze_patch_sites(original_image, metadata.value().kallsyms.symbols, required.value());
    if (!sites)
        return propagate<InjectionResult>(sites.error());
    auto fixup_result =
        collect_module_fixups(module, metadata.value().kallsyms.symbols, base, image_size);
    if (!fixup_result)
        return propagate<InjectionResult>(fixup_result.error());

    const BootstrapObjectView bootstrap_object_view = bootstrap_object();
    std::string bootstrap_error;
    const std::size_t bootstrap_size =
        bootstrap_image_size(bootstrap_object_view, &bootstrap_error);
    if (bootstrap_size == 0)
        return failure<InjectionResult>(ErrorCode::kUnsupported,
                                        "cannot parse embedded LKM bootstrap: " + bootstrap_error);
    auto cave =
        find_text_tail_cave(original_image, image_size, metadata.value().kallsyms.symbols, base,
                            required.value().text_start, required.value().text_end, bootstrap_size);
    if (!cave)
        return propagate<InjectionResult>(cave.error());
    const std::size_t code_offset = cave.value().first;
    const std::size_t cave_size = cave.value().second;
    if (base > std::numeric_limits<std::uint64_t>::max() - code_offset)
        return failure<InjectionResult>(ErrorCode::kOverflow, "bootstrap address overflow");
    const std::uint64_t code_address = base + code_offset;

    auto overlap = check_non_overlapping({
        {code_offset, code_offset + bootstrap_size, "bootstrap cave"},
        {sites.value().async_call.file_offset, sites.value().async_call.file_offset + 4,
         "kernel_init patch"},
        {sites.value().strndup_call.file_offset, sites.value().strndup_call.file_offset + 4,
         "load_module patch"},
        {sites.value().memblock_reserve_call.file_offset,
         sites.value().memblock_reserve_call.file_offset + 4, "memblock patch"},
    });
    if (!overlap)
        return propagate<InjectionResult>(overlap.error());

    std::vector<std::uint8_t> restore_source = original_image;
    if (restore_source.size() < image_size)
        restore_source.resize(image_size, 0);
    RestoreMetadata restore_metadata{original_image.size(), image_size, {}};
    const auto add_restore_record = [&](std::size_t offset, std::size_t size) -> Result<void> {
        if (!range_ok(restore_source, offset, size))
            return Result<void>::failure(ErrorCode::kOutOfRange,
                                         "restore metadata source range is outside the Image");
        restore_metadata.records.push_back(
            {offset, std::vector<std::uint8_t>(
                         restore_source.begin() + static_cast<std::ptrdiff_t>(offset),
                         restore_source.begin() + static_cast<std::ptrdiff_t>(offset + size))});
        return Result<void>::success();
    };
    if (auto status = add_restore_record(kImageSizeOffset, sizeof(std::uint64_t)); !status)
        return propagate<InjectionResult>(status.error());
    if (auto status = add_restore_record(code_offset, bootstrap_size); !status)
        return propagate<InjectionResult>(status.error());
    if (auto status = add_restore_record(sites.value().async_call.file_offset, 4); !status)
        return propagate<InjectionResult>(status.error());
    if (auto status = add_restore_record(sites.value().strndup_call.file_offset, 4); !status)
        return propagate<InjectionResult>(status.error());
    if (auto status = add_restore_record(sites.value().memblock_reserve_call.file_offset, 4);
        !status)
        return propagate<InjectionResult>(status.error());

    auto capsule_result =
        build_capsule(image_size, module, fixup_result.value().first, &restore_metadata);
    if (!capsule_result)
        return propagate<InjectionResult>(capsule_result.error());
    Capsule capsule = std::move(capsule_result.value());
    const std::size_t reserve_extension = capsule.image_size - image_size;

    std::vector<BootstrapDefinition> definitions;
    auto add_definition = [&definitions](std::string_view name, std::uint64_t value) {
        definitions.push_back({name, value});
    };
    add_definition("ksu_ext_memblock_reserve", sites.value().memblock_reserve_call.target);
    add_definition("ksu_ext_memstart_addr", required.value().memstart_addr.address);
    add_definition("ksu_ext_kimage_voffset", required.value().kimage_voffset.address);
    add_definition("ksu_ext_async_synchronize_full", sites.value().async_call.target);
    add_definition("ksu_ext_vmalloc", required.value().vmalloc.address);
    add_definition("ksu_ext_memcpy", required.value().memcpy.address);
    add_definition("ksu_ext_load_module", required.value().load_module.address);
    add_definition("ksu_ext_kstrdup", required.value().kstrdup.address);
    add_definition("ksu_ext_strndup_user", sites.value().strndup_call.target);
    add_definition("ksu_image_base", base);
    add_definition("ksu_capsule_magic", capsule_magic_value());
    add_definition("ksu_capsule_version", kCapsuleVersion);
    add_definition("ksu_capsule_header_size", kCapsuleHeaderSize);
    add_definition("ksu_capsule_image_offset", capsule.file_offset);
    add_definition("ksu_capsule_size", capsule.data.size());
    add_definition("ksu_module_capsule_offset", capsule.module_offset - capsule.file_offset);
    add_definition("ksu_module_size", module.size());
    add_definition("ksu_fixup_capsule_offset", capsule.fixup_offset - capsule.file_offset);
    add_definition("ksu_fixup_count", fixup_result.value().first.size());
    add_definition("ksu_reserve_extension", reserve_extension);
    add_definition("ksu_page_offset", sites.value().page_offset);
    add_definition("ksu_load_info_size", abi.load_info_storage_size);
    add_definition("ksu_load_info_hdr_offset", abi.load_info_hdr_offset);
    add_definition("ksu_load_info_len_offset", abi.load_info_len_offset);
    add_definition("ksu_gfp_kernel", abi.gfp_kernel);

    LinkedBootstrap linked;
    if (!link_bootstrap(bootstrap_object_view, code_address, definitions, &linked,
                        &bootstrap_error))
        return failure<InjectionResult>(
            ErrorCode::kUnsupported, "cannot relocate embedded LKM bootstrap: " + bootstrap_error);
    if (linked.data.size() != bootstrap_size)
        return failure<InjectionResult>(
            ErrorCode::kMalformedArm64Image,
            "linked LKM bootstrap size differs from its restore coverage");
    if (linked.data.size() > cave_size)
        return failure<InjectionResult>(ErrorCode::kUnsupported,
                                        "bootstrap does not fit the text cave");
    if (linked.entry_address != code_address)
        return failure<InjectionResult>(ErrorCode::kMalformedArm64Image,
                                        "linked bootstrap entry does not match the selected cave");
    std::vector<std::uint8_t> image = original_image;
    if (image.size() < image_size)
        image.resize(image_size, 0);
    if (!range_ok(image, code_offset, linked.data.size()))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "bootstrap output is outside the Image");
    std::copy(linked.data.begin(), linked.data.end(),
              image.begin() + static_cast<std::ptrdiff_t>(code_offset));
    auto async_instruction = encode_bl(sites.value().async_call.address, linked.entry_address);
    auto strndup_instruction =
        encode_bl(sites.value().strndup_call.address, linked.strndup_adapter_address);
    auto memblock_instruction =
        encode_bl(sites.value().memblock_reserve_call.address, linked.reserve_wrapper_address);
    if (!async_instruction || !strndup_instruction || !memblock_instruction)
        return failure<InjectionResult>(ErrorCode::kUnsupported, "cannot encode bootstrap branch");
    if (!write_u32(&image, sites.value().async_call.file_offset, async_instruction.value()) ||
        !write_u32(&image, sites.value().strndup_call.file_offset, strndup_instruction.value()) ||
        !write_u32(&image, sites.value().memblock_reserve_call.file_offset,
                   memblock_instruction.value()))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "kernel patch site is outside the Image");
    image.resize(capsule.file_offset, 0);
    image.insert(image.end(), capsule.data.begin(), capsule.data.end());
    if (image.size() != capsule.image_size ||
        !write_u64(&image, kImageSizeOffset, capsule.image_size))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "patched Image size is inconsistent");

    InjectionReport report;
    report.kernel_release = kernel_release;
    report.kallsyms_layout = metadata.value().kallsyms.layout;
    report.kallsyms_count = metadata.value().kallsyms.count;
    report.gki_abi = abi;
    report.code_offset = code_offset;
    report.code_size = linked.data.size();
    report.memblock_call_offset = sites.value().memblock_reserve_call.file_offset;
    report.page_offset = sites.value().page_offset;
    report.fixup_count = fixup_result.value().first.size();
    report.unresolved = std::move(fixup_result.value().second);
    report.image_size = capsule.image_size;
    if (metadata.value().btf) {
        const KernelBtf selected_btf = metadata.value().btf.value_or(KernelBtf{});
        report.btf_offset = selected_btf.file_offset;
        report.btf_size = selected_btf.size;
        report.btf_type_count = selected_btf.type_count;
    }
    return Result<InjectionResult>::success({std::move(image), std::move(report)});
}

}  // namespace

Result<void> mark_module_image_patch(std::vector<std::uint8_t>* module, bool require_marker) {
    if (module == nullptr)
        return failure<void>(ErrorCode::kInvalidArgument, "module buffer is null");
    auto parsed_sections = parse_module_sections(*module);
    if (!parsed_sections)
        return propagate<void>(parsed_sections.error());
    const auto& sections = parsed_sections.value();
    std::optional<std::size_t> load_mode_offset;

    for (const auto& symbol_section : sections) {
        if (symbol_section.type != kShtSymtab)
            continue;
        if (symbol_section.entry_size < 24 ||
            symbol_section.size % symbol_section.entry_size != 0 ||
            symbol_section.link >= sections.size())
            return failure<void>(ErrorCode::kInvalidArgument, "module symbol table is malformed");
        const auto& string_section = sections[symbol_section.link];
        if (string_section.type != kShtStrtab ||
            !range_ok(*module, string_section.offset, string_section.size))
            return failure<void>(ErrorCode::kInvalidArgument,
                                 "module symbol string table is malformed");
        const std::size_t string_end = string_section.offset + string_section.size;
        for (std::size_t index = 1; index < symbol_section.size / symbol_section.entry_size;
             ++index) {
            const std::size_t symbol_offset =
                symbol_section.offset + (index * symbol_section.entry_size);
            std::uint32_t name_offset = 0;
            std::uint16_t section_index = 0;
            std::uint64_t section_value = 0;
            std::uint64_t symbol_size = 0;
            if (!read_u32(*module, symbol_offset, &name_offset) ||
                !read_u16(*module, symbol_offset + 6, &section_index) ||
                !read_u64(*module, symbol_offset + 8, &section_value) ||
                !read_u64(*module, symbol_offset + 16, &symbol_size) ||
                section_index == kShnUndef || name_offset >= string_section.size)
                continue;
            const auto name =
                read_c_string(*module, string_section.offset + name_offset, string_end);
            if (!name || *name != "ksu_boot_load_mode")
                continue;
            if (section_index >= sections.size() || symbol_size < sizeof(std::uint32_t) ||
                section_value > std::numeric_limits<std::size_t>::max())
                return failure<void>(ErrorCode::kInvalidArgument,
                                     "LKM image-patch marker symbol is malformed");
            const auto& data_section = sections[section_index];
            const std::size_t value = static_cast<std::size_t>(section_value);
            if (data_section.type == kShtNobits || value > data_section.size ||
                data_section.size - value < sizeof(std::uint32_t) ||
                data_section.offset > std::numeric_limits<std::size_t>::max() - value ||
                !range_ok(*module, data_section.offset + value, sizeof(std::uint32_t)))
                return failure<void>(ErrorCode::kOutOfRange,
                                     "LKM image-patch marker is outside the object");
            const std::size_t candidate = data_section.offset + value;
            if (load_mode_offset && *load_mode_offset != candidate)
                return failure<void>(ErrorCode::kInvalidArgument,
                                     "LKM contains duplicate image-patch markers");
            load_mode_offset = candidate;
        }
    }

    if (!load_mode_offset) {
        if (!require_marker)
            return Result<void>::success();
        return failure<void>(ErrorCode::kUnsupported,
                             "LKM does not expose the image-patch load-mode marker");
    }
    std::uint32_t current_mode = 0;
    if (!read_u32(*module, *load_mode_offset, &current_mode) ||
        (current_mode != kLoadModeRamdisk && current_mode != kLoadModeImagePatch))
        return failure<void>(ErrorCode::kInvalidArgument,
                             "LKM image-patch marker has an unexpected value");
    if (!write_u32(module, *load_mode_offset, kLoadModeImagePatch))
        return failure<void>(ErrorCode::kOutOfRange, "cannot update the LKM image-patch marker");
    return Result<void>::success();
}

Result<InjectionResult> inject_image(const std::vector<std::uint8_t>& original_image,
                                     const std::vector<std::uint8_t>& module) {
    return inject_image_impl(original_image, module);
}

namespace {

Result<InjectionResult> replace_capsule_module_full(const std::vector<std::uint8_t>& patched_image,
                                                    const std::vector<std::uint8_t>& module,
                                                    const ParsedCapsule& capsule) {
    // Kallsyms encodes the original _end - _text size. Hide the appended
    // capsule while recovering metadata; the bytes themselves remain present
    // for BTF and banner parsing.
    std::vector<std::uint8_t> analysis_image = patched_image;
    if (!write_u64(&analysis_image, kImageSizeOffset, capsule.file_offset))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "cannot normalize patched Image size for analysis");
    auto metadata = recover_kernel_metadata(analysis_image);
    if (!metadata)
        return propagate<InjectionResult>(metadata.error());
    auto required = resolve_required_symbols(metadata.value().kallsyms.symbols);
    if (!required)
        return propagate<InjectionResult>(required.error());
    const std::uint64_t base = image_base(required.value());
    auto bounds = validate_required_bounds(required.value(), capsule.file_offset);
    if (!bounds)
        return propagate<InjectionResult>(bounds.error());
    auto abi =
        recover_gki_abi(analysis_image, base, required.value().linux_banner, metadata.value().btf);
    if (!abi)
        return propagate<InjectionResult>(abi.error());
    auto fixup_result =
        collect_module_fixups(module, metadata.value().kallsyms.symbols, base, capsule.file_offset);
    if (!fixup_result)
        return propagate<InjectionResult>(fixup_result.error());
    const RestoreMetadata* restore_metadata =
        capsule.restore_metadata ? &*capsule.restore_metadata : nullptr;
    auto new_capsule =
        build_capsule(capsule.file_offset, module, fixup_result.value().first, restore_metadata);
    if (!new_capsule)
        return propagate<InjectionResult>(new_capsule.error());
    std::vector<std::uint8_t> image = patched_image;
    image.resize(new_capsule.value().file_offset);
    image.insert(image.end(), new_capsule.value().data.begin(), new_capsule.value().data.end());
    if (image.size() != new_capsule.value().image_size ||
        !write_u64(&image, kImageSizeOffset, new_capsule.value().image_size))
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "repatched Image size is inconsistent");

    // Locate the literal pool through the three call sites we already patched
    // instead of depending on a code prefix.
    std::vector<BootstrapMatch> bootstrap_candidates;
    const auto collect_bootstrap_targets = [&](const MapSymbol& function,
                                               std::size_t maximum_size) -> Result<void> {
        auto range = function_scan_range(image, metadata.value().kallsyms.symbols, base, function,
                                         maximum_size);
        if (!range)
            return propagate<void>(range.error());
        for (std::size_t offset = range.value().first; offset + 4 <= range.value().second;
             offset += 4) {
            std::uint32_t instruction = 0;
            if (!read_u32(image, offset, &instruction))
                break;
            auto target = decode_bl_target(instruction, base + offset);
            if (!target || *target < base || *target - base > image.size() ||
                *target - base > std::numeric_limits<std::size_t>::max())
                continue;
            const std::size_t target_offset = static_cast<std::size_t>(*target - base);
            for (const std::size_t target_delta :
                 {std::size_t{0}, std::size_t{0x1a0}, std::size_t{0x1b0}, std::size_t{0x1f0},
                  std::size_t{0x200}}) {
                if (target_offset < target_delta)
                    continue;
                const std::size_t candidate = target_offset - target_delta;
                for (const BootstrapLayout* layout : kBootstrapLayouts) {
                    if (bootstrap_pool_matches(image, candidate, capsule.file_offset, *layout))
                        bootstrap_candidates.push_back({candidate, layout});
                }
            }
        }
        return Result<void>::success();
    };
    if (auto status = collect_bootstrap_targets(required.value().kernel_init, 0x4000); !status)
        return propagate<InjectionResult>(status.error());
    if (auto status = collect_bootstrap_targets(required.value().load_module, 0x10000); !status)
        return propagate<InjectionResult>(status.error());
    if (auto status = collect_bootstrap_targets(required.value().arm64_memblock_init, 0x8000);
        !status)
        return propagate<InjectionResult>(status.error());
    auto bootstrap = single_bootstrap_match(std::move(bootstrap_candidates));
    if (!bootstrap)
        return propagate<InjectionResult>(bootstrap.error());
    const BootstrapLayout& layout = *bootstrap.value().layout;
    const std::size_t pool = bootstrap.value().pool;
    const std::uint64_t reserve_extension =
        new_capsule.value().image_size - new_capsule.value().file_offset;
    const bool updated =
        write_u64(&image, pool + layout.capsule_offset, new_capsule.value().file_offset) &&
        write_u64(&image, pool + layout.capsule_size, new_capsule.value().data.size()) &&
        write_u64(&image, pool + layout.module_offset,
                  new_capsule.value().module_offset - new_capsule.value().file_offset) &&
        write_u64(&image, pool + layout.module_size, module.size()) &&
        write_u64(&image, pool + layout.fixup_offset,
                  new_capsule.value().fixup_offset - new_capsule.value().file_offset) &&
        write_u64(&image, pool + layout.fixup_count, fixup_result.value().first.size()) &&
        write_u64(&image, pool + layout.load_info_size,
                  abi.value().second.load_info_storage_size) &&
        write_u64(&image, pool + layout.load_info_hdr, abi.value().second.load_info_hdr_offset) &&
        write_u64(&image, pool + layout.load_info_len, abi.value().second.load_info_len_offset) &&
        write_u64(&image, pool + layout.reserve_extension, reserve_extension);
    if (!updated)
        return failure<InjectionResult>(ErrorCode::kOutOfRange,
                                        "existing direct-LKM bootstrap literal pool is truncated");

    InjectionReport report;
    report.kernel_release = abi.value().first;
    report.kallsyms_layout = metadata.value().kallsyms.layout;
    report.kallsyms_count = metadata.value().kallsyms.count;
    report.gki_abi = abi.value().second;
    report.fixup_count = fixup_result.value().first.size();
    report.unresolved = std::move(fixup_result.value().second);
    report.image_size = new_capsule.value().image_size;
    if (metadata.value().btf) {
        const KernelBtf selected_btf = metadata.value().btf.value_or(KernelBtf{});
        report.btf_offset = selected_btf.file_offset;
        report.btf_size = selected_btf.size;
        report.btf_type_count = selected_btf.type_count;
    }
    return Result<InjectionResult>::success({std::move(image), std::move(report)});
}

}  // namespace

Result<InjectionResult> replace_capsule_module(const std::vector<std::uint8_t>& patched_image,
                                               const std::vector<std::uint8_t>& module,
                                               bool allow_reuse) {
    auto capsule_storage = locate_capsule(patched_image);
    if (!capsule_storage)
        return propagate<InjectionResult>(capsule_storage.error());
    const ParsedCapsule& capsule = capsule_storage.value();

    std::string skipped_reason = "disabled by request";
    if (allow_reuse) {
        auto reused = replace_capsule_module_in_place(patched_image, module, capsule);
        if (reused)
            return reused;
        skipped_reason = reused.error().message;
    }
    auto rebuilt = replace_capsule_module_full(patched_image, module, capsule);
    if (rebuilt)
        rebuilt.value().report.reuse_skipped_reason = std::move(skipped_reason);
    return rebuilt;
}

bool contains_capsule(const std::vector<std::uint8_t>& image) {
    return locate_capsule(image).has_value();
}

Result<std::vector<std::uint8_t>> remove_capsule(const std::vector<std::uint8_t>& patched_image) {
    auto capsule_storage = locate_capsule(patched_image);
    if (!capsule_storage)
        return propagate<std::vector<std::uint8_t>>(capsule_storage.error());
    const ParsedCapsule& capsule = capsule_storage.value();
    if (capsule.restore_metadata) {
        const RestoreMetadata& metadata = *capsule.restore_metadata;
        if (metadata.original_file_size > patched_image.size() ||
            metadata.original_image_size > capsule.file_offset ||
            metadata.original_file_size > metadata.original_image_size)
            return failure<std::vector<std::uint8_t>>(
                ErrorCode::kMalformedArm64Image,
                "restore metadata describes an invalid Image size");
        std::vector<std::uint8_t> restored = patched_image;
        for (const auto& record : metadata.records) {
            if (!range_ok(restored, record.offset, record.bytes.size()))
                return failure<std::vector<std::uint8_t>>(
                    ErrorCode::kMalformedArm64Image,
                    "restore metadata record is outside the image");
            std::copy(record.bytes.begin(), record.bytes.end(),
                      restored.begin() + static_cast<std::ptrdiff_t>(record.offset));
        }
        if (restored.size() < kImageSizeOffset + sizeof(std::uint64_t))
            return failure<std::vector<std::uint8_t>>(ErrorCode::kMalformedArm64Image,
                                                      "restored Image header is truncated");
        std::uint64_t restored_image_size = 0;
        if (!read_u64(restored, kImageSizeOffset, &restored_image_size) ||
            restored_image_size != metadata.original_image_size)
            return failure<std::vector<std::uint8_t>>(
                ErrorCode::kMalformedArm64Image, "restore metadata did not restore Image size");
        restored.resize(metadata.original_file_size);
        return Result<std::vector<std::uint8_t>>::success(std::move(restored));
    }
    const std::size_t capsule_offset = capsule.file_offset;

    std::vector<std::uint8_t> analysis_image = patched_image;
    if (!write_u64(&analysis_image, kImageSizeOffset, capsule_offset))
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOutOfRange,
                                                  "cannot normalize patched Image size");
    auto metadata = recover_kernel_metadata(analysis_image);
    if (!metadata)
        return propagate<std::vector<std::uint8_t>>(metadata.error());
    auto required = resolve_required_symbols(metadata.value().kallsyms.symbols);
    if (!required)
        return propagate<std::vector<std::uint8_t>>(required.error());
    const std::uint64_t base = image_base(required.value());
    auto bounds = validate_required_bounds(required.value(), capsule_offset);
    if (!bounds)
        return propagate<std::vector<std::uint8_t>>(bounds.error());

    std::vector<BootstrapMatch> candidates;
    const auto collect = [&](const MapSymbol& function, std::size_t maximum_size) -> Result<void> {
        auto range = function_scan_range(patched_image, metadata.value().kallsyms.symbols, base,
                                         function, maximum_size);
        if (!range)
            return propagate<void>(range.error());
        for (std::size_t offset = range.value().first; offset + 4 <= range.value().second;
             offset += 4) {
            std::uint32_t instruction = 0;
            if (!read_u32(patched_image, offset, &instruction))
                break;
            auto target = decode_bl_target(instruction, base + offset);
            if (!target || *target < base || *target - base > patched_image.size())
                continue;
            const std::size_t target_offset = static_cast<std::size_t>(*target - base);
            for (const std::size_t target_delta :
                 {std::size_t{0}, std::size_t{0x1a0}, std::size_t{0x1b0}, std::size_t{0x1f0},
                  std::size_t{0x200}}) {
                if (target_offset < target_delta)
                    continue;
                const std::size_t candidate = target_offset - target_delta;
                for (const BootstrapLayout* layout : kBootstrapLayouts) {
                    if (bootstrap_pool_matches(patched_image, candidate, capsule_offset, *layout))
                        candidates.push_back({candidate, layout});
                }
            }
        }
        return Result<void>::success();
    };
    if (auto status = collect(required.value().kernel_init, 0x4000); !status)
        return propagate<std::vector<std::uint8_t>>(status.error());
    if (auto status = collect(required.value().load_module, 0x10000); !status)
        return propagate<std::vector<std::uint8_t>>(status.error());
    if (auto status = collect(required.value().arm64_memblock_init, 0x8000); !status)
        return propagate<std::vector<std::uint8_t>>(status.error());
    auto bootstrap = single_bootstrap_match(std::move(candidates));
    if (!bootstrap)
        return propagate<std::vector<std::uint8_t>>(bootstrap.error());
    const BootstrapLayout& layout = *bootstrap.value().layout;
    const std::size_t pool = bootstrap.value().pool;
    const std::uint64_t entry_address = base + pool;
    const std::uint64_t wrapper_address = entry_address + layout.reserve_wrapper;
    const std::uint64_t adapter_address = entry_address + layout.strndup_adapter;

    const auto find_call = [&](const MapSymbol& function, std::size_t maximum_size,
                               std::uint64_t target) -> Result<std::size_t> {
        auto range = function_scan_range(patched_image, metadata.value().kallsyms.symbols, base,
                                         function, maximum_size);
        if (!range)
            return propagate<std::size_t>(range.error());
        std::optional<std::size_t> result;
        for (std::size_t offset = range.value().first; offset + 4 <= range.value().second;
             offset += 4) {
            std::uint32_t instruction = 0;
            if (!read_u32(patched_image, offset, &instruction))
                break;
            auto decoded = decode_bl_target(instruction, base + offset);
            if (!decoded || *decoded != target)
                continue;
            if (result)
                return failure<std::size_t>(ErrorCode::kAmbiguous,
                                            "multiple direct-LKM call sites found");
            result = offset;
        }
        if (!result)
            return failure<std::size_t>(ErrorCode::kUnsupported, "direct-LKM call site is missing");
        return Result<std::size_t>::success(*result);
    };
    auto async_call = find_call(required.value().kernel_init, 0x4000, entry_address);
    auto strndup_call = find_call(required.value().load_module, 0x10000, adapter_address);
    auto memblock_call = find_call(required.value().arm64_memblock_init, 0x8000, wrapper_address);
    if (!async_call)
        return propagate<std::vector<std::uint8_t>>(async_call.error());
    if (!strndup_call)
        return propagate<std::vector<std::uint8_t>>(strndup_call.error());
    if (!memblock_call)
        return propagate<std::vector<std::uint8_t>>(memblock_call.error());
    std::vector<std::uint8_t> restored = patched_image;
    const auto restore_call = [&](std::size_t offset, std::uint64_t target) -> Result<void> {
        auto instruction = encode_bl(base + offset, target);
        if (!instruction || !write_u32(&restored, offset, instruction.value()))
            return failure<void>(ErrorCode::kUnsupported, "cannot restore direct-LKM call site");
        return Result<void>::success();
    };
    if (auto status =
            restore_call(async_call.value(), required.value().async_synchronize_full.address);
        !status)
        return propagate<std::vector<std::uint8_t>>(status.error());
    if (auto status = restore_call(strndup_call.value(), required.value().strndup_user.address);
        !status)
        return propagate<std::vector<std::uint8_t>>(status.error());
    if (auto status =
            restore_call(memblock_call.value(), required.value().memblock_reserve.address);
        !status)
        return propagate<std::vector<std::uint8_t>>(status.error());
    if (pool > restored.size() || layout.code_size > restored.size() - pool)
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOutOfRange,
                                                  "direct-LKM bootstrap is outside Image");
    std::fill(restored.begin() + static_cast<std::ptrdiff_t>(pool),
              restored.begin() + static_cast<std::ptrdiff_t>(pool + layout.code_size), 0);
    restored.resize(capsule_offset);
    if (!write_u64(&restored, kImageSizeOffset, capsule_offset))
        return failure<std::vector<std::uint8_t>>(ErrorCode::kOutOfRange,
                                                  "restored Image size is inconsistent");
    return Result<std::vector<std::uint8_t>>::success(std::move(restored));
}

std::optional<std::string> detect_kmi(const std::vector<std::uint8_t>& image) {
    if (image.size() < 4)
        return std::nullopt;
    for (std::size_t index = 0; index <= image.size() - 4; ++index) {
        if (image[index] < '5' || image[index] > '9' || image[index + 1] != '.' ||
            !std::isdigit(image[index + 2]) ||
            (image[index] == '5' && !std::isdigit(image[index + 3])))
            continue;
        const std::size_t limit = std::min(image.size(), index + 100);
        std::size_t end = index;
        while (end < limit && image[end] != 0)
            ++end;
        if (end == limit)
            continue;
        const std::string_view candidate(reinterpret_cast<const char*>(image.data() + index),
                                         end - index);
        if (const auto kmi = parse_kmi_string(candidate))
            return kmi;
    }
    return std::nullopt;
}

}  // namespace ksud::boot::lkm_image
