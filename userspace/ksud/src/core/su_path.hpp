#pragma once

#include <string>
#include <vector>

namespace ksud {

// The caller holds SucompatTransitionLock during feature restoration.
int restore_su_path(bool* custom = nullptr);
int su_path_command(const std::vector<std::string>& args);

}  // namespace ksud
