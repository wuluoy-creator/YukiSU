#pragma once

#include <string>

namespace ksud {

// Ensure BINARY_DIR and the multi-call symlinks exist. Returns 0 on success.
int ensure_binaries(bool ignore_if_exist);

}  // namespace ksud
