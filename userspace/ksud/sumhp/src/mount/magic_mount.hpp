#pragma once

// Magic Mount backend: Magisk-style systemless mounting (tmpfs skeleton +
// bind-mount + MS_MOVE) over module partition trees. File binds and directory
// skeletons are tracked by a boot-scoped mount identity journal.

#include <string>
#include <vector>

#include "mount/backend.hpp"
#include "sumhp/config.hpp"

namespace sumhp::mount::magic {

// Mount every module's partition trees over the live filesystem. Must run inside
// the init mount namespace; records committed mount points to magic_mounts.list.
// The staging tmpfs is detached after commit; only final targets remain mounted.
bool mount_modules(const std::vector<ModuleEntry>& modules, const Config& config);

// Detach SUMHP's recorded mounts and clean skeletons. Must run in the init ns.
bool unmount_all(const Config& config);

std::vector<std::string> active_mounts(const Config& config);
bool is_active(const Config& config);

// Install-time layout normalization (metainstall.sh): fold <module>/<p> into
// <module>/system/<p> for partitions exposed as /system/<p> symlinks.
bool normalize_module(const std::string& module_path);

}  // namespace sumhp::mount::magic
