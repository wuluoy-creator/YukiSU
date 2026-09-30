#include "../src/kernel_version.hpp"

using ksud::is_supported_kernel_version;
using ksud::is_supported_kmi;

static_assert(!is_supported_kernel_version(0, 0));
static_assert(!is_supported_kernel_version(4, 19));
static_assert(!is_supported_kernel_version(5, 10));
static_assert(!is_supported_kernel_version(5, 15));
static_assert(!is_supported_kernel_version(5, 99));
static_assert(!is_supported_kernel_version(6, 0));
static_assert(is_supported_kernel_version(6, 1));
static_assert(is_supported_kernel_version(6, 6));
static_assert(is_supported_kernel_version(6, 12));
static_assert(is_supported_kernel_version(6, 18));
static_assert(is_supported_kernel_version(7, 0));

static_assert(!is_supported_kmi("android12-5.10"));
static_assert(!is_supported_kmi("android13-5.10"));
static_assert(!is_supported_kmi("android13-5.15"));
static_assert(!is_supported_kmi("android14-5.15"));
static_assert(!is_supported_kmi("5.99"));
static_assert(!is_supported_kmi("6.0"));
static_assert(!is_supported_kmi("android14-6.0"));
static_assert(is_supported_kmi("android14-6.1"));
static_assert(is_supported_kmi("android15-6.6"));
static_assert(is_supported_kmi("android16-6.12"));
static_assert(is_supported_kmi("android17-6.18"));
static_assert(is_supported_kmi("android18-7.0"));
static_assert(is_supported_kmi("6.1"));
static_assert(is_supported_kmi("6.6"));
static_assert(is_supported_kmi("6.12"));
static_assert(is_supported_kmi("6.18"));
static_assert(is_supported_kmi("7.0"));
static_assert(is_supported_kmi("10.0"));

static_assert(!is_supported_kmi(""));
static_assert(!is_supported_kmi("android"));
static_assert(!is_supported_kmi("android-6.1"));
static_assert(!is_supported_kmi("android14-"));
static_assert(!is_supported_kmi("Android14-6.1"));
static_assert(!is_supported_kmi("android14.6.1"));
static_assert(!is_supported_kmi("android14x-6.1"));
static_assert(!is_supported_kmi("android-14-6.1"));
static_assert(!is_supported_kmi("android+14-6.1"));
static_assert(!is_supported_kmi("-6.1"));
static_assert(!is_supported_kmi("+6.1"));
static_assert(!is_supported_kmi("6.-1"));
static_assert(!is_supported_kmi("6.+1"));
static_assert(!is_supported_kmi("6"));
static_assert(!is_supported_kmi("6."));
static_assert(!is_supported_kmi(".1"));
static_assert(!is_supported_kmi("6..1"));
static_assert(!is_supported_kmi("6.1.0"));
static_assert(!is_supported_kmi("6.1-android14"));
static_assert(!is_supported_kmi(" 6.1"));
static_assert(!is_supported_kmi("6.1 "));
static_assert(!is_supported_kmi("6.1\n"));
static_assert(!is_supported_kmi("android14-6.1-extra"));
static_assert(!is_supported_kmi("999999999999999999999999999999.1"));
static_assert(!is_supported_kmi("6.999999999999999999999999999999"));
static_assert(!is_supported_kmi("android999999999999999999999999999999-6.1"));
static_assert(!is_supported_kmi(std::string_view("6.1\0suffix", 10)));

int main() {}
