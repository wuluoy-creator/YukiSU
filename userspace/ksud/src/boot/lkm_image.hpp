#pragma once

#include "lkm_image_core.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ksud::boot::lkm_image {

struct GkiAbiInfo {
    std::optional<std::uint64_t> load_info_structure_size;
    std::uint64_t load_info_storage_size = 256;
    std::uint64_t load_info_hdr_offset = 16;
    std::uint64_t load_info_len_offset = 24;
    std::uint64_t gfp_kernel = 0xcc0;
};

struct InjectionReport {
    std::string kernel_release;
    std::optional<std::size_t> btf_offset;
    std::optional<std::size_t> btf_size;
    std::size_t btf_type_count = 0;
    std::string kallsyms_layout;
    std::size_t kallsyms_count = 0;
    GkiAbiInfo gki_abi;
    std::size_t code_offset = 0;
    std::size_t code_size = 0;
    std::size_t memblock_call_offset = 0;
    std::uint64_t page_offset = 0;
    std::size_t fixup_count = 0;
    std::vector<std::string> unresolved;
    std::size_t image_size = 0;
    // Set when an existing bootstrap and its recovered kernel metadata were
    // reused instead of re-derived. Mutually exclusive with reuse_skipped_reason.
    bool reused_metadata = false;
    std::string reuse_skipped_reason;
};

struct InjectionResult {
    std::vector<std::uint8_t> image;
    InjectionReport report;
};

// External modules may omit ZySU's load-mode marker.
Result<void> mark_module_image_patch(std::vector<std::uint8_t>* module, bool require_marker = true);

Result<InjectionResult> inject_image(const std::vector<std::uint8_t>& original_image,
                                     const std::vector<std::uint8_t>& module);

// Replace the module capsule in an image that already contains the direct-LKM
// bootstrap. The existing bootstrap and kernel call-site patches are retained.
// With allow_reuse the capsule's own fixup table answers the replacement
// module's symbols, which skips kallsyms and BTF recovery entirely; anything the
// capsule cannot answer falls back to a full re-analysis.
Result<InjectionResult> replace_capsule_module(const std::vector<std::uint8_t>& patched_image,
                                               const std::vector<std::uint8_t>& module,
                                               bool allow_reuse = true);

// True only when the Image already carries a direct-LKM capsule. An unpatched
// raw Image stops before the BSS that its image_size covers, so it is never
// reported as patched.
bool contains_capsule(const std::vector<std::uint8_t>& image);

// Remove the direct-LKM capsule and restore the exact pre-patch raw Image when metadata is present.
Result<std::vector<std::uint8_t>> remove_capsule(const std::vector<std::uint8_t>& patched_image);

// Recover the KMI string from a raw kernel banner for embedded asset lookup.
std::optional<std::string> detect_kmi(const std::vector<std::uint8_t>& image);

}  // namespace ksud::boot::lkm_image
