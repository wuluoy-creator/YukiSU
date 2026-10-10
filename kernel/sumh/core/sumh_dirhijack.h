#ifndef _SUMH_DIRHIJACK_H
#define _SUMH_DIRHIJACK_H

#include <linux/path.h>
#include <linux/types.h>

struct inode;
struct inode_operations;
struct super_operations;

int sumh_dirhijack_init(void);
void sumh_dirhijack_stop_new(void);
void sumh_dirhijack_exit(void);

bool sumh_dirhijack_enabled(void);
const struct inode_operations *
sumh_dirhijack_original_iops(const struct inode *inode);

/*
 * Virtual-inode reclaim leaf functions, invoked by the unified super_operations
 * owner (sumh_sop_shadow) from its destroy/evict/drop_inode trampolines with
 * @orig already resolved under RCU.  The trampoline owns the active-counter
 * drain, so these bodies may early-return freely.  Kept here so the
 * load-bearing sumh_vnode reclaim branches stay co-located with the rest of
 * dirhijack.
 */
void sumh_dh_reclaim_destroy_inode(struct inode *inode,
				   const struct super_operations *orig);
void sumh_dh_reclaim_evict_inode(struct inode *inode,
				 const struct super_operations *orig);
int sumh_dh_reclaim_drop_inode(struct inode *inode,
			       const struct super_operations *orig);

/*
 * Register a virtual child at @visible_path backed by @source, projected with
 * stable identity @v_ino.  Resolves the visible parent directory, installs the
 * lookup/superblock hijack there if needed, and indexes the child.  Sleepable
 * context only (control path).  Returns 0 or a negative errno.
 */
int sumh_dirhijack_add(const char *visible_path, const struct path *source,
		       unsigned long v_ino, u8 flags);

int sumh_dirhijack_add_su(const char *visible_path, unsigned long v_ino,
			  struct path *parent);
/* Return 1 for an unchanged binding, 0 for a mismatch, or a negative errno. */
int sumh_dirhijack_match_su(const char *visible_path, const struct path *parent,
			    unsigned long v_ino);
void sumh_dirhijack_del_su(const struct path *parent, const char *name,
			   unsigned long v_ino);

/*
 * Register a lookup-only child at @visible_path backed by @source: VFS lookup
 * resolves it to a SUMH vnode, but dirhijack does not shadow this dir's
 * readdir — the overlay filldir injection keeps emitting and deduping the name.
 * Sinks a merge-materialized file's lookup axis onto the VFS layer (Slice 4a).
 * Sleepable context only.  Returns 0 or a negative errno.
 */
int sumh_dirhijack_add_shadow(const char *visible_path,
			      const struct path *source, unsigned long v_ino,
			      u8 flags);

/* Remove one registered child (by visible path). */
int sumh_dirhijack_del(const char *visible_path);

/*
 * Register a suppress-only ("hide") child at @visible_path: VFS lookup returns
 * a negative dentry and readdir omits the name for hide-target observers.
 * Root and rule-management lookups retain the real entry. Sleepable context
 * only.
 */
int sumh_dirhijack_hide(const char *visible_path);

struct sumh_hide_binding;
int sumh_dirhijack_hide_get(const struct path *parent, const char *name,
			    struct sumh_hide_binding **result);
void sumh_dirhijack_hide_put(struct sumh_hide_binding *binding);
bool sumh_dirhijack_hide_matches(const struct sumh_hide_binding *binding,
				 const struct path *parent);
bool sumh_dirhijack_hidden(struct inode *parent, const char *name, int len);

/*
 * Restore every inode/file/dentry operation shadow, invalidate affected
 * dentries, drain callbacks, and release all dirhijack metadata.  May sleep.
 */
void sumh_dirhijack_clear(void);

#endif /* _SUMH_DIRHIJACK_H */
