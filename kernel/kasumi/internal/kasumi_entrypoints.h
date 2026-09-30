#ifndef _KASUMI_ENTRYPOINTS_H
#define _KASUMI_ENTRYPOINTS_H

#include <asm/ptrace.h>
#include <linux/fs.h>

#include "kasumi_base.h"
#include "kasumi_types.h"

long kasumi_handle_ioctl(unsigned int cmd, void __user *arg);

bool kasumi_policy_current_is_view_target(void);
bool kasumi_policy_current_is_spoof_target(void);

bool kasumi_filldir_filter(struct dir_context *ctx, const char *name,
			   int namlen, loff_t offset, u64 ino,
			   unsigned int d_type);

#endif /* _KASUMI_ENTRYPOINTS_H */
