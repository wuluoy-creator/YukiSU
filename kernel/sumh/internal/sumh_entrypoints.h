#ifndef _SUMH_ENTRYPOINTS_H
#define _SUMH_ENTRYPOINTS_H

#include <asm/ptrace.h>
#include <linux/fs.h>

#include "sumh_base.h"
#include "sumh_types.h"

long sumh_handle_ioctl(unsigned int cmd, void __user *arg);

bool sumh_policy_current_is_view_target(void);
bool sumh_policy_current_is_spoof_target(void);
bool sumh_policy_current_is_mount_view_target(void);

bool sumh_filldir_filter(struct dir_context *ctx, const char *name, int namlen,
			 loff_t offset, u64 ino, unsigned int d_type);

#endif /* _SUMH_ENTRYPOINTS_H */
