#pragma once

#include <map>
#include <string>
#include <vector>

#include "mount/backend.hpp"

namespace sumhp::mount::sumh {

// Mount orchestration honors kernel-build boot scheduling. Explicit runtime
// config apply passes apply_immediately=true. The kernel engine is always on.
bool apply_feature_config(const Config& config, std::string& error, bool apply_immediately = false);

// Reset runtime features without discarding path rules. The SUMH engine
// remains enabled while this cleanup is performed.
bool reset_feature_state(std::string& error);

// Restore user HIDE rules in init's mount namespace after boot completion,
// when SUMH is available. Failures must not roll back module mappings.
bool restore_persisted_hide_rules(std::string& error);
bool has_pending_hide_rules();
// Called by the daemon's event loop; reloads current rules before retrying.
void retry_pending_hide_rules();

// Reset runtime features and clear path rules without requiring an active mirror.
bool reset_runtime_state(std::string& error);

// Compiles each enabled module's /data/adb/modules tree into ADD/MERGE/HIDE
// SUMH rules that redirect straight to the source inode (no mirror; the vnode
// clones the source SELinux SID). Must run in PID 1's mount namespace so the
// rules resolve against the init-namespace view.
bool mount_modules(const std::vector<ModuleEntry>& modules, const Config& config,
                   const ModuleRuleMap& rules);

// Removes SUMHP-owned SUMH rules. SUMH holds no mirror storage of its own.
// Must run in the init mount namespace.
bool unmount_all(const Config& config);

bool is_active();
void invalidate_active_state();
bool has_replayable_mappings();
std::vector<std::string> replayable_module_ids();
bool record_replayable_mappings(const std::vector<ModuleEntry>& modules);
void clear_replayable_mappings();

}  // namespace sumhp::mount::sumh
