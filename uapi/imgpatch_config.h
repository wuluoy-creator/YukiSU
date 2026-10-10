#ifndef __KSU_UAPI_IMGPATCH_CONFIG_H
#define __KSU_UAPI_IMGPATCH_CONFIG_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

/* "KSUICFG1" in little-endian byte order. */
#define KSU_IMGPATCH_CONFIG_MAGIC 0x314746434955534bULL
#define KSU_IMGPATCH_CONFIG_VERSION 2U

#define KSU_IMGPATCH_CONFIG_ALLOW_SHELL (1ULL << 0)
#define KSU_IMGPATCH_CONFIG_ENABLE_ADBD (1ULL << 1)
/* Bit 2 is retired; do not reuse it. */
#define KSU_IMGPATCH_CONFIG_BUNDLED (1ULL << 3)
#define KSU_IMGPATCH_CONFIG_VALID_FLAGS                                        \
  (KSU_IMGPATCH_CONFIG_ALLOW_SHELL | KSU_IMGPATCH_CONFIG_ENABLE_ADBD |         \
   KSU_IMGPATCH_CONFIG_BUNDLED)

/*
 * Patchable, on-disk ABI stored in the LKM .data section. Keep this block at
 * 512 bytes so future versions can add early-boot settings without changing
 * the marker scanner used by older patchers.
 */
struct ksu_imgpatch_config {
  __u64 magic;
  __u32 version;
  __u32 size;
  __u64 flags;
  __u64 reserved[61];
};

#ifdef __cplusplus
}
#endif // #ifdef __cplusplus

#endif // #ifndef __KSU_UAPI_IMGPATCH_CONFIG_H
