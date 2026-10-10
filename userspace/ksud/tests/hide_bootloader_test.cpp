// The runner includes the production implementation with property and feature
// access replaced by this deterministic fixture; no Android or root is needed.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "uapi/feature.h"

namespace ksud {
namespace fixture {
bool supported = true;
uint64_t enabled = 1;
int warnings = 0;
std::map<std::string, std::string> properties;
std::vector<std::string> writes;
std::string failed_property;
std::string ignored_property;

void reset() {
    supported = true;
    enabled = 1;
    warnings = 0;
    properties = {{"sys.boot_completed", "0"},
                  {"ro.boot.verifiedbootstate", "orange"},
                  {"ro.boot.vbmeta.device_state", "unlocked"},
                  {"ro.build.type", "user"}};
    writes.clear();
    failed_property.clear();
    ignored_property.clear();
}
}  // namespace fixture

std::pair<uint64_t, bool> get_feature(uint32_t id) {
    assert(id == KSU_FEATURE_HIDE_BOOTLOADER);
    return {fixture::enabled, fixture::supported};
}

std::optional<std::string> getprop(const std::string& name) {
    auto found = fixture::properties.find(name);
    if (found == fixture::properties.end())
        return std::nullopt;
    return found->second;
}

template <typename... Args>
void log_i(const char*, Args...) {}

template <typename... Args>
void log_w(const char*, Args...) {
    ++fixture::warnings;
}

extern "C" int resetprop_main(int argc, char** argv) {
    assert(argc == 4 && argv[4] == nullptr);
    assert(std::string(argv[0]) == "resetprop");
    // Any boot-completion wait would fail immediately, rather than hanging.
    assert(std::string(argv[1]) == "-n");
    const std::string name = argv[2];
    fixture::writes.push_back(name);
    if (name == fixture::failed_property)
        return 1;
    if (name != fixture::ignored_property)
        fixture::properties[name] = argv[3];
    return 0;
}
}  // namespace ksud

#define RESETPROP_ALONE_AVAILABLE 1
#define LOGI(...) ksud::log_i(__VA_ARGS__)
#define LOGW(...) ksud::log_w(__VA_ARGS__)
#include "hide_bootloader_under_test.inc"

int main() {
    using namespace ksud;
    using namespace ksud::fixture;

    // Apply before boot completion, without creating vendor-specific props or
    // rewriting values that already match. Later stages must be idempotent.
    reset();
    hide_bootloader_status();
    assert(properties.at("sys.boot_completed") == "0");
    assert(properties.at("ro.boot.verifiedbootstate") == "green");
    assert(properties.at("ro.boot.vbmeta.device_state") == "locked");
    assert(properties.count("ro.boot.realmebootstate") == 0);
    assert(writes.size() == 2 && warnings == 0);
    hide_bootloader_status();
    assert(writes.size() == 2 && warnings == 0);

    // Late vendor properties are picked up by a later stage.
    properties["ro.boot.realmebootstate"] = "orange";
    hide_bootloader_status();
    assert(properties.at("ro.boot.realmebootstate") == "green");
    assert(writes.size() == 3);

    // Each invocation rechecks the feature so disabling it stops later retries.
    reset();
    enabled = 0;
    hide_bootloader_status();
    assert(writes.empty());
    enabled = 1;
    supported = false;
    hide_bootloader_status();
    assert(writes.empty());

    // A single failure must not prevent other props from being applied and
    // must remain eligible for retry on the next boot-stage callback.
    reset();
    failed_property = "ro.boot.vbmeta.device_state";
    hide_bootloader_status();
    assert(properties.at(failed_property) == "unlocked");
    assert(properties.at("ro.boot.verifiedbootstate") == "green");
    assert(writes.size() == 2 && warnings == 2);
    failed_property.clear();
    hide_bootloader_status();
    assert(properties.at("ro.boot.vbmeta.device_state") == "locked");
    assert(writes.size() == 3);

    // A resetprop success code without a persisted value is still a failure.
    reset();
    ignored_property = "ro.boot.vbmeta.device_state";
    hide_bootloader_status();
    assert(properties.at(ignored_property) == "unlocked");
    assert(warnings == 2);
    ignored_property.clear();
    hide_bootloader_status();
    assert(properties.at("ro.boot.vbmeta.device_state") == "locked");

    std::puts("hide bootloader regressions passed");
}
