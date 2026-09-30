#ifndef __KSU_H_SECCOMP_CACHE
#define __KSU_H_SECCOMP_CACHE

#include <linux/fs.h>

extern void ksu_seccomp_clear_cache(struct seccomp_filter *filter, int nr);
extern void ksu_seccomp_allow_cache(struct seccomp_filter *filter, int nr);

#endif // #ifndef __KSU_H_SECCOMP_CACHE
