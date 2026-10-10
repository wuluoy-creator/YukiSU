// Minimum Android API floor for every ZySU userspace component.
//
// Android 12 (API 31) is the minimum supported userspace API. The Linux 6.1
// kernel requirement is enforced separately; Android API levels do not identify
// the device's kernel version.
//
// The check lives here because the build plumbing alone is not trustworthy:
// __ANDROID_API__ is set by the versioned compiler wrapper
// (aarch64-linux-android31-clang++), and a bare `--target=aarch64-linux-android`
// leaves it undefined entirely -- which is why the undefined case is an error
// too, not just a low value.
//
// Include this from a translation unit that every component links.

#pragma once

#if !defined(__ANDROID__)
#error "ZySU userspace targets Android only"
#endif

#if !defined(__ANDROID_API__)
#error \
    "__ANDROID_API__ is undefined: the toolchain was invoked without an API level. Use the versioned NDK wrapper (e.g. aarch64-linux-android31-clang++)."
#endif

// An unversioned target (`--target=aarch64-linux-android`) leaves __ANDROID_API__
// unset on the command line, and bionic's headers then default it to
// __ANDROID_API_FUTURE__ (10000). That sentinel is numerically above any real
// floor, so a plain `< 31` test silently passes for exactly the misconfigured
// build this header exists to reject. Catch it explicitly so the check cannot be
// defeated by including a system header before this one.
#if __ANDROID_API__ >= 10000
#error \
    "__ANDROID_API__ is __ANDROID_API_FUTURE__: no concrete API level was selected. Use the versioned NDK wrapper (e.g. aarch64-linux-android31-clang++)."
#endif

#if __ANDROID_API__ < 31
#error "ZySU userspace requires Android API 31 (Android 12) or newer. Do not lower this."
#endif
