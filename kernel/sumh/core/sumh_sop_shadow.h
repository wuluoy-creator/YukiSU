/* SUMH - explicitly retired per-superblock super_operations owner. */
#ifndef _SUMH_SOP_SHADOW_H
#define _SUMH_SOP_SHADOW_H

#include <linux/fs.h>

int sumh_sop_shadow_init(void);
void sumh_sop_shadow_stop_new(void);
void sumh_sop_shadow_exit(void);

/* One client is registered for each installed dirhijack parent directory. */
int sumh_sop_shadow_register_dh(struct super_block *sb);
void sumh_sop_shadow_unregister_dh(struct super_block *sb);

/* Actual new_inode() objects, distinct from rule/source capture counts. */
bool sumh_sop_vnode_get(struct super_block *sb);
void sumh_sop_vnode_put(struct super_block *sb);

/* Restore and release every owner with no client and no live vnode. */
void sumh_sop_shadow_reap(void);

#endif /* _SUMH_SOP_SHADOW_H */
