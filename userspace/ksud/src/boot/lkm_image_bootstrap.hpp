#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ksud::boot::lkm_image {

struct BootstrapObjectView {
    const uint8_t* data;
    size_t size;
};

struct BootstrapDefinition {
    std::string_view name;
    uint64_t value;
};

enum class Aarch64Relocation : std::uint16_t {
    kAbs64 = 257,
    kAbs32 = 258,
    kAdrPrelPgHi21 = 275,
    kAddAbsLo12Nc = 277,
    kJump26 = 282,
    kCall26 = 283,
};

inline constexpr std::string_view kTextSection = ".text.ksu_bootstrap";
inline constexpr std::string_view kRodataSection = ".rodata.ksu_bootstrap";
inline constexpr std::string_view kEntrySymbol = "ksu_bootstrap";
inline constexpr std::string_view kReserveWrapperSymbol = "ksu_memblock_reserve_wrapper";
inline constexpr std::string_view kStrndupAdapterSymbol = "ksu_strndup_user_adapter";

namespace symbol {

inline constexpr std::string_view kExtAsyncSynchronizeFull = "ksu_ext_async_synchronize_full";
inline constexpr std::string_view kExtKimageVoffset = "ksu_ext_kimage_voffset";
inline constexpr std::string_view kExtMemstartAddr = "ksu_ext_memstart_addr";
inline constexpr std::string_view kExtVmalloc = "ksu_ext_vmalloc";
inline constexpr std::string_view kExtMemcpy = "ksu_ext_memcpy";
inline constexpr std::string_view kExtLoadModule = "ksu_ext_load_module";
inline constexpr std::string_view kExtMemblockReserve = "ksu_ext_memblock_reserve";
inline constexpr std::string_view kExtStrndupUser = "ksu_ext_strndup_user";
inline constexpr std::string_view kExtKstrdup = "ksu_ext_kstrdup";
inline constexpr std::string_view kImageBase = "ksu_image_base";
inline constexpr std::string_view kCapsuleImageOffset = "ksu_capsule_image_offset";
inline constexpr std::string_view kPageOffset = "ksu_page_offset";
inline constexpr std::string_view kCapsuleMagic = "ksu_capsule_magic";
inline constexpr std::string_view kCapsuleVersion = "ksu_capsule_version";
inline constexpr std::string_view kCapsuleHeaderSize = "ksu_capsule_header_size";
inline constexpr std::string_view kCapsuleSize = "ksu_capsule_size";
inline constexpr std::string_view kModuleCapsuleOffset = "ksu_module_capsule_offset";
inline constexpr std::string_view kModuleSize = "ksu_module_size";
inline constexpr std::string_view kFixupCapsuleOffset = "ksu_fixup_capsule_offset";
inline constexpr std::string_view kFixupCount = "ksu_fixup_count";
inline constexpr std::string_view kLoadInfoSize = "ksu_load_info_size";
inline constexpr std::string_view kLoadInfoHdrOffset = "ksu_load_info_hdr_offset";
inline constexpr std::string_view kLoadInfoLenOffset = "ksu_load_info_len_offset";
inline constexpr std::string_view kReserveExtension = "ksu_reserve_extension";
inline constexpr std::string_view kGfpKernel = "ksu_gfp_kernel";

}  // namespace symbol

/* Implemented by the generated lkm_image_bootstrap_data.cpp. */
BootstrapObjectView bootstrap_object() noexcept;

}  // namespace ksud::boot::lkm_image
