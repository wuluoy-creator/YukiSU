#!/usr/bin/env python3
"""Exercise SUMH task guards and count lock acquisitions on their empty path.

Runs production function bodies with task/list/lock doubles, including hash
collisions, nested getattr, task switches and error propagation. This is not a
kernel concurrency stress test or a device latency benchmark.
"""

import argparse
import os
from pathlib import Path
import shutil
import sys

# Import the shared freestanding runner without leaving build files in source.
sys.dont_write_bytecode = True
from test_sumh_vfs_helpers import COMMON, ROOT, function, run_test


PREAMBLE = r"""
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define ATOMIC_INIT(x) (x)
#define container_of(p, t, m) ((t *)((char *)(p) - __builtin_offsetof(t, m)))
typedef unsigned int u32;
struct task_struct { __UINTPTR_TYPE__ id; };
static struct task_struct tasks[128], *current;
struct hlist_node { struct hlist_node *next, **pprev; };
struct hlist_head { struct hlist_node *first; };
#define DEFINE_HASHTABLE(name, bits) struct hlist_head name[1 << (bits)]
#define HASH_BITS(table) (__builtin_ctz(sizeof(table) / sizeof((table)[0])))
#define hash_min(key, bits) (((key) >> 3) & ((1UL << (bits)) - 1))
static int lock_count, lock_depth, lock_errors, wakes, sumh_view_active;
#define DEFINE_SPINLOCK(name) int name
#define spin_lock_irqsave(lock, flags) do { \
    (void)(lock); (flags) = 0; lock_errors += !!lock_depth; \
    lock_depth++; lock_count++; \
} while (0)
#define spin_unlock_irqrestore(lock, flags) do { \
    (void)(lock); (void)(flags); lock_errors += lock_depth != 1; lock_depth--; \
} while (0)
static struct hlist_node *locked_first(struct hlist_head *head) {
    lock_errors += lock_depth != 1; return head->first;
}
#define hash_for_each_possible(table, entry, member, key) \
    for (struct hlist_node *node_ = locked_first(&(table)[hash_min(key, HASH_BITS(table))]); \
         node_ && ((entry) = container_of(node_, __typeof__(*(entry)), member), 1); \
         node_ = node_->next)
static void add_node(struct hlist_head *head, struct hlist_node *node) {
    lock_errors += lock_depth != 1;
    node->next = head->first;
    if (head->first) head->first->pprev = &node->next;
    node->pprev = &head->first; head->first = node;
}
#define hash_add(table, node, key) add_node(&(table)[hash_min(key, HASH_BITS(table))], node)
static void hash_del(struct hlist_node *node) {
    lock_errors += lock_depth != 1;
    if (node->next) node->next->pprev = node->pprev;
    *node->pprev = node->next; node->next = NULL; node->pprev = NULL;
}
#define atomic_dec_and_test(p) (--*(p) == 0)
static int sumh_view_wait;
static void wake_up_all(int *wait) { (void)wait; wakes++; }
struct path { int unused; };
struct kstat { int unused; };
static struct task_struct *collision, *other_bucket;
static int vfs_depth, vfs_error, vfs_failures;
static bool sumh_vfs_internal_current(void);
static int sumh_vfs_getattr_unprojected(const struct path *, struct kstat *, u32, unsigned int);
static int vfs_getattr(const struct path *path, struct kstat *stat, u32 mask, unsigned int flags) {
    int before;
    struct task_struct *owner = current;
    if (!sumh_vfs_internal_current()) vfs_failures++;
    if (vfs_depth++ == 0) {
        /* Simulate another task while the owner sleeps in VFS. */
        current = collision; before = lock_count;
        if (sumh_vfs_internal_current() || lock_count != before + 1) vfs_failures++;
        if (sumh_vfs_getattr_unprojected(path, stat, mask, flags) != vfs_error) vfs_failures++;
        if (sumh_vfs_internal_current()) vfs_failures++;
        current = other_bucket; before = lock_count;
        if (sumh_vfs_internal_current() || lock_count != before) vfs_failures++;
        current = owner;
        if (!sumh_vfs_internal_current()) vfs_failures++;
        /* A nested marker must not remove the outer marker on return. */
        if (sumh_vfs_getattr_unprojected(path, stat, mask, flags) != vfs_error) vfs_failures++;
        if (!sumh_vfs_internal_current()) vfs_failures++;
    }
    vfs_depth--;
    return vfs_error;
}
"""


TESTS = r"""
int main(void) {
    struct sumh_view_guard outer, inner, foreign;
    int before;
    current = &tasks[0];
    /* This test hash deliberately collides tasks 0 and 64 in both tables. */
    collision = &tasks[64]; other_bucket = &tasks[1];
    for (int i = 0; i < 10000; i++) {
        CHECK(!sumh_vfs_internal_current());
        CHECK(!sumh_view_guarded());
    }
    CHECK(lock_count == 0);
    CHECK(sumh_vfs_getattr_unprojected(NULL, NULL, 0, 0) == 0);
    CHECK(!sumh_vfs_internal_current() && !vfs_failures);
    vfs_error = -5;
    CHECK(sumh_vfs_getattr_unprojected(NULL, NULL, 0, 0) == -5);
    CHECK(!sumh_vfs_internal_current() && !vfs_failures);

    sumh_view_active++; sumh_view_enter(&outer);
    CHECK(sumh_view_guarded());
    sumh_view_active++; sumh_view_enter(&inner);
    CHECK(sumh_view_guarded());
    sumh_view_leave(&inner);
    CHECK(sumh_view_guarded() && sumh_view_active == 1 && !wakes);
    current = collision; before = lock_count;
    CHECK(!sumh_view_guarded() && lock_count == before + 1);
    sumh_view_active++; sumh_view_enter(&foreign);
    CHECK(sumh_view_guarded());
    current = &tasks[0];
    CHECK(sumh_view_guarded());
    /* Removal of an outer node must retain a colliding task's marker. */
    sumh_view_leave(&outer);
    CHECK(!sumh_view_guarded() && sumh_view_active == 1 && !wakes);
    current = other_bucket; before = lock_count;
    CHECK(!sumh_view_guarded() && lock_count == before);
    current = collision;
    CHECK(sumh_view_guarded());
    sumh_view_leave(&foreign);
    CHECK(!sumh_view_active && wakes == 1);
    before = lock_count;
    CHECK(!sumh_view_guarded() && lock_count == before);
    CHECK(!lock_depth && !lock_errors);
    return 0;
}
"""


def guard_source():
    runtime_path = "kernel/sumh/core/sumh_runtime.c"
    view_path = "kernel/sumh/features/sumh_vfs_view.c"
    runtime = (ROOT / runtime_path).read_text(encoding="utf-8")
    view = (ROOT / view_path).read_text(encoding="utf-8")
    declarations = runtime[runtime.index("#define SUMH_INTERNAL_VFS_HASH_BITS"):
                           runtime.index("bool sumh_vfs_internal_current")]
    declarations += view[view.index("struct sumh_view_guard {"):
                         view.index("static atomic_t sumh_view_active")]
    bodies = function(runtime_path, "bool sumh_vfs_internal_current")
    bodies += function(runtime_path, "int SUMH_NOCFI sumh_vfs_getattr_unprojected")
    for marker in ("static bool sumh_view_guarded", "static void sumh_view_enter",
                   "static void sumh_view_leave"):
        bodies += function(view_path, marker)
    # Kernel unsigned long can hold a pointer; Windows uses LLP64 instead.
    bodies = bodies.replace("(unsigned long)current", "(__UINTPTR_TYPE__)current")
    return COMMON + PREAMBLE + declarations + bodies + TESTS


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("clang") or shutil.which("cc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("a C compiler is required; pass --cc")
    compiler = shutil.which(args.cc) or str(Path(args.cc).resolve())
    run_test(compiler, "task guards (20000 empty checks, zero lock acquisitions)", guard_source())
