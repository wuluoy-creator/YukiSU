#!/usr/bin/env python3
"""Exercise requested/runtime SELinux state using production feature handlers.

The hook and mutex doubles inject runtime failures without a kernel tree. This
does not replace a kernel build or on-device validation of the actual hooks.
"""

import argparse
import os
from pathlib import Path
import shutil
import sys

sys.dont_write_bytecode = True
from test_sumh_vfs_helpers import COMMON, ROOT, function, run_test


def status_source():
    preamble = r"""
typedef unsigned long long u64;
#define EAGAIN 11
#define ENOSYS 38
#define READ_ONCE(x) (x)
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
static int selinux_hide_mutex;
static struct { int status_lock; } selinux_state;
static int lock_depth, lock_errors;
static void mutex_lock(int *lock) {
    if (*lock) lock_errors++;
    *lock = 1; lock_depth++;
}
static void mutex_unlock(int *lock) {
    if (!*lock) lock_errors++;
    *lock = 0; lock_depth--;
}
static bool ksu_selinux_hide_enabled, ksu_selinux_hide_running;
static int ksu_selinux_hide_last_error;
static void *fake_status, *orig_sel_open_handle_status;
static int enable_result, enable_calls, disable_calls;
static int ksu_selinux_hide_enable(void) { enable_calls++; return enable_result; }
static void ksu_selinux_hide_disable(void) { disable_calls++; }
"""
    body = r"""
int main(void) {
    u64 requested, status;
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_DISABLED);

    /* No backup is expected after boot: preserve the setting for next boot. */
    enable_result = -EAGAIN;
    CHECK(!selinux_hide_feature_set(1));
    CHECK(!selinux_hide_feature_get(&requested) && requested == 1);
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_PENDING_REBOOT && enable_calls == 1);

    /* A permanent hook failure must not appear active or merely pending. */
    enable_result = -ENOSYS;
    CHECK(!selinux_hide_feature_set(1));
    CHECK(!selinux_hide_feature_get(&requested) && requested == 1);
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_FAILED && enable_calls == 2);

    /* Policy hooks are insufficient when the status hook or page is absent. */
    enable_result = 0;
    CHECK(!selinux_hide_feature_set(1));
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_FAILED);
    fake_status = &status;
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_FAILED);
    fake_status = NULL;
    orig_sel_open_handle_status = &status;
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_FAILED);
    fake_status = &status;
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_ACTIVE);
    CHECK(!selinux_hide_feature_set(1) && enable_calls == 3);

    CHECK(!selinux_hide_feature_set(0) && disable_calls == 1);
    CHECK(!selinux_hide_feature_get(&requested) && requested == 0);
    CHECK(!selinux_hide_status_get(&status));
    CHECK(status == KSU_SELINUX_HIDE_DISABLED);
    CHECK(!ksu_selinux_hide_last_error && !ksu_selinux_hide_running);
    CHECK(!selinux_hide_feature_set(0) && disable_calls == 1);
    CHECK(!lock_depth && !lock_errors);
    return 0;
}
"""
    handlers = "\n".join(
        function("kernel/feature/selinux_hide.c", marker)
        for marker in (
            "static int selinux_hide_feature_get",
            "static int selinux_hide_status_get",
            "static int selinux_hide_feature_set",
        )
    )
    uapi = (ROOT / "uapi/feature.h").read_text(encoding="utf-8")
    return COMMON + uapi + preamble + handlers + body


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("clang") or shutil.which("cc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("a C compiler is required; pass --cc")
    compiler = shutil.which(args.cc) or str(Path(args.cc).resolve())
    run_test(compiler, "selinux-hide-status", status_source())
