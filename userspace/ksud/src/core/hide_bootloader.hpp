#pragma once

namespace ksud {

/**
 * Hide bootloader unlock status by resetting system properties
 * This is a "soft" BL hiding method that modifies props at runtime
 *
 * Apply synchronously after post-fs-data scripts and retry in later boot stages.
 * Never waits for boot completion. Only existing properties are changed; this
 * does not modify the bootloader or hardware-backed boot attestation.
 */
void hide_bootloader_status();

}  // namespace ksud
