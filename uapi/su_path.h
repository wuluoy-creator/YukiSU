#ifndef __KSU_UAPI_SU_PATH_H
#define __KSU_UAPI_SU_PATH_H

#include <linux/types.h>

#define KSU_SU_PATH_VERSION 1
#define KSU_SU_PATH_MAX 4096
#define KSU_SU_PATH_DEFAULT "/system/bin/su"
#define KSU_SU_PATH_ENABLED (1U << 0)

struct ksu_su_path_config {
  __u32 version;
  __u32 size;
  __u32 flags;
  __u32 reserved;
  char path[KSU_SU_PATH_MAX];
};

#endif
