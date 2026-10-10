#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ksuinit {

struct VermagicMismatch {
    std::string module_vermagic;
    std::string required_vermagic;
};

/**
 * Extract a module version-magic mismatch from newly emitted, kernel-facility
 * devkmsg records.
 *
 * @param kmsg Kernel log records read after an init_module attempt
 * @param mismatch Receives the module-provided and kernel-required values
 * @return true when an exact version-magic mismatch record was found
 */
bool extract_vermagic_mismatch(std::string_view kmsg, VermagicMismatch& mismatch);

/**
 * Replace the vermagic entry in an ELF64 kernel module's .modinfo section.
 *
 * The rebuilt section is appended to the module image so the replacement may
 * be longer than the original value. The .modinfo section header is updated to
 * point at the new data.
 *
 * @param module ELF64 little-endian kernel module image
 * @param reported_vermagic Module-provided value reported by the kernel
 * @param required_vermagic Kernel-required version magic
 * @param error Receives a human-readable validation error on failure
 * @return true when the module image was updated
 */
bool replace_module_vermagic(std::vector<uint8_t>& module, std::string_view reported_vermagic,
                             std::string_view required_vermagic, std::string& error);

}  // namespace ksuinit
