#include "hide_bootloader.hpp"
#include "../log.hpp"
#include "../utils.hpp"
#include "ksucalls.hpp"

extern "C" {
#include "uapi/feature.h"
}

#include <array>
#include <string>

namespace ksud {

// Property definitions: {name, expected_value}
struct PropDef {
    const char* name;
    const char* expected;
};
static constexpr PropDef PROPS_TO_HIDE[] = {
    // Generic bootloader/verified boot status
    {"ro.boot.vbmeta.device_state", "locked"},
    {"ro.boot.verifiedbootstate", "green"},
    {"ro.boot.flash.locked", "1"},
    {"ro.boot.veritymode", "enforcing"},
    {"ro.boot.warranty_bit", "0"},
    {"ro.warranty_bit", "0"},
    {"ro.debuggable", "0"},
    {"ro.force.debuggable", "0"},
    {"ro.secure", "1"},
    {"ro.adb.secure", "1"},
    {"ro.build.type", "user"},
    {"ro.build.tags", "release-keys"},
    {"ro.vendor.boot.warranty_bit", "0"},
    {"ro.vendor.warranty_bit", "0"},
    {"vendor.boot.vbmeta.device_state", "locked"},
    {"vendor.boot.verifiedbootstate", "green"},
    {"sys.oem_unlock_allowed", "0"},

    // MIUI specific
    {"ro.secureboot.lockstate", "locked"},

    // Realme specific
    {"ro.boot.realmebootstate", "green"},
    {"ro.boot.realme.lockstate", "1"},

    // OnePlus specific
    {"ro.boot.oem_unlock_support", "0"},
};

namespace {

#if !defined(RESETPROP_ALONE_AVAILABLE) || !RESETPROP_ALONE_AVAILABLE
#error "Hide bootloader requires resetpropAlone to be linked into ksud"
#endif  // #if !defined(RESETPROP_ALONE_AVAILABLE)...
extern "C" int resetprop_main(int argc, char** argv);

/** Get property value in-process (no popen). */
std::string get_prop(const char* name) {
    const auto v = getprop(name);
    return v.value_or("");
}

/**
 * Set property using resetprop.
 * Calls the resetprop entry point linked into ksud; no command is executed.
 * Uses -n to skip init trigger (like Shamiko).
 */
bool reset_prop(const char* name, const char* value) {
    std::array<char*, 5> argv_c = {
        const_cast<char*>("resetprop"),
        const_cast<char*>("-n"),
        const_cast<char*>(name),
        const_cast<char*>(value),
        nullptr,
    };
    return resetprop_main(4, argv_c.data()) == 0;
}

/**
 * Check and reset prop if value doesn't match expected
 */
bool check_reset_prop(const char* name, const char* expected) {
    const std::string value = get_prop(name);

    // Skip if empty (property doesn't exist) or already matches
    if (value.empty() || value == expected) {
        return true;
    }

    LOGI("hide_bl: resetting %s from '%s' to '%s'", name, value.c_str(), expected);
    if (!reset_prop(name, expected)) {
        LOGW("hide_bl: failed to reset %s", name);
        return false;
    }
    if (get_prop(name) != expected) {
        LOGW("hide_bl: %s did not retain the expected value", name);
        return false;
    }
    return true;
}

/**
 * Do bootloader hiding in the current process.
 * Only direct property writes are used: never wait on a later init stage.
 */
bool do_hide_bootloader() {
    LOGI("hide_bl: starting bootloader status hiding...");
    bool success = true;
    for (const auto& prop : PROPS_TO_HIDE) {
        if (!check_reset_prop(prop.name, prop.expected))
            success = false;
    }
    if (success)
        LOGI("hide_bl: bootloader status hiding completed");
    else
        LOGW("hide_bl: bootloader status hiding completed with errors");
    return success;
}

}  // namespace

void hide_bootloader_status() {
    const auto [value, supported] = get_feature(KSU_FEATURE_HIDE_BOOTLOADER);
    if (!supported) {
        LOGW("hide_bl: feature %u is unsupported, skipping", KSU_FEATURE_HIDE_BOOTLOADER);
        return;
    }
    if (value == 0) {
        LOGI("hide_bl: disabled, skipping");
        return;
    }
    // Complete before returning to init so later app processes see the updated
    // properties. Repeated boot stages retry failed or newly populated props.
    (void)do_hide_bootloader();
}

}  // namespace ksud
