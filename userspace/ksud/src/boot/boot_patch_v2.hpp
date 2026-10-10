#pragma once

#include <string>
#include <vector>

namespace ksud {

// Patch a raw ARM64 kernel inside an Android boot image using the direct LKM
// bootstrap path. With --flash, the command targets boot (and optionally the
// inactive slot) and removes legacy KernelSU ramdisk payloads first.
int boot_patch_v2(const std::vector<std::string>& args);

}  // namespace ksud
