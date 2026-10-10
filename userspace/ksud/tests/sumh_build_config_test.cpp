// Exercise the production config parser/merge and KMI source selection without
// filesystem writes, root privileges, or a running SUMH kernel.
#include "cli_args.hpp"
#include "core/json.hpp"
#include "sumhp/config.hpp"
#include "sumhp/embedded.hpp"
#include "sumhp/kernel_build.hpp"
#include "sumhp/sumh_client.hpp"
#include "uapi/sumh.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::optional<std::string> saved_config;
bool saved = false;
bool build_supported = true;
int capabilities_error = 0;
bool original_query_ok = true;
int original_query_error = EIO;
const std::string original_version = "#2 SMP PREEMPT_RT Fri Aug 23 03:08:10 UTC 2024";
std::string system_build_date = "Sun Oct 4 12:30:00 UTC 2026";
bool system_build_date_present = true;
bool boot_completed = false;
int projected_reads = 0;
bool apply_ok = true;
bool kernel_available = true;
bool safe_mode = false;
bool runtime_preparation_ok = true;
int runtime_preparation_calls = 0;
std::string mount_owner;
int build_apply_calls = 0;
int hide_apply_calls = 0;
int boot_errors = 0;
int aggressive_mount_error = 0;
int normal_mount_error = 0;
std::vector<std::pair<bool, sumhp::sumh::MountHideMode>> mount_hide_calls;
sumhp::sumh::KernelBuild live_build;
std::string cli_output;
}  // namespace

namespace ksud {
std::optional<std::string> getprop(const std::string& key) {
    if (key == "ro.build.date") {
        if (!system_build_date_present)
            return std::nullopt;
        return system_build_date;
    }
    assert(key == "sys.boot_completed");
    return boot_completed ? "1" : "0";
}
bool is_safe_mode() {
    return safe_mode;
}
std::optional<std::string> read_file(const std::string&) {
    errno = saved_config ? 0 : ENOENT;
    return saved_config;
}
}  // namespace ksud

namespace sumhp {
bool prepare_runtime(std::string& error) {
    ++runtime_preparation_calls;
    if (!runtime_preparation_ok)
        error = "injected migration failure";
    return runtime_preparation_ok;
}
namespace logging {
void set_debug_enabled(bool) {}
enum class Level { Info, Error };
void write(Level level, const std::string&, const std::string&) {
    if (level == Level::Error)
        ++boot_errors;
}
}  // namespace logging

struct ConfigFileLock {
    bool acquire(std::string&) { return true; }
};
std::filesystem::path runtime_config_file() {
    return "/mock/config.json";
}
bool save_config(const std::string& data, std::string&) {
    saved_config = data;
    saved = true;
    return true;
}
#include "sumh_build_config_under_test.inc"
}  // namespace sumhp

namespace sumhp::sumh {
FeatureCapabilities feature_capabilities() {
    if (capabilities_error)
        return {false, capabilities_error, 0};
    return {true, 0, build_supported ? SUMH_FEATURE_KERNEL_BUILD_SPOOF : 0};
}
bool original_kernel_build(KernelBuild& state) {
    if (!original_query_ok) {
        errno = original_query_error;
        return false;
    }
    state = {true, "6.6.1-android15-original", original_version};
    return true;
}
bool kernel_build(KernelBuild& state) {
    state = live_build;
    return true;
}
bool set_kernel_build(bool enable, const std::string& release, const std::string& version) {
    ++build_apply_calls;
    if (capabilities_error) {
        errno = capabilities_error;
        return false;
    }
    if (enable && !build_supported) {
        errno = EOPNOTSUPP;
        return false;
    }
    if (!apply_ok) {
        errno = EIO;
        return false;
    }
    live_build = {enable, release, version};
    return true;
}
bool is_available() {
    return kernel_available;
}
VersionInfo version_info() {
    return {};
}
int features() {
    return 0;
}
bool clear_maps_rules() {
    return false;
}
bool add_maps_rule(unsigned long, unsigned long, unsigned long, unsigned long, const std::string&) {
    return false;
}
bool clear_rules() {
    return false;
}
bool hide_path(const std::string&) {
    return false;
}
bool delete_rule(const std::string&) {
    return false;
}
bool fix_mounts() {
    return false;
}
bool hide_overlay_xattrs(const std::string&) {
    return false;
}
bool set_mount_hide(bool enable, MountHideMode mode) {
    mount_hide_calls.emplace_back(enable, mode);
    const int error =
        mode == MountHideMode::Aggressive ? aggressive_mount_error : normal_mount_error;
    if (enable && error) {
        errno = error;
        return false;
    }
    return true;
}
bool set_maps_spoof(bool) {
    return true;
}
bool set_statfs_spoof(bool) {
    return true;
}
bool set_debug(bool) {
    return true;
}
bool set_stealth(bool) {
    return true;
}
}  // namespace sumhp::sumh

namespace sumhp {
namespace mount {
void recovery_boot_completed() {}
}  // namespace mount
namespace mount::sumh {
void invalidate_active_state() {}
void mlog(const std::string&) {}
#include "sumh_build_features_under_test.inc"
}  // namespace mount::sumh
std::string arg_or_default(const std::vector<std::string>& args, std::size_t index,
                           const std::string& fallback) {
    return index < args.size() ? args[index] : fallback;
}
int print_sumh_version_json() {
    return 1;
}
int print_sumh_rules_json() {
    return 1;
}
int print_features_json(int) {
    return 1;
}
bool parse_unsigned_long(const std::string&, unsigned long&) {
    return false;
}
void print_usage() {}
#include "sumh_build_cli_under_test.inc"
std::string embedded_external_mount_owner() {
    return mount_owner;
}
int run_via_daemon(const std::vector<std::string>& args) {
    assert((args == std::vector<std::string>{"hide", "apply"}));
    ++hide_apply_calls;
    return 0;
}
#include "sumh_build_boot_under_test.inc"
}  // namespace sumhp

namespace ksud {
struct utsname {
    char release[65] = {};
};
int uname(utsname* uts) {
    ++projected_reads;
    std::strcpy(uts->release, "6.1.75-android14-spoofed");
    return 0;
}
std::string read_kernel_release_from_sysfs() {
    ++projected_reads;
    return "6.1.75-android14-spoofed";
}
#define LOGE(...) ((void)0)
#include "sumh_kmi_under_test.inc"
#undef LOGE
}  // namespace ksud

namespace {
void test_config() {
    using namespace sumhp;
    Config config;
    std::string error;
    assert(parse_config_json(default_config_json(), config, error));
    assert(!config.enable_kernel_build_spoof && config.kernel_build_release.empty() &&
           config.kernel_build_version.empty() &&
           config.kernel_build_apply_stage == "post-fs-data");

    JsonValue fields = JsonValue::object();
    fields.o["enable_kernel_build_spoof"] = true;
    fields.o["kernel_build_release"] = "6.1.75-android14-11-g16c5f6cd5e9b-ab12268515";
    fields.o["kernel_build_version"] = "#1 SMP PREEMPT Fri Aug 23 03:08:10 UTC 2024";
    assert(parse_config_json(stringify_json(fields), config, error));
    assert(config.enable_kernel_build_spoof &&
           config.kernel_build_release == fields.o["kernel_build_release"].s);
    fields.o["enable_kernel_build_spoof"] = false;
    assert(parse_config_json(stringify_json(fields), config, error));
    assert(!config.enable_kernel_build_spoof && !config.kernel_build_release.empty());

    std::string utf8_limit;
    for (int i = 0; i < 32; ++i)
        utf8_limit += "\xc3\xa9";
    fields.o["kernel_build_release"] = utf8_limit;
    assert(parse_config_json(stringify_json(fields), config, error));
    fields.o["kernel_build_release"] = utf8_limit + "\xc3\xa9";
    assert(!parse_config_json(stringify_json(fields), config, error));
    assert(config.kernel_build_release == utf8_limit);
    fields.o["kernel_build_release"] = "release";
    for (const auto& bad : {std::string("version\n"), std::string("version\t"),
                            std::string("a\0b", 3), std::string("a\x7f", 2)}) {
        fields.o["kernel_build_version"] = bad;
        assert(!parse_config_json(stringify_json(fields), config, error));
    }
    fields.o["kernel_build_version"] = "version";
    fields.o["enable_kernel_build_spoof"] = "true";
    assert(!parse_config_json(stringify_json(fields), config, error));

    saved_config = default_config_json();
    saved = false;
    assert(!merge_config_json(R"({"enable_kernel_build_spoof":true})", error));
    assert(!saved);
    assert(merge_config_json(
        R"({"kernel_build_release":"release","kernel_build_version":"version"})", error));
    assert(merge_config_json(R"({"enable_kernel_build_spoof":true})", error));
    saved = false;
    assert(merge_config_json(R"({"kernel_build_version":""})", error));
    const auto enabled_config = saved_config;
    saved = false;
    assert(!merge_config_json(R"({"kernel_build_release":""})", error));
    assert(!saved && saved_config == enabled_config);
    assert(!merge_config_json(R"({"kernel_build_release":"with space"})", error));
    assert(merge_config_json(R"({"kernel_build_apply_stage":"boot-completed"})", error));
    assert(!merge_config_json(R"({"kernel_build_apply_stage":"other"})", error));
    assert(merge_config_json(R"({"enable_kernel_build_spoof":false})", error));
    Config persisted;
    assert(read_config_file(persisted, error));
    assert(!persisted.enable_kernel_build_spoof && persisted.kernel_build_release == "release" &&
           persisted.kernel_build_version.empty() &&
           persisted.kernel_build_apply_stage == "boot-completed");
    // JSON config has no magic default token; only the CLI treats it specially.
    assert(merge_config_json(R"({"kernel_build_release":"default"})", error));
    assert(read_config_file(persisted, error) && persisted.kernel_build_release == "default");
}

void test_kmi_source() {
    assert(ksud::read_kernel_release() == "6.6.1-android15-original");
    assert(projected_reads == 0);
    original_query_ok = false;
    assert(ksud::read_kernel_release().empty());
    assert(projected_reads == 0);
    build_supported = false;
    assert(ksud::read_kernel_release() == "6.1.75-android14-spoofed");
    assert(projected_reads == 1);
    build_supported = true;
    capabilities_error = EIO;
    assert(ksud::read_kernel_release().empty() && projected_reads == 1);
    original_query_ok = true;
    assert(ksud::read_kernel_release() == "6.6.1-android15-original" && projected_reads == 1);
    original_query_ok = false;
    for (const int error : {EIO, EACCES, EPERM, EPROTO, EINVAL, EINTR}) {
        capabilities_error = error;
        original_query_error = ENOTTY;
        assert(ksud::read_kernel_release().empty() && projected_reads == 1);
        capabilities_error = ENOTTY;
        original_query_error = error;
        assert(ksud::read_kernel_release().empty() && projected_reads == 1);
    }
    for (const int error : {ENOTTY, EOPNOTSUPP, ENOSYS, ENODEV}) {
        capabilities_error = original_query_error = error;
        assert(ksud::read_kernel_release() == "6.1.75-android14-spoofed");
    }
    assert(projected_reads == 5);
    capabilities_error = 0;
    original_query_ok = true;
}

int run_cli(const std::vector<std::string>& args) {
    std::ostringstream output;
    std::ostringstream errors;
    auto* previous_output = std::cout.rdbuf(output.rdbuf());
    auto* previous_errors = std::cerr.rdbuf(errors.rdbuf());
    std::vector<std::string> input = {"sumhp"};
    input.insert(input.end(), args.begin(), args.end());
    ksud::CliArguments parsed;
    const int parse_result = ksud::parse_cli(input, parsed);
    const int result = parse_result == -1 ? sumhp::handle_sumh(parsed.args) : parse_result;
    std::cout.rdbuf(previous_output);
    std::cerr.rdbuf(previous_errors);
    cli_output = output.str();
    return result;
}

void test_cli() {
    using namespace sumhp;
    saved_config = default_config_json();
    saved = false;
    build_supported = true;
    std::string error;
    assert(run_cli({"sumh", "kernel-build", "set", "6.1.75-android14-example", "#1 test"}) == 0);
    assert(live_build.enabled && live_build.release == "6.1.75-android14-example");
    Config config;
    assert(read_config_file(config, error));
    assert(config.enable_kernel_build_spoof && config.kernel_build_version == "#1 test");

    assert(run_cli({"sumh", "kernel-build", "show"}) == 0);
    const auto snapshot = json::parse(cli_output);
    assert(snapshot.find("enabled")->b && snapshot.find("release")->s == live_build.release);
    assert(snapshot.find("original_release")->s == "6.6.1-android15-original");
    assert(snapshot.find("original_version")->s == original_version);
    assert(snapshot.find("suggested_version")->s ==
           "#2 SMP PREEMPT_RT Sun Oct 4 12:30:00 UTC 2026");
    assert(snapshot.find("suggestion_source")->s == "ro.build.date");
    system_build_date_present = false;
    assert(run_cli({"sumh", "kernel-build", "show"}) == 0);
    const auto without_date = json::parse(cli_output);
    assert(without_date.find("suggested_version")->s.empty() &&
           without_date.find("suggestion_source")->s.empty());
    system_build_date_present = true;
    original_query_ok = false;
    assert(run_cli({"sumh", "kernel-build", "show"}) != 0);
    original_query_ok = true;
    const auto previous_config = saved_config;
    apply_ok = false;
    assert(run_cli({"sumh", "kernel-build", "set", "different", "#2 different"}) != 0);
    assert(saved_config == previous_config && live_build.release == "6.1.75-android14-example");
    apply_ok = true;

    saved = false;
    assert(run_cli({"sumh", "kernel-build", "set", "missing-version"}) != 0);
    assert(run_cli({"sumh", "kernel-build", "set", "", "#1 test"}) != 0);
    assert(run_cli({"sumh", "kernel-build", "set", std::string(65, 'x'), "#1 test"}) != 0);
    assert(run_cli({"sumh", "kernel-build", "set", "release", "#1\ntest"}) != 0);
    assert(!saved);
    assert(run_cli({"sumh", "kernel-build", "reset"}) == 0);
    assert(!live_build.enabled);
    assert(read_config_file(config, error));
    assert(!config.enable_kernel_build_spoof &&
           config.kernel_build_release == "6.1.75-android14-example");
    assert(run_cli({"sumh", "kernel-build", "set", "default", "#4 new build"}) == 0);
    assert(read_config_file(config, error) && config.kernel_build_release.empty() &&
           config.kernel_build_version == "#4 new build");
    assert(run_cli({"sumh", "kernel-build", "set", "new-release", "default"}) == 0);
    assert(read_config_file(config, error) && config.kernel_build_release == "new-release" &&
           config.kernel_build_version.empty());
    const auto before_defaults = saved_config;
    assert(run_cli({"sumh", "kernel-build", "set", "default", "default"}) != 0);
    assert(saved_config == before_defaults);

    saved_config =
        R"({"mountsource":"MY-KSU","future-setting":42,"enable_kernel_build_spoof":"bad","kernel_build_release":"bad release","kernel_build_version":"#7 saved","kernel_build_apply_stage":"wrong"})";
    assert(run_cli({"sumh", "kernel-build", "reset"}) == 0);
    assert(read_config_file(config, error) && !config.enable_kernel_build_spoof &&
           config.kernel_build_release.empty() && config.kernel_build_version == "#7 saved" &&
           config.kernel_build_apply_stage == "post-fs-data" && config.mount_source == "MY-KSU");
    assert(json::parse(*saved_config).find("future-setting")->n == 42);
    saved_config =
        R"({"mountsource":"MY-KSU","enable_kernel_build_spoof":true,"kernel_build_release":"invalid release"})";
    capabilities_error = EIO;
    saved = false;
    assert(run_cli({"sumh", "kernel-build", "reset"}) != 0);
    assert(saved && read_config_file(config, error) && !config.enable_kernel_build_spoof &&
           config.mount_source == "MY-KSU" && config.kernel_build_release.empty());
    capabilities_error = 0;
    // An invalid JSON document cannot be repaired without losing settings;
    // reset still restores the live identity and reports persistence failure.
    saved_config = "not json";
    assert(run_cli({"sumh", "kernel-build", "reset"}) != 0 && !live_build.enabled);
    assert(*saved_config == "not json");
    saved_config = default_config_json();

    build_supported = false;
    saved = false;
    assert(run_cli({"sumh", "kernel-build", "set", "release", "version"}) != 0);
    assert(!saved);
    assert(run_cli({"sumh", "kernel-build", "reset"}) == 0);
}

void test_boot_restore() {
    using namespace sumhp;
    mount_owner = "external.metamodule";
    build_supported = kernel_available = apply_ok = true;
    safe_mode = false;
    saved_config =
        R"({"enable_kernel_build_spoof":true,"kernel_build_release":"release","kernel_build_version":"#1 version"})";
    const auto persisted = saved_config;
    build_apply_calls = hide_apply_calls = boot_errors = 0;
    runtime_preparation_calls = 0;
    saved = false;
    // The post-fs-data hook restores identity before Zygote, including when
    // an external backend will handle mounting. It does not restore mounts.
    embedded_restore_kernel_build();
    assert(build_apply_calls == 1 && live_build.enabled && live_build.release == "release");
    assert(hide_apply_calls == 0 && boot_errors == 0 && !saved && saved_config == persisted);
    assert(runtime_preparation_calls == 1);
    runtime_preparation_ok = false;
    embedded_restore_kernel_build();
    assert(build_apply_calls == 1 && boot_errors == 1 && saved_config == persisted);
    runtime_preparation_ok = true;
    boot_errors = 0;

    std::string error;
    assert(merge_config_json(R"({"kernel_build_apply_stage":"boot-completed"})", error));
    Config late;
    assert(read_config_file(late, error));
    build_apply_calls = 0;
    embedded_restore_kernel_build();
    assert(build_apply_calls == 0);
    // Module mount orchestration must not bypass the selected late stage.
    assert(mount::sumh::apply_feature_config(late, error, false));
    assert(build_apply_calls == 0);
    // An explicit runtime apply ignores scheduling, even before boot complete.
    assert(mount::sumh::apply_feature_config(late, error, true));
    assert(build_apply_calls == 1);
    embedded_boot_completed();
    assert(build_apply_calls == 2);
    boot_completed = true;
    assert(mount::sumh::apply_feature_config(late, error, false));
    assert(build_apply_calls == 3);
    boot_completed = false;
    late.kernel_build_apply_stage = "post-fs-data";
    assert(mount::sumh::apply_feature_config(late, error, false));
    assert(build_apply_calls == 4);
    late.kernel_build_apply_stage = "boot-completed";
    late.enable_kernel_build_spoof = false;
    assert(mount::sumh::apply_feature_config(late, error, false));
    assert(build_apply_calls == 5 && !live_build.enabled);
    saved_config = persisted;
    saved = false;

    build_apply_calls = 0;
    safe_mode = true;
    embedded_boot_completed();
    assert(build_apply_calls == 0 && hide_apply_calls == 0);
    safe_mode = false;
    kernel_available = false;
    embedded_boot_completed();
    assert(build_apply_calls == 0);
    kernel_available = true;
    saved_config.reset();
    embedded_boot_completed();
    assert(build_apply_calls == 0 && boot_errors == 0 && !saved_config);
    saved_config = R"({"enable_kernel_build_spoof":true})";
    embedded_boot_completed();
    assert(build_apply_calls == 0 && boot_errors == 1);

    saved_config = persisted;
    apply_ok = false;
    embedded_boot_completed();
    assert(build_apply_calls == 1 && boot_errors == 2 && saved_config == persisted);
    apply_ok = true;
    saved_config = default_config_json();
    build_supported = false;
    embedded_boot_completed();
    assert(!live_build.enabled && boot_errors == 2);

    // The independent feature restore keeps the existing built-in hide flow.
    mount_owner.clear();
    embedded_boot_completed();
    assert(hide_apply_calls == 1 && !saved);
}

void test_retired_mount_mode_restore() {
    using sumhp::sumh::MountHideMode;
    sumhp::Config config;
    config.enable_mount_hide = true;
    config.mount_hide_mode = "aggressive";
    std::string error;
    mount_hide_calls.clear();
    aggressive_mount_error = EOPNOTSUPP;
    assert(sumhp::mount::sumh::apply_feature_config(config, error, false));
    assert(error.empty() && mount_hide_calls.size() == 2);
    assert(mount_hide_calls[0].first && mount_hide_calls[0].second == MountHideMode::Aggressive);
    assert(mount_hide_calls[1].first && mount_hide_calls[1].second == MountHideMode::Normal);

    // Transport errors are not evidence that the requested mode was retired.
    mount_hide_calls.clear();
    aggressive_mount_error = EIO;
    assert(!sumhp::mount::sumh::apply_feature_config(config, error, false));
    assert(!error.empty() && mount_hide_calls.size() == 2);
    assert(!mount_hide_calls.back().first);  // Existing failure cleanup remains active.

    mount_hide_calls.clear();
    error.clear();
    aggressive_mount_error = EOPNOTSUPP;
    normal_mount_error = EIO;
    assert(!sumhp::mount::sumh::apply_feature_config(config, error, false));
    assert(!error.empty() && mount_hide_calls.size() == 3);
    assert(!mount_hide_calls.back().first);

    mount_hide_calls.clear();
    error.clear();
    aggressive_mount_error = normal_mount_error = 0;
    assert(sumhp::mount::sumh::apply_feature_config(config, error, false));
    assert(error.empty() && mount_hide_calls.size() == 1);
    assert(mount_hide_calls[0].second == MountHideMode::Aggressive);
    mount_hide_calls.clear();
}

void test_date_suggestion() {
    using sumhp::suggest_kernel_build_version;
    const std::string prefix = "#2 SMP PREEMPT_RT ";
    assert(suggest_kernel_build_version(original_version, system_build_date) ==
           prefix + system_build_date);
    assert(suggest_kernel_build_version("#17 Fri Aug 23 03:08:10 UTC 2024",
                                        "Sun Oct  4 12:30:00 +0800 2026") ==
           "#17 Sun Oct 4 12:30:00 +0800 2026");
    for (const auto& invalid :
         {std::string{}, std::string("2026-10-04"), std::string("Sun Oct 4 12:30:00 UTC 2026\n"),
          std::string("Sun Oct 4 25:30:00 UTC 2026"), std::string("Mon Feb 29 12:30:00 UTC 2025"),
          std::string("Sun Oct 4 12:30:00 +2460 2026"),
          std::string("Sun Oct 4 12:30:00 UTC 9999999999999999"),
          system_build_date + std::string("\0", 1)}) {
        assert(suggest_kernel_build_version(original_version, invalid).empty());
    }
    assert(suggest_kernel_build_version(original_version, "Thu Feb 29 12:30:00 UTC 2024") ==
           prefix + "Thu Feb 29 12:30:00 UTC 2024");
    assert(suggest_kernel_build_version("#1 unsupported date format", system_build_date).empty());
    assert(suggest_kernel_build_version("unknown Fri Aug 23 03:08:10 UTC 2024", system_build_date)
               .empty());
    const std::string long_prefix = "#1 " + std::string(33, 'X') + " ";
    const auto maximum = long_prefix + "Fri Aug 2 03:08:10 UTC 2024";
    assert(maximum.size() <= 64);
    assert(suggest_kernel_build_version(maximum, "Sun Oct 14 12:30:00 +0800 2026").empty());
}
}  // namespace

int main() {
    test_config();
    test_kmi_source();
    test_cli();
    test_boot_restore();
    test_retired_mount_mode_restore();
    test_date_suggestion();
    std::cout << "sumh_build_config_test: all checks passed\n";
}
