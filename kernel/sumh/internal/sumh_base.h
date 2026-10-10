#ifndef _SUMH_BASE_H
#define _SUMH_BASE_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/version.h>

#include "sumh_uapi.h"

#include "klog.h" // IWYU pragma: keep

#if defined(__clang__)
#if __clang_major__ >= 17
#define SUMH_NOCFI __attribute__((no_sanitize("cfi", "kcfi")))
#else
#define SUMH_NOCFI __attribute__((no_sanitize("cfi")))
#endif
#else
#define SUMH_NOCFI
#endif

#define SUMH_HASH_BITS 12
#define SUMH_BLOOM_BITS 10
#define SUMH_BLOOM_SIZE (1 << SUMH_BLOOM_BITS)
#define SUMH_BLOOM_MASK (SUMH_BLOOM_SIZE - 1)
#define SUMH_MERGE_HASH_BITS 6
#define SUMH_MERGE_HASH_SIZE (1 << SUMH_MERGE_HASH_BITS)

#define SUMH_PATH_BUF 512
#define SUMH_ITERATE_PATH_BUF 512

#define SUMH_UID_ALLOW_MARKER ((void *)1)
#define SUMH_MAX_MERGE_TARGETS 4

extern bool sumh_debug_enabled;

#define sumh_log(fmt, ...)                                                     \
	(void)(sumh_debug_enabled && pr_info("sumh: " fmt, ##__VA_ARGS__))

#endif /* _SUMH_BASE_H */
