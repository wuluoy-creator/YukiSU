#ifndef _KASUMI_RUNTIME_H
#define _KASUMI_RUNTIME_H

#include "kasumi_base.h"
#include "infra/symbol_resolver.h"
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/security.h>

/* GKI exports this without a public namei.h declaration. */
int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
		    const char *name, unsigned int flags, struct path *path);

#include <linux/anon_inodes.h>
#include <linux/bitmap.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/kprobes.h>
#include <linux/limits.h>
#include <linux/llist.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/srcu.h>
#include <linux/stat.h>
#include <linux/task_work.h>
#include <linux/vmalloc.h>

#include "kasumi_base.h"
#include "kasumi_types.h"

extern bool kasumi_enabled;
extern atomic_t kasumi_rule_count;
extern atomic_t kasumi_hide_count;
extern atomic_t kasumi_spoof_kstat_count;

struct kasumi_hook_stats {
	atomic64_t iop_getattr_entries;
	atomic64_t iop_getattr_spoofs;
	atomic64_t statfs_entries;
	atomic64_t statfs_spoofs;
	atomic64_t iterate_fop_entries;
	atomic64_t iterate_fop_wrapped;
	atomic64_t filldir_hidden;
	atomic64_t filldir_injected;
};

extern struct kasumi_hook_stats kasumi_hook_stats;

extern atomic_long_t kasumi_ioctl_tgid;
extern struct kmem_cache *kasumi_filldir_cache;

bool kasumi_valid_kernel_addr(unsigned long addr);
#define kasumi_lookup_name find_kernel_symbol_exact
#define kasumi_lookup_name_quiet find_kernel_symbol_exact
static inline unsigned long kasumi_lookup_callable(const char *name)
{
	return (unsigned long)ksu_lookup_symbol(name);
}
#define kasumi_lookup_callable_quiet kasumi_lookup_callable
dev_t kasumi_vnode_device(void);
unsigned long kasumi_vnode_source_ino(dev_t source_dev, u64 source_ino);
unsigned long kasumi_vnode_vpath_ino(const char *vpath);
unsigned long kasumi_vnode_ino_alloc(dev_t src_dev, u64 src_ino);
dev_t kasumi_vnode_visible_dev(const char *visible_path);
u64 kasumi_vnode_allocated(void);
unsigned int kasumi_vnode_live(void);

extern bool kasumi_stealth_enabled;

extern int kasumi_mount_hide_vfsmnt_registered;
extern int kasumi_mount_hide_mountinfo_registered;
extern int kasumi_proc_proxy_registered;
extern int kasumi_proc_ns_readlink_registered;
extern int kasumi_feature_enabled_mask;
extern int kasumi_mount_hide_mode;
extern int kasumi_fscap_kretprobe_registered;
extern int kasumi_fscaps_enabled;
extern int kasumi_device_sources_enabled;
extern dev_t kasumi_system_dev;

/* notify_change first arg (idmap/userns) varies across kernel versions. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
extern int (*kasumi_notify_change)(struct mnt_idmap *, struct dentry *,
				   struct iattr *, struct inode **);
#else
extern int (*kasumi_notify_change)(struct user_namespace *, struct dentry *,
				   struct iattr *, struct inode **);
#endif
int kasumi_vfs_getattr_unprojected(const struct path *path, struct kstat *stat,
				   u32 request_mask, unsigned int query_flags);
bool kasumi_vfs_internal_current(void);
/*
 * Resolved get_vfs_caps_from_disk (security/commoncap.c).  Its leading
 * idmap/user_namespace argument varies by version exactly like notify_change;
 * absent (NULL) simply disables source-file-capability forwarding.  Use the
 * kasumi_source_vfs_caps() wrapper, which supplies the source mount's idmap.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
extern int (*kasumi_get_vfs_caps_from_disk)(struct mnt_idmap *,
					    const struct dentry *,
					    struct cpu_vfs_cap_data *);
#else
extern int (*kasumi_get_vfs_caps_from_disk)(struct user_namespace *,
					    const struct dentry *,
					    struct cpu_vfs_cap_data *);
#endif
int kasumi_source_vfs_caps(const struct path *src,
			   struct cpu_vfs_cap_data *out);
/* Data-plane delegates for a char/blk/fifo source wrapper's special fops.
 * Stable signatures across all supported KMIs; NULL disables special read/write
 * (the op returns -EINVAL). */
extern ssize_t (*kasumi_vfs_read)(struct file *, char __user *, size_t,
				  loff_t *);
extern ssize_t (*kasumi_vfs_write)(struct file *, const char __user *, size_t,
				   loff_t *);
extern char *(*kasumi_d_absolute_path)(const struct path *, char *, int);
extern struct dentry *(*kasumi_d_hash_and_lookup)(struct dentry *,
						  const struct qstr *);
extern void *kasumi_vfs_getxattr_addr;
extern void *kasumi_vfs_listxattr_addr;
extern void *kasumi_vfs_setxattr_addr;
extern void *kasumi_vfs_removexattr_addr;
extern void *kasumi_mnt_want_write_addr;
extern void *kasumi_mnt_drop_write_addr;
/*
 * Directory-mutation delegates (Final Phase 1b): a directory-source vnode's
 * i_op create family forwards to these against the pinned source dir.  Their
 * leading idmap/user_namespace argument varies by version exactly like
 * notify_change; vfs_link takes the idmap as its SECOND argument.  Any may be
 * NULL (resolved quietly) — the vnode op returns -EOPNOTSUPP then.
 * lookup_one_len manufactures the source-side child dentry (caller holds the
 * source dir i_rwsem); its signature is stable across all supported KMIs.
 */
extern struct dentry *(*kasumi_lookup_one_len)(const char *, struct dentry *,
					       int);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
extern int (*kasumi_vfs_create)(struct mnt_idmap *, struct inode *,
				struct dentry *, umode_t, bool);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
extern struct dentry *(*kasumi_vfs_mkdir)(struct mnt_idmap *, struct inode *,
					  struct dentry *, umode_t);
#else
extern int (*kasumi_vfs_mkdir)(struct mnt_idmap *, struct inode *,
			       struct dentry *, umode_t);
#endif
extern int (*kasumi_vfs_mknod)(struct mnt_idmap *, struct inode *,
			       struct dentry *, umode_t, dev_t);
extern int (*kasumi_vfs_symlink)(struct mnt_idmap *, struct inode *,
				 struct dentry *, const char *);
extern int (*kasumi_vfs_unlink)(struct mnt_idmap *, struct inode *,
				struct dentry *, struct inode **);
extern int (*kasumi_vfs_rmdir)(struct mnt_idmap *, struct inode *,
			       struct dentry *);
extern int (*kasumi_vfs_link)(struct dentry *, struct mnt_idmap *,
			      struct inode *, struct dentry *, struct inode **);
#else
extern int (*kasumi_vfs_create)(struct user_namespace *, struct inode *,
				struct dentry *, umode_t, bool);
extern int (*kasumi_vfs_mkdir)(struct user_namespace *, struct inode *,
			       struct dentry *, umode_t);
extern int (*kasumi_vfs_mknod)(struct user_namespace *, struct inode *,
			       struct dentry *, umode_t, dev_t);
extern int (*kasumi_vfs_symlink)(struct user_namespace *, struct inode *,
				 struct dentry *, const char *);
extern int (*kasumi_vfs_unlink)(struct user_namespace *, struct inode *,
				struct dentry *, struct inode **);
extern int (*kasumi_vfs_rmdir)(struct user_namespace *, struct inode *,
			       struct dentry *);
extern int (*kasumi_vfs_link)(struct dentry *, struct user_namespace *,
			      struct inode *, struct dentry *, struct inode **);
#endif
/*
 * vfs_rename uses struct renamedata from the kernel's fs.h. Its idmap/userns
 * members vary across supported kernels and are set by name at the call site.
 */
extern int (*kasumi_vfs_rename)(struct renamedata *);
/* Public LSM secctx round-trip: copy a source inode's security context onto a
 * synthetic vnode's in-core SID without touching SELinux blob internals.  Both
 * may be NULL when the LSM/symbols are unavailable — callers must check. */
extern typeof(security_inode_getsecctx) *kasumi_security_inode_getsecctx;
extern typeof(security_inode_notifysecctx) *kasumi_security_inode_notifysecctx;
extern typeof(security_release_secctx) *kasumi_security_release_secctx;
extern void (*kasumi_free_inode_nonrcu_ptr)(struct inode *);

#endif /* _KASUMI_RUNTIME_H */
