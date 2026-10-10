#pragma once

#include <filesystem>
#include <string>

namespace sumhp {

// Called while holding config.json.lock. Existing SUMHP records take priority;
// only successfully imported records are removed from the legacy directory.
bool migrate_legacy_state(const std::filesystem::path& legacy_dir,
                          const std::filesystem::path& data_dir, std::string& error);

}  // namespace sumhp
