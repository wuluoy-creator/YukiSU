"""Run isolated Kasumi VFS helper regressions against the actual C functions.

The small kernel API doubles make these runnable without a Linux kernel tree.
Use --cc with a native C compiler, or an NDK clang on Windows (no CRT needed).
These tests do not replace a kernel build or concurrent on-device validation.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def function(path, marker):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


COMMON = r"""
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ssize_t;
typedef _Bool bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define KASUMI_NOCFI
#define CHECK(test) do { if (!(test)) return __LINE__; } while (0)
void *memset(void *dst, int value, size_t n) {
    char *d = dst; for (size_t i = 0; i < n; i++) d[i] = value; return dst;
}
static void *memcpy(void *dst, const void *src, size_t n) {
    char *d = dst; const char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
static void *memmove(void *dst, const void *src, size_t n) {
    char *d = dst; const char *s = src;
    if (d < s) for (size_t i = 0; i < n; i++) d[i] = s[i];
    else for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    return dst;
}
static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static size_t strnlen(const char *s, size_t max) {
    size_t n = 0; while (n < max && s[n]) n++; return n;
}
static int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) break;
    }
    return 0;
}
static int strcmp(const char *a, const char *b) {
    return strncmp(a, b, strlen(a) + 1);
}
"""


def xattr_source():
    preamble = r"""
#define EIO 5
#define ENOMEM 12
#define EFAULT 14
#define ERANGE 34
#define EOVERFLOW 75
#define GFP_KERNEL 0
#define XATTR_LIST_MAX 65536
struct dentry { int unused; };
struct kasumi_view_guard { int unused; };
static char input[XATTR_LIST_MAX], allocated[XATTR_LIST_MAX];
static size_t input_len;
static int allocs, frees, reads, active, atomic_context, irq_context;
static int alloc_fail, native_error, invalid_count, wrong_free;
static int in_atomic(void) { return atomic_context; }
static int irqs_disabled(void) { return irq_context; }
static void kasumi_view_enter(struct kasumi_view_guard *g) { (void)g; active++; }
static void kasumi_view_leave(struct kasumi_view_guard *g) { (void)g; active--; }
static void *kvmalloc(size_t n, int flags) {
    (void)n; (void)flags; allocs++; return alloc_fail ? NULL : allocated;
}
static void kvfree(void *p) { frees++; if (p != allocated) wrong_free++; }
static ssize_t kasumi_view_listxattr_orig(struct dentry *d, char *out, size_t n) {
    (void)d; reads++;
    if (native_error) return native_error;
    if (invalid_count) return n + 1;
    if (!n) return input_len;
    if (input_len > n) return -ERANGE;
    if (out) memcpy(out, input, input_len);
    return input_len;
}
static void reset(const char *data, size_t length) {
    if (data) memcpy(input, data, length);
    input_len = length;
    allocs = frees = reads = active = atomic_context = irq_context = 0;
    alloc_fail = native_error = invalid_count = wrong_free = 0;
}
"""
    body = r"""
int main(void) {
    static const char mixed[] = "security.selinux\0trusted.overlay.origin\0user.keep\0user.overlay.test\0";
    static const char kept[] = "security.selinux\0user.keep\0";
    char output[256];
    struct dentry d;
    reset(mixed, sizeof(mixed) - 1);
    CHECK(kasumi_view_listxattr(&d, output, sizeof(output)) == sizeof(kept) - 1);
    CHECK(!strncmp(output, "security.selinux", 17));
    CHECK(!strncmp(output + 17, "user.keep", 10));
    CHECK(allocs == 0 && reads == 1 && active == 0 && frees == 0);
    CHECK(kasumi_view_listxattr(&d, NULL, 0) == sizeof(kept) - 1);
    CHECK(allocs == 0 && active == 0);
    CHECK(kasumi_view_listxattr(&d, output, 1) == -ERANGE);
    CHECK(kasumi_view_listxattr(&d, NULL, 100) == -EFAULT);
    reset("trusted.overlay.origin\0", 23);
    CHECK(kasumi_view_listxattr(&d, NULL, 1) == 0);
    reset(NULL, 0);
    CHECK(kasumi_view_listxattr(&d, output, sizeof(output)) == 0 && allocs == 0);
    reset(NULL, 400);
    for (size_t i = 0; i < 400; i += 10) memcpy(input + i, "user.keep\0", 10);
    CHECK(kasumi_view_listxattr(&d, NULL, 0) == 400);
    CHECK(allocs == 1 && frees == 1 && reads == 2 && active == 0 && !wrong_free);
    reset(NULL, 400); alloc_fail = 1;
    CHECK(kasumi_view_listxattr(&d, NULL, 0) == -ENOMEM && active == 0 && !frees);
    reset(NULL, 0); native_error = -EIO;
    CHECK(kasumi_view_listxattr(&d, NULL, 0) == -EIO && active == 0 && !allocs);
    reset("bad", 3);
    CHECK(kasumi_view_listxattr(&d, output, sizeof(output)) == -EIO);
    reset(NULL, 0); invalid_count = 1;
    CHECK(kasumi_view_listxattr(&d, NULL, 0) == -EOVERFLOW && active == 0);
    reset(mixed, sizeof(mixed) - 1); atomic_context = 1;
    CHECK(kasumi_view_listxattr(&d, NULL, 0) == sizeof(mixed) - 1 && !allocs);
    CHECK(active == 0 && !wrong_free);
    return 0;
}
"""
    helpers = "\n".join(
        function("kernel/kasumi/features/kasumi_xattr_filter.h", marker)
        for marker in ("static inline bool kasumi_overlay_name", "static inline ssize_t kasumi_xattr_filter_list")
    )
    wrapper = function("kernel/kasumi/features/kasumi_vfs_view.c", "static KASUMI_NOCFI ssize_t kasumi_view_listxattr")
    return COMMON + preamble + helpers + wrapper + body


def directory_source():
    preamble = r"""
typedef long long loff_t;
typedef unsigned int u32;
#define ENOENT 2
#define GFP_ATOMIC 0
#define KASUMI_ITERATE_PATH_BUF 128
#define KASUMI_MAX_MERGE_TARGETS 8
#define KASUMI_HASH_BITS 4
#define AS_FLAGS_KASUMI_DIR_HAS_HIDDEN 0
#define AS_FLAGS_KASUMI_DIR_HAS_INJECT 1
#define READ_ONCE(v) (v)
#define ERR_PTR(v) ((void *)(ssize_t)(v))
#define IS_ERR(v) ((ssize_t)(v) < 0)
#define IS_ERR_OR_NULL(v) (!(v) || IS_ERR(v))
enum kasumi_policy_scope { KASUMI_POLICY_SCOPE_NONE, KASUMI_POLICY_SCOPE_VIEW, KASUMI_POLICY_SCOPE_SPOOF };
struct mapping { unsigned long flags; };
struct inode { struct mapping *i_mapping; };
struct qstr { char *name; };
struct dentry { struct inode *inode; struct qstr d_name; int refs; };
struct path { struct dentry *dentry; const char *name; };
struct file { struct path f_path; };
struct dir_context { bool (*actor)(void); loff_t pos; };
struct kasumi_filldir_wrapper {
    struct dir_context wrap_ctx, *orig_ctx;
    struct dentry *parent_dentry;
    int dir_path_len;
    bool dir_has_hidden;
    const char *dir_path;
    bool dir_has_inject, inject_done, view_allowed, spoof_allowed;
    int merge_target_count;
    struct dentry *merge_target_dentries[KASUMI_MAX_MERGE_TARGETS];
    char dir_path_buf[KASUMI_ITERATE_PATH_BUF];
};
struct kasumi_inject_entry { const char *dir; };
struct kasumi_merge_entry { const char *src, *resolved_src; struct dentry *target_dentry; };
static struct kasumi_merge_entry merge;
static int kasumi_ioctl_tgid, kasumi_rule_count = 1, kasumi_enabled = 1;
static int kasumi_stealth_enabled, kasumi_filldir_cache, current;
static int rcu_depth, bad_dget, freed, allocated_count, allow_view = 1;
static struct kasumi_filldir_wrapper allocated[4], *nested_wrapper;
static struct file *nested_file;
static struct dir_context *nested_ctx;
static int nest;
#define atomic_long_read(v) (*(v))
#define atomic_read(v) (*(v))
#define task_tgid_vnr(v) (17)
#define test_bit(bit, flags) ((*(flags) >> (bit)) & 1)
#define hash_min(hash, bits) (0)
#define full_name_hash(p, name, len) ((u32)(len))
#define hlist_for_each_entry_rcu(ie, table, node) for ((ie) = NULL; (ie); (ie) = NULL)
#define hash_for_each_rcu(table, bucket, me, node) for ((bucket) = 0, (me) = &merge; (bucket) < 1; (bucket)++)
static void rcu_read_lock(void) { rcu_depth++; }
static void rcu_read_unlock(void) { rcu_depth--; }
static struct dentry *dget(struct dentry *d) { if (!rcu_depth) bad_dget++; d->refs++; return d; }
static void dput(struct dentry *d) { d->refs--; }
static struct inode *d_inode(struct dentry *d) { return d->inode; }
static int kasumi_policy_current_scope(void) { return allow_view ? KASUMI_POLICY_SCOPE_VIEW : KASUMI_POLICY_SCOPE_NONE; }
static bool kasumi_policy_current_is_hide_target(struct inode *i) { (void)i; return false; }
static bool kasumi_filldir_filter(void) { return true; }
static bool kasumi_merge_filldir(void) { return true; }
static bool ordinary_actor(void) { return true; }
static void *kmem_cache_zalloc(int cache, int flags) {
    (void)cache; (void)flags;
    struct kasumi_filldir_wrapper *w = &allocated[allocated_count++];
    char *p = (char *)w; for (size_t i = 0; i < sizeof(*w); i++) p[i] = 0;
    return w;
}
static void kmem_cache_free(int cache, void *ptr) { (void)cache; (void)ptr; freed++; }
struct kasumi_filldir_wrapper *kasumi_iterate_prepare_wrapper(struct file *, struct dir_context *);
void kasumi_iterate_finish_wrapper(struct kasumi_filldir_wrapper *);
static char *absolute_path(const struct path *path, char *buffer, size_t size) {
    size_t len = strlen(path->name);
    char *out = buffer + size - len - 1;
    memcpy(out, path->name, len + 1);
    if (nest) {
        nest = 0;
        nested_wrapper = kasumi_iterate_prepare_wrapper(nested_file, nested_ctx);
    }
    return out;
}
static char *(*kasumi_d_absolute_path)(const struct path *, char *, size_t) = absolute_path;
static char *dentry_path_raw(struct dentry *d, char *buffer, size_t size) {
    (void)d; (void)buffer; (void)size; return ERR_PTR(-ENOENT);
}
"""
    body = r"""
int main(void) {
    struct mapping mapping = { 2 };
    struct inode inode = { &mapping };
    struct dentry parent = { &inode, { "system" }, 1 };
    struct dentry target = { &inode, { "module" }, 1 };
    struct file first = { { &parent, "/system" } }, second = { { &parent, "/vendor" } };
    struct dir_context ctx = { ordinary_actor, 0 }, second_ctx = { ordinary_actor, 0 };
    struct dir_context internal = { kasumi_merge_filldir, 0 };
    struct kasumi_filldir_wrapper *w;
    merge = (struct kasumi_merge_entry){ "/system", NULL, &target };
    CHECK(kasumi_is_merge_context(&internal));
    CHECK(!kasumi_is_merge_context(&ctx) && !kasumi_is_merge_context(NULL));
    CHECK(!kasumi_iterate_prepare_wrapper(&first, &internal) && allocated_count == 0);
    nested_file = &second; nested_ctx = &second_ctx; nest = 1;
    w = kasumi_iterate_prepare_wrapper(&first, &ctx);
    CHECK(w && nested_wrapper);
    CHECK(!strcmp(w->dir_path, "/system"));
    CHECK(!strcmp(nested_wrapper->dir_path, "/vendor"));
    CHECK(w->merge_target_count == 1 && target.refs == 2 && !bad_dget);
    CHECK(rcu_depth == 0);
    dput(&target); /* Simulate CLEAR releasing the rule after an RCU grace period. */
    CHECK(target.refs == 1);
    w->wrap_ctx.pos = 42;
    kasumi_iterate_finish_wrapper(w);
    CHECK(target.refs == 0 && ctx.pos == 42 && freed == 1);
    kasumi_iterate_finish_wrapper(nested_wrapper);
    CHECK(freed == 2);
    kasumi_iterate_finish_wrapper(NULL);
    CHECK(freed == 2);
    return 0;
}
"""
    guard = function("kernel/kasumi/overlay/kasumi_overlay.c", "bool kasumi_is_merge_context")
    prepare = function("kernel/kasumi/hooks/kasumi_vfs_hooks.c", "KASUMI_NOCFI struct kasumi_filldir_wrapper *")
    finish = function("kernel/kasumi/hooks/kasumi_vfs_hooks.c", "void kasumi_iterate_finish_wrapper")
    return COMMON + preamble + guard + prepare + finish + body


def run_test(compiler, name, source):
    with tempfile.TemporaryDirectory(prefix=f"kasumi-{name}-") as temporary:
        directory = Path(temporary)
        src = directory / "test.c"
        exe = directory / ("test.exe" if os.name == "nt" else "test")
        src.write_text(source, encoding="utf-8")
        flags = ["-std=gnu11", "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-Werror", "-Wno-unused-function", "-Wno-unused-variable"]
        if os.name == "nt":
            obj = directory / "test.obj"
            subprocess.run([compiler, "--target=x86_64-pc-windows-msvc", *flags, "-c", str(src), "-o", str(obj)], check=True)
            linker = Path(compiler).with_name("ld.lld.exe")
            subprocess.run([str(linker), "-flavor", "link", "/entry:main", "/subsystem:console", "/nodefaultlib", f"/out:{exe}", str(obj)], check=True)
        else:
            subprocess.run([compiler, *flags, str(src), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], check=False)
        if result.returncode:
            lines = source.splitlines()
            candidates = [f"{i}: {line.strip()}" for i, line in enumerate(lines, 1)
                          if "CHECK(" in line and i % 256 == result.returncode % 256]
            raise AssertionError(f"{name}: exit {result.returncode}; " + "; ".join(candidates))
        print(f"PASS {name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("clang") or shutil.which("cc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("a C compiler is required; pass --cc")
    compiler = shutil.which(args.cc) or str(Path(args.cc).resolve())
    run_test(compiler, "xattr", xattr_source())
    run_test(compiler, "directory", directory_source())
