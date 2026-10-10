#!/usr/bin/env python3
"""Run SUMH's kernel-build state/validation code with host UTS lock doubles.

The production C source and public argument structure are compiled directly,
with small replacements for kernel APIs. This verifies input rejection and
restoration semantics, not real kernel symbol/KMI or on-device behavior.
"""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]

PRELUDE = r"""
typedef __SIZE_TYPE__ size_t;
typedef unsigned int __u32;
#define bool int
#define true 1
#define false 0
#define NULL ((void *)0)
#define EINVAL 22
#define ENAMETOOLONG 36
#define EOPNOTSUPP 95
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, y) ((x) = (y))
#define BUILD_BUG_ON(x) _Static_assert(!(x), #x)
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define CHECK(id, expr) do { if (!(expr)) return id; } while (0)

static void *memset(void *out, int c, size_t n)
{
    unsigned char *p = out;
    while (n--) *p++ = (unsigned char)c;
    return out;
}
static void *memcpy(void *out, const void *in, size_t n)
{
    unsigned char *p = out;
    const unsigned char *q = in;
    while (n--) *p++ = *q++;
    return out;
}
static int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    while (n--) {
        if (*p != *q) return *p - *q;
        p++; q++;
    }
    return 0;
}
static size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i]) i++;
    return i;
}
static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int strscpy(char *out, const char *in, size_t n)
{
    size_t i = 0;
    if (!n) return -7;
    while (i + 1 < n && in[i]) { out[i] = in[i]; i++; }
    out[i] = 0;
    return in[i] ? -7 : (int)i;
}
struct new_utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65];
};
struct uts_namespace { int refcount; struct new_utsname name; };
struct rw_semaphore { int state; };
static struct uts_namespace initial_uts;
static struct rw_semaphore uts_lock;
static int missing_ns, missing_sem, lock_errors, writes;
static void down_read(struct rw_semaphore *sem)
{
    if (sem != &uts_lock || sem->state < 0) lock_errors++;
    sem->state++;
}
static void up_read(struct rw_semaphore *sem)
{
    if (sem != &uts_lock || sem->state <= 0) lock_errors++;
    sem->state--;
}
static void down_write(struct rw_semaphore *sem)
{
    if (sem != &uts_lock || sem->state) lock_errors++;
    sem->state = -1;
    writes++;
}
static void up_write(struct rw_semaphore *sem)
{
    if (sem != &uts_lock || sem->state != -1) lock_errors++;
    sem->state = 0;
}
static __UINTPTR_TYPE__ find_kernel_symbol_exact(const char *symbol)
{
    if (!strcmp(symbol, "init_uts_ns"))
        return missing_ns ? 0 : (__UINTPTR_TYPE__)&initial_uts;
    if (!strcmp(symbol, "uts_sem"))
        return missing_sem ? 0 : (__UINTPTR_TYPE__)&uts_lock;
    return 0;
}
void sumh_kernel_build_clear(void);
"""

TESTS = r"""
static struct sumh_kernel_build_arg request, reply;
static const char actual_release[] = "6.1.75-android14-11-g16c5f6cd5e9b-ab12268515";
static const char actual_version[] = "#1 SMP PREEMPT Fri Aug 23 03:08:10 UTC 2024";

static void setup_request(int enabled, const char *release, const char *version)
{
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.enable = enabled;
    strscpy(request.release, release, sizeof(request.release));
    strscpy(request.version, version, sizeof(request.version));
}

static int is_actual(void)
{
    return !strcmp(initial_uts.name.release, actual_release) &&
        !strcmp(initial_uts.name.version, actual_version);
}

int run(void)
{
    struct new_utsname before;
    int prior_writes, i;

    CHECK(1, sizeof(request) == 148 &&
        __builtin_offsetof(struct sumh_kernel_build_arg, release) == 16 &&
        __builtin_offsetof(struct sumh_kernel_build_arg, version) == 81);
    setup_request(1, "6.1.99-example", "#2 SMP PREEMPT example");
    CHECK(2, sumh_kernel_build_set(&request) == -EOPNOTSUPP &&
        sumh_kernel_build_get(&reply) == -EOPNOTSUPP);
    strscpy(initial_uts.name.sysname, "Linux", 65);
    strscpy(initial_uts.name.nodename, "phone", 65);
    strscpy(initial_uts.name.machine, "aarch64", 65);
    strscpy(initial_uts.name.release, actual_release, 65);
    strscpy(initial_uts.name.version, actual_version, 65);
    before = initial_uts.name;

    /* Missing symbols and an incompatible UTS identity must not advertise. */
    missing_ns = 1;
    sumh_kernel_build_init();
    CHECK(3, !sumh_kernel_build_available() && !writes);
    missing_ns = 0; missing_sem = 1;
    sumh_kernel_build_init();
    CHECK(4, !sumh_kernel_build_available() && !writes);
    missing_sem = 0;
    initial_uts.name.sysname[0] = 'X';
    sumh_kernel_build_init();
    CHECK(5, !sumh_kernel_build_available() && !writes);
    initial_uts.name = before;
    sumh_kernel_build_init();
    CHECK(6, sumh_kernel_build_available() && !sumh_kernel_build_enabled());
    memset(&reply, 0xa5, sizeof(reply));
    CHECK(7, !sumh_kernel_build_get(&reply) && !reply.enable &&
        reply.size == sizeof(reply) && !strcmp(reply.release, actual_release) &&
        !strcmp(reply.version, actual_version) && !reply.reserved[0] &&
        !reply.reserved[1] && !reply.reserved_tail[0] && !reply.reserved_tail[1]);

    /* Updates retain the first original pair, including all byte padding. */
    CHECK(8, !sumh_kernel_build_set(&request) && sumh_kernel_build_enabled());
    CHECK(9, !strcmp(initial_uts.name.release, request.release) &&
        !strcmp(initial_uts.name.version, request.version));
    CHECK(10, !strcmp(initial_uts.name.sysname, "Linux") &&
        !strcmp(initial_uts.name.nodename, "phone") &&
        !strcmp(initial_uts.name.machine, "aarch64"));
    setup_request(1, "6.1.100-second", "#3 second build");
    CHECK(11, !sumh_kernel_build_set(&request));
    CHECK(12, !sumh_kernel_build_get(&reply) && reply.enable == 1 &&
        !strcmp(reply.release, request.release) &&
        !strcmp(reply.version, request.version));
    before = initial_uts.name;
    prior_writes = writes;

    /* Rejected requests are transactional: neither field may change. */
    request.version[0] = 0;
    CHECK(13, sumh_kernel_build_set(&request) == -EINVAL);
    setup_request(1, "6.1.new", "valid");
    request.version[6] = 'x';
    CHECK(14, sumh_kernel_build_set(&request) == -EINVAL);
    setup_request(1, "6.1.new", "bad\nversion");
    CHECK(15, sumh_kernel_build_set(&request) == -EINVAL);
    setup_request(1, "bad\trelease", "good");
    CHECK(16, sumh_kernel_build_set(&request) == -EINVAL);
    setup_request(1, "6.1.new", "bad\177version");
    CHECK(17, sumh_kernel_build_set(&request) == -EINVAL);
    setup_request(1, "6.1.new", "good");
    memset(request.release, 'r', 65);
    CHECK(18, sumh_kernel_build_set(&request) == -ENAMETOOLONG);
    setup_request(1, "6.1.new", "good");
    memset(request.version, 'v', 65);
    CHECK(19, sumh_kernel_build_set(&request) == -ENAMETOOLONG);
    setup_request(1, "6.1.new", "good");
    request.size--;
    CHECK(20, sumh_kernel_build_set(&request) == -EINVAL);
    request.size++;
    request.enable = 2;
    CHECK(21, sumh_kernel_build_set(&request) == -EINVAL);
    request.enable = 1;
    for (i = 0; i < 2; i++) {
        request.reserved[i] = 1;
        CHECK(22, sumh_kernel_build_set(&request) == -EINVAL);
        request.reserved[i] = 0;
        request.reserved_tail[i] = 1;
        CHECK(23, sumh_kernel_build_set(&request) == -EINVAL);
        request.reserved_tail[i] = 0;
    }
    CHECK(24, !memcmp(&before, &initial_uts.name, sizeof(before)) &&
        writes == prior_writes && sumh_kernel_build_enabled());

    /* Disabling restores originals, rather than the immediately prior spoof. */
    setup_request(0, "", "");
    CHECK(25, !sumh_kernel_build_set(&request) && is_actual() &&
        !sumh_kernel_build_enabled());
    prior_writes = writes;
    CHECK(26, !sumh_kernel_build_set(&request) && writes == prior_writes);
    setup_request(1, "", "");
    CHECK(27, sumh_kernel_build_set(&request) == -EINVAL && is_actual());

    /* Full 64-byte fields, including UTF-8 bytes, remain exact and bounded. */
    setup_request(1, "x", "y");
    memset(request.release, 'r', 64);
    memset(request.version, 'v', 64);
    request.version[62] = (char)0xc3;
    request.version[63] = (char)0xa9;
    CHECK(28, !sumh_kernel_build_set(&request));
    CHECK(29, !sumh_kernel_build_get(&reply) &&
        !memcmp(reply.release, request.release, 65) &&
        !memcmp(reply.version, request.version, 65));
    sumh_kernel_build_clear();
    CHECK(30, is_actual() && !sumh_kernel_build_enabled());

    /* A fresh enable snapshots the current real values, after an external
     * change while off. Reinitialization must not overwrite the saved pair. */
    strscpy(initial_uts.name.release, "6.1.external", 65);
    strscpy(initial_uts.name.version, "#9 external", 65);
    before = initial_uts.name;
    setup_request(1, "6.1.spoof", "#10 spoof");
    CHECK(31, !sumh_kernel_build_set(&request));
    sumh_kernel_build_init();
    sumh_kernel_build_exit();
    CHECK(32, !memcmp(&before, &initial_uts.name, sizeof(before)) &&
        !sumh_kernel_build_available() && !sumh_kernel_build_enabled());
    prior_writes = writes;
    sumh_kernel_build_exit();
    CHECK(33, writes == prior_writes && !uts_lock.state && !lock_errors);
    CHECK(34, sumh_kernel_build_get(&reply) == -EOPNOTSUPP);
    sumh_kernel_build_init();
    CHECK(35, !sumh_kernel_build_get_original(&reply) && !reply.enable &&
        !strcmp(reply.release, "6.1.external") &&
        !strcmp(reply.version, "#9 external"));
    CHECK(36, !sumh_kernel_build_set(&request));
    CHECK(37, !sumh_kernel_build_get_original(&reply) && reply.enable &&
        !strcmp(reply.release, "6.1.external") &&
        !strcmp(reply.version, "#9 external"));
    setup_request(1, "6.1.updated", "#11 updated");
    CHECK(38, !sumh_kernel_build_set(&request));
    CHECK(39, !sumh_kernel_build_get_original(&reply) && reply.enable &&
        !strcmp(reply.release, "6.1.external") &&
        !strcmp(reply.version, "#9 external"));
    sumh_kernel_build_exit();
    CHECK(40, !memcmp(&before, &initial_uts.name, sizeof(before)) &&
        !uts_lock.state && !lock_errors);
    return 0;
}
#ifndef _WIN32
int main(void) { return run(); }
#endif
"""


def test_source():
    uapi = (ROOT / "uapi/sumh.h").read_text(encoding="utf-8")
    limit = re.search(r"^#define SUMH_KERNEL_BUILD_MAX .+$", uapi, re.M).group(0)
    argument = re.search(r"struct sumh_kernel_build_arg \{.*?\n\};", uapi, re.S).group(0)
    source = (ROOT / "kernel/sumh/features/sumh_kernel_build.c").read_text(encoding="utf-8")
    source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.M)
    return PRELUDE + limit + "\n" + argument + "\n" + source + TESTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("clang") or shutil.which("cc"))
    parser.add_argument("--ld", help="Windows: path to LLVM ld.lld")
    args = parser.parse_args()
    if not args.cc:
        parser.error("No C compiler found; pass --cc")
    with tempfile.TemporaryDirectory(prefix="sumh-kernel-build-") as temp:
        directory = Path(temp)
        source = directory / "test.c"
        source.write_text(test_source(), encoding="utf-8")
        output = directory / ("test.exe" if sys.platform == "win32" else "test")
        command = [args.cc, "-std=gnu11", "-Wall", "-Werror", "-ffreestanding",
                   "-fno-stack-protector", "-fno-builtin"]
        if sys.platform == "win32":
            obj = directory / "test.obj"
            subprocess.run(command + ["--target=x86_64-pc-windows-msvc", "-c", str(source), "-o", str(obj)], check=True)
            linker = args.ld or str(Path(args.cc).with_name("ld.lld.exe"))
            subprocess.run([linker, "-flavor", "link", "/entry:run", "/subsystem:console", "/nodefaultlib", f"/out:{output}", str(obj)], check=True)
        else:
            subprocess.run(command + [str(source), "-o", str(output)], check=True)
        result = subprocess.run([str(output)])
        if result.returncode:
            raise AssertionError(f"SUMH kernel-build check {result.returncode} failed")
        print("SUMH kernel build: 40 ABI/state/validation checks passed")


if __name__ == "__main__":
    main()
