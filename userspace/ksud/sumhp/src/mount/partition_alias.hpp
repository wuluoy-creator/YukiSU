#pragma once

#include <filesystem>
#include <optional>

namespace sumhp::mount::fsutil {

// The installer moves partition content to <module>/<part> and leaves
// <module>/system/<part> -> ../<part>. Only follow this local directory alias;
// arbitrary symlinks must retain the backends' normal replacement checks.
inline std::optional<std::filesystem::path> module_partition_alias(
    const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    if (path.parent_path().filename() != "system" || path.filename() == "system")
        return std::nullopt;
    std::error_code ec;
    const auto link = fs::read_symlink(path, ec);
    if (ec)
        return std::nullopt;
    const auto target = path.parent_path().parent_path() / path.filename();
    // Absolute links still point at the original module after mirror copying.
    if (link != fs::path("..") / path.filename() ||
        !fs::is_directory(fs::symlink_status(target, ec)) || ec)
        return std::nullopt;
    return target;
}

}  // namespace sumhp::mount::fsutil
