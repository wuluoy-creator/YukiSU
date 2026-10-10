#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ksud::boot_backup {

struct Backup {
    std::filesystem::path image;
    std::filesystem::path stored_image;
    std::filesystem::path reference;
    std::string sha1;
};

std::string calculate_sha1(const std::filesystem::path& image);
std::optional<Backup> save_stock(const std::filesystem::path& image);
std::optional<Backup> bind_imgpatch(const Backup& backup,
                                    const std::filesystem::path& patched_image);
std::optional<Backup> find_imgpatch(const std::filesystem::path& patched_image,
                                    const std::filesystem::path& workdir);
std::optional<Backup> find_stock(const std::string& sha1, const std::filesystem::path& workdir);
void consume(const Backup& backup);

}  // namespace ksud::boot_backup
