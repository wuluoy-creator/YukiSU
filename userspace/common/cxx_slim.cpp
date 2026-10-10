// libc++abi's default terminate handler pretty-prints the exception type name
// through __cxa_demangle. Resolving that reference against the real demangler
// drags the full C++ name parser and the DWARF unwinder into every binary --
// measured at 131.6 KiB (arm64) and 85.4 KiB (armv7) per target. -fno-exceptions
// does not avoid it: libc++'s prebuilt archive still reaches the demangler
// through operator new's bad_alloc path.
//
// Nothing in ZySU demangles symbols, so satisfy the reference here and let
// libc++abi's archive member stay unlinked. An uncaught exception then reports
// the raw mangled type name, which is still resolvable offline.
//
// Keep this replacement private to each component.

// Every first-party component links this TU, so the API floor is enforced
// everywhere without extra per-target wiring. Keep this include first: bionic's
// headers inject a default __ANDROID_API__ once any of them is seen.
#include "api_floor.hpp"

#include <cstddef>

// The signature must stay ABI-identical to libc++abi's declaration, so the unused
// pointer parameters cannot be made const.
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-non-const-parameter)
extern "C" __attribute__((visibility("hidden"))) char* __cxa_demangle(const char* mangled_name,
                                                                      char* output_buffer,
                                                                      size_t* length, int* status) {
    (void)mangled_name;
    (void)output_buffer;
    (void)length;
    if (status != nullptr) {
        *status = -2;  // libc++abi: invalid mangled name
    }
    return nullptr;
}
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-non-const-parameter)
