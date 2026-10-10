#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string_view>

namespace ksud {

// Some filesystems leave d_type unset. Preserve the no-symlink module policy
// while falling back to a descriptor-relative lookup only when necessary.
inline bool is_module_directory(DIR* dir, const dirent& entry) {
    if (entry.d_type == DT_DIR)
        return true;
    if (entry.d_type != DT_UNKNOWN)
        return false;
    struct stat st{};
    return fstatat(dirfd(dir), entry.d_name, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode);
}

// Module IDs follow the installer contract: ^[a-zA-Z][a-zA-Z0-9._-]+$.
inline bool validate_module_id(std::string_view id) {
    const auto is_alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (id.size() < 2 || !is_alpha(id.front()))
        return false;
    for (char c : id) {
        if (!is_alpha(c) && !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-')
            return false;
    }
    return true;
}

}  // namespace ksud
