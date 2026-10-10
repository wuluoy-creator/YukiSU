#ifndef _SUMH_IOP_OVERRIDE_H
#define _SUMH_IOP_OVERRIDE_H

#include <linux/fs.h>

/* Module init/exit. Returns 0 on success. */
int sumh_iop_override_init(void);
void sumh_iop_override_stop_new(void);
void sumh_iop_override_exit(void);
bool sumh_iop_override_quiesced(void);

/*
 * Install shadow inode_operations on `inode`. Idempotent: safe to call on an
 * already-installed inode (becomes a no-op).
 *
 * After successful install, AS_FLAGS_SUMH_IOP_INSTALLED is set on
 * inode->i_mapping->flags so the slow kprobe path can short-circuit.
 *
 * Returns 0 on success or already-installed; negative errno on failure.
 */
int sumh_iop_install(struct inode *inode);
int sumh_iop_mark_spoof(struct inode *inode);

/*
 * Apply kstat spoofing in place. Extracted from the original vfs_getattr
 * kretprobe ret handler so both the legacy kprobe and the new shadow getattr
 * share one implementation.
 *
 * Caller must have a valid inode and stat. Safe to call from atomic context.
 */
void sumh_apply_kstat_spoof(struct inode *inode, struct kstat *stat);

#endif /* _SUMH_IOP_OVERRIDE_H */
