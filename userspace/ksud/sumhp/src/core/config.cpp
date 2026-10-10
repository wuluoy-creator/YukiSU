#include "sumhp/config.hpp"

#include "core/json.hpp"
#include "core/log.hpp"
#include "core/runtime.hpp"
#include "uapi/sumh.h"
#include "utils.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace sumhp {

class ConfigFileLock {
public:
    ConfigFileLock() = default;
    ConfigFileLock(const ConfigFileLock&) = delete;
    ConfigFileLock& operator=(const ConfigFileLock&) = delete;
    ConfigFileLock(ConfigFileLock&&) = delete;
    ConfigFileLock& operator=(ConfigFileLock&&) = delete;
    ~ConfigFileLock() {
        if (fd_ >= 0) {
            (void)::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
    }

    bool acquire(std::string& error) {
        if (!prepare_private_directory(runtime_data_dir(), error)) {
            return false;
        }
        const std::string lock_path = runtime_config_file().string() + ".lock";
        fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) {
            error = "open " + lock_path + ": " + std::strerror(errno);
            return false;
        }
        if (::flock(fd_, LOCK_EX) != 0) {
            error = "lock " + lock_path + ": " + std::strerror(errno);
            return false;
        }
        return true;
    }

private:
    int fd_ = -1;
};

std::string default_config_json() {
    return R"({
  "mountsource": "KSU",
  "work_dir": "/dev/sumhp",
  "mirror_dir": "",
  "mirror_img_size_mb": 2048,
  "overlay_writable": false,
  "debug": false,
  "verbose": false,
  "fs_type": "auto",
  "enable_kernel_debug": false,
  "enable_stealth": true,
  "enable_overlay_xattr_hide": false,
  "enable_mount_hide": false,
  "mount_hide_mode": "normal",
  "enable_maps_spoof": false,
  "enable_statfs_spoof": false,
  "enable_kernel_build_spoof": false,
  "kernel_build_release": "",
  "kernel_build_version": "",
  "kernel_build_apply_stage": "post-fs-data",
  "overlayfs_enabled": true,
  "magic_mount_enabled": true,
  "mount_backend": "auto",
  "partitions": []
}
)";
}

void prune_config_fields(json::Value& config) {
    static const auto defaults = json::parse(default_config_json());
    for (auto it = config.o.begin(); it != config.o.end();) {
        if (!defaults.find(it->first))
            it = config.o.erase(it);
        else
            ++it;
    }
}

bool validate_config_patch(const json::Value& patch, std::string& error) {
    if (!patch.is_object()) {
        error = "config updates must be a JSON object";
        return false;
    }
    static const auto defaults = json::parse(default_config_json());
    const auto reject = [&error](const std::string& key, const char* reason) {
        error = "configuration field '" + key + "' " + reason;
        return false;
    };
    for (const auto& [key, value] : patch.o) {
        const auto* expected = defaults.find(key);
        if (!expected)
            return reject(key, "is unknown");
        if (value.type != expected->type) {
            const char* type = "must be a positive integer";
            if (expected->is_bool())
                type = "must be a boolean";
            else if (expected->is_string())
                type = "must be a string";
            else if (expected->is_array())
                type = "must be an array of partition names";
            return reject(key, type);
        }
        if (value.is_string() && value.s.find('\0') != std::string::npos)
            return reject(key, "must not contain NUL");
        if (key == "kernel_build_release" || key == "kernel_build_version") {
            if (value.s.size() >= SUMH_KERNEL_BUILD_MAX)
                return reject(key, "must be at most 64 UTF-8 bytes");
            if (std::any_of(value.s.begin(), value.s.end(),
                            [](unsigned char byte) { return byte < 0x20 || byte == 0x7f; }))
                return reject(key, "must not contain control characters");
            if (key == "kernel_build_release" && value.s.find(' ') != std::string::npos)
                return reject(key, "must not contain whitespace");
        }
        if (key == "kernel_build_apply_stage" && value.s != "post-fs-data" &&
            value.s != "boot-completed")
            return reject(key, "must be post-fs-data or boot-completed");
        if (key == "mirror_img_size_mb" && (!std::isfinite(value.n) || value.n < 1 ||
                                            value.n > INT_MAX || std::floor(value.n) != value.n))
            return reject(key, "must be an integer between 1 and 2147483647");
        if ((key == "work_dir" && (value.s.empty() || value.s.front() != '/')) ||
            (key == "mirror_dir" && !value.s.empty() && value.s.front() != '/'))
            return reject(key, "must be an absolute path");
        if (key == "mount_hide_mode" && !value.s.empty() && value.s != "normal" &&
            value.s != "aggressive")
            return reject(key, "must be normal or aggressive");
        if (key == "fs_type" && !value.s.empty() && value.s != "auto" && value.s != "tmpfs" &&
            value.s != "ext4" && value.s != "erofs")
            return reject(key, "must be auto, tmpfs, ext4 or erofs");
        if (key == "mount_backend" && !value.s.empty() && value.s != "auto" && value.s != "sumh" &&
            value.s != "overlay" && value.s != "magic" && value.s != "none")
            return reject(key, "must be auto, sumh, overlay, magic or none");
        if (key == "partitions") {
            for (const auto& partition : value.a) {
                if (!partition.is_string() || partition.s.empty() || partition.s == "." ||
                    partition.s == ".." || partition.s.find('/') != std::string::npos ||
                    partition.s.find('\0') != std::string::npos)
                    return reject(key, "must contain relative partition names without slashes");
            }
        }
    }
    return true;
}

namespace {
bool save_config(const std::string& data, std::string& error) {
    if (ksud::write_file_atomic(runtime_config_file(), data))
        return true;
    error = "write config: " + std::string(std::strerror(errno));
    return false;
}
}  // namespace

bool write_default_config(std::string& error) {
    ConfigFileLock lock;
    return lock.acquire(error) && save_config(default_config_json(), error);
}

namespace {
std::vector<std::string> json_string_array_or_empty(const JsonValue* value) {
    std::vector<std::string> out;
    if (!value || !value->is_array()) {
        return out;
    }
    for (const auto& item : value->a) {
        if (item.is_string()) {
            out.push_back(item.s);
        }
    }
    return out;
}
}  // namespace

bool load_user_hide_rules(std::vector<std::string>& rules, std::string& error) {
    errno = 0;
    const auto input = ksud::read_file((runtime_data_dir() / "user_hide_rules.json").string());
    if (!input) {
        if (errno == ENOENT) {
            rules.clear();
            return true;
        }
        error = "read user hide rules: " + std::string(std::strerror(errno ? errno : EIO));
        return false;
    }
    JsonValue root;
    std::string parse_error;
    if (!parse_json(*input, root, parse_error) || !root.is_array()) {
        error = "invalid user hide rules: " +
                (parse_error.empty() ? std::string("expected a JSON array") : parse_error);
        return false;
    }
    std::vector<std::string> parsed;
    parsed.reserve(root.a.size());
    for (const auto& item : root.a) {
        if (!item.is_string() || item.s.empty() || item.s.front() != '/' ||
            item.s.find('\0') != std::string::npos || item.s.size() >= SUMH_USER_HIDE_PATH_MAX) {
            error = "invalid user hide rule at index " + std::to_string(parsed.size());
            return false;
        }
        parsed.push_back(item.s);
    }
    rules = std::move(parsed);
    return true;
}

std::vector<std::string> load_user_hide_rules() {
    std::vector<std::string> rules;
    std::string error;
    if (!load_user_hide_rules(rules, error))
        logging::write(logging::Level::Warning, "config", error);
    return rules;
}

bool save_user_hide_rules(const std::vector<std::string>& rules) {
    JsonValue root;
    root.type = JsonValue::Type::Array;
    for (const auto& rule : rules)
        root.a.emplace_back(rule);
    return ksud::write_file_atomic(runtime_data_dir() / "user_hide_rules.json",
                                   stringify_json(root, 2) + "\n");
}

namespace {
bool json_bool_or(const JsonValue* root, const char* key, bool fallback) {
    const auto* value = root ? root->find(key) : nullptr;
    return value ? value->bool_or(fallback) : fallback;
}

std::string json_string_or(const JsonValue* root, const char* key, const std::string& fallback) {
    const auto* value = root ? root->find(key) : nullptr;
    return value ? value->string_or(fallback) : fallback;
}

int json_int_or(const JsonValue* root, const char* key, int fallback) {
    const auto* value = root ? root->find(key) : nullptr;
    return value ? static_cast<int>(value->u32_or(static_cast<std::uint32_t>(fallback))) : fallback;
}
}  // namespace

bool parse_config_json(const std::string& json, Config& config, std::string& error) {
    JsonValue root;
    if (!parse_json(json, root, error)) {
        return false;
    }
    if (!root.is_object()) {
        error = "config root must be a JSON object";
        return false;
    }

    // Validate these fields even when loading a manually edited config. A
    // invalid enabled build must never be applied or silently replaced.
    JsonValue build_fields;
    build_fields.type = JsonValue::Type::Object;
    for (const auto* key : {"enable_kernel_build_spoof", "kernel_build_release",
                            "kernel_build_version", "kernel_build_apply_stage"}) {
        if (const auto* value = root.find(key))
            build_fields.o[key] = *value;
    }
    if (!validate_config_patch(build_fields, error))
        return false;
    const bool build_enabled =
        json_bool_or(&root, "enable_kernel_build_spoof", config.enable_kernel_build_spoof);
    const auto build_release =
        json_string_or(&root, "kernel_build_release", config.kernel_build_release);
    const auto build_version =
        json_string_or(&root, "kernel_build_version", config.kernel_build_version);
    if (build_enabled && build_release.empty() && build_version.empty()) {
        error = "kernel build spoof requires a custom kernel_build_release or kernel_build_version";
        return false;
    }

    config.mount_source = json_string_or(&root, "mountsource", config.mount_source);
    config.work_dir = json_string_or(&root, "work_dir", config.work_dir);
    config.mirror_dir = json_string_or(&root, "mirror_dir", config.mirror_dir);
    config.mirror_img_size_mb = json_int_or(&root, "mirror_img_size_mb", config.mirror_img_size_mb);
    config.overlay_writable = json_bool_or(&root, "overlay_writable", config.overlay_writable);
    config.fs_type = json_string_or(&root, "fs_type", config.fs_type);
    config.debug = json_bool_or(&root, "debug", config.debug);
    config.verbose = json_bool_or(&root, "verbose", config.verbose);
    logging::set_debug_enabled(config.debug || config.verbose);
    config.enable_kernel_debug =
        json_bool_or(&root, "enable_kernel_debug", config.enable_kernel_debug);
    config.enable_stealth = json_bool_or(&root, "enable_stealth", config.enable_stealth);
    config.enable_overlay_xattr_hide =
        json_bool_or(&root, "enable_overlay_xattr_hide", config.enable_overlay_xattr_hide);
    config.enable_mount_hide = json_bool_or(&root, "enable_mount_hide", config.enable_mount_hide);
    config.mount_hide_mode = json_string_or(&root, "mount_hide_mode", config.mount_hide_mode);
    if (config.mount_hide_mode != "normal" && config.mount_hide_mode != "aggressive")
        config.mount_hide_mode = "normal";
    config.enable_maps_spoof = json_bool_or(&root, "enable_maps_spoof", config.enable_maps_spoof);
    config.enable_statfs_spoof =
        json_bool_or(&root, "enable_statfs_spoof", config.enable_statfs_spoof);
    config.enable_kernel_build_spoof = build_enabled;
    config.kernel_build_release = build_release;
    config.kernel_build_version = build_version;
    config.kernel_build_apply_stage =
        json_string_or(&root, "kernel_build_apply_stage", config.kernel_build_apply_stage);
    config.overlayfs_enabled = json_bool_or(&root, "overlayfs_enabled", config.overlayfs_enabled);
    config.magic_mount_enabled =
        json_bool_or(&root, "magic_mount_enabled", config.magic_mount_enabled);
    config.mount_backend = json_string_or(&root, "mount_backend", config.mount_backend);
    config.partitions = json_string_array_or_empty(root.find("partitions"));

    return true;
}

bool read_config_file(Config& config, std::string& error) {
    const auto input = ksud::read_file(runtime_config_file().string());
    if (!input) {
        error = "read config: " + std::string(std::strerror(errno));
        return false;
    }
    return parse_config_json(*input, config, error);
}

bool merge_config_json(const std::string& updates, std::string& error) {
    JsonValue patch;
    if (!parse_json(updates, patch, error) || !patch.is_object()) {
        if (error.empty()) {
            error = "config updates must be a JSON object";
        }
        return false;
    }

    if (!validate_config_patch(patch, error))
        return false;
    ConfigFileLock lock;
    if (!lock.acquire(error)) {
        return false;
    }

    const auto input = ksud::read_file(runtime_config_file().string());
    if (!input && errno != ENOENT) {
        error = "read config: " + std::string(std::strerror(errno));
        return false;
    }
    JsonValue root;
    if (!parse_json(input.value_or(default_config_json()), root, error) || !root.is_object()) {
        if (error.empty()) {
            error = "config root must be an object";
        }
        return false;
    }

    prune_config_fields(root);
    prune_config_fields(patch);
    for (const auto& [key, value] : patch.o) {
        root.o[key] = value;
    }
    Config validated;
    if (!parse_config_json(stringify_json(root), validated, error))
        return false;
    const bool saved = save_config(stringify_json(root, 2) + "\n", error);
    if (saved)
        logging::set_debug_enabled(json_bool_or(&root, "debug", false) ||
                                   json_bool_or(&root, "verbose", false));
    return saved;
}

bool reset_kernel_build_config(std::string& error) {
    ConfigFileLock lock;
    if (!lock.acquire(error))
        return false;
    const auto input = ksud::read_file(runtime_config_file().string());
    if (!input && errno != ENOENT) {
        error = "read config: " + std::string(std::strerror(errno));
        return false;
    }
    JsonValue root;
    if (!parse_json(input.value_or(default_config_json()), root, error) || !root.is_object()) {
        if (error.empty())
            error = "config root must be an object";
        return false;
    }
    // Keep unrelated settings and valid drafts, including unknown keys. Reset
    // is also the recovery path for a manually edited invalid build setting.
    const auto defaults = json::parse(default_config_json());
    for (const auto* key :
         {"kernel_build_release", "kernel_build_version", "kernel_build_apply_stage"}) {
        if (const auto* value = root.find(key)) {
            JsonValue field = JsonValue::object();
            field.o[key] = *value;
            std::string validation_error;
            if (!validate_config_patch(field, validation_error))
                root.o[key] = *defaults.find(key);
        }
    }
    root.o["enable_kernel_build_spoof"] = false;
    return save_config(stringify_json(root, 2) + "\n", error);
}

}  // namespace sumhp
