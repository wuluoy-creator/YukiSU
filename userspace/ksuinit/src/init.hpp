#pragma once

namespace ksuinit {

// Absolute fallback path for handing off even if replacing /init fails.
const char* real_init_path();

/**
 * Initialize KernelSU
 *
 * This function:
 * 1. Sets up kernel logging via /dev/kmsg
 * 2. Mounts /proc temporarily
 * 3. Loads the KernelSU LKM module
 * 4. Sets up the symlink for the real init
 *
 * @return true when /init was restored; module-load failure does not block boot
 */
bool init();

}  // namespace ksuinit
