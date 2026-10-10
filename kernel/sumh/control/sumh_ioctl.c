#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0) &&                           \
    !defined(arch_ftrace_get_regs)
#define arch_ftrace_get_regs(fregs) (NULL)
#endif
#include <linux/kprobes.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/jhash.h>
#include <linux/kdev_t.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/sched/task.h>
#include <linux/fs_struct.h>
#include <linux/dirent.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/anon_inodes.h>
#include <linux/fcntl.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/mount.h>
#include <linux/xattr.h>
#include <linux/seq_file.h>
#include <uapi/linux/magic.h>
#include <asm/unistd.h>
#include "sumh_runtime.h"
#include "sumh_dirhijack.h"
#include "sumh_hide_rules.h"
#include "sumh_vnode.h"
#include "sumh_bootstrap.h"
#include "feature/sucompat_vfs.h"
#include "sumh_store.h"
#include "sumh_entrypoints.h"
#include "sumh_path_policy.h"
#include "sumh_overlay.h"
#include "sumh_proc_read_hooks.h"
#include "sumh_vfs_hooks.h"
#include "sumh_vfs_view.h"
#include "sumh_fop_bridge.h"
#include "sumh_iop_override.h"
#include "sumh_fop_override.h"
#include "sumh_sop_shadow.h"
#include "sumh_fake_mountinfo.h"
#include "sumh_kernel_build.h"

static int SUMH_NOCFI sumh_resolve_rule_path(char **pathname,
					     struct inode **target_out,
					     struct inode **parent_out)
{
	struct path path;
	struct inode *target = NULL, *parent = NULL;
	char *buffer, *parent_name = NULL, *resolved = NULL, *text;
	const char *leaf = NULL;
	int ret;

	if (!pathname || !*pathname || (*pathname)[0] != '/')
		return -EINVAL;
	buffer = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	ret = kern_path(*pathname, LOOKUP_FOLLOW, &path);
	if (ret == -ENOENT) {
		char *slash;

		parent_name = kstrdup(*pathname, GFP_KERNEL);
		if (!parent_name) {
			ret = -ENOMEM;
			goto out;
		}
		slash = strrchr(parent_name, '/');
		leaf = *pathname + (slash - parent_name) + 1;
		if (!*leaf) {
			ret = -EINVAL;
			goto out;
		}
		if (slash == parent_name)
			slash[1] = '\0';
		else
			*slash = '\0';
		ret = kern_path(parent_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
				&path);
		if (ret == -ENOENT) {
			/* Keep the stored key deletable after its parent
			 * disappears. */
			ret = 0;
			goto out;
		}
	}
	if (ret)
		goto out;
	text = d_path(&path, buffer, PATH_MAX);
	if (IS_ERR(text)) {
		ret = PTR_ERR(text);
		goto out_path;
	}
	if (text[0] != '/') {
		ret = -EINVAL;
		goto out_path;
	}
	if (leaf) {
		size_t prefix = strcmp(text, "/") ? strlen(text) : 0;
		size_t length = strlen(leaf);

		if (prefix + length + 2 > PATH_MAX) {
			ret = -ENAMETOOLONG;
			goto out_path;
		}
		resolved = kmalloc(prefix + length + 2, GFP_KERNEL);
		if (resolved) {
			memcpy(resolved, text, prefix);
			resolved[prefix] = '/';
			memcpy(resolved + prefix + 1, leaf, length + 1);
		}
		parent = d_inode(path.dentry);
	} else {
		resolved = kstrdup(text, GFP_KERNEL);
		target = d_inode(path.dentry);
		parent = d_inode(path.dentry->d_parent);
	}
	if (!resolved) {
		ret = -ENOMEM;
		goto out_path;
	}
	if (target_out && target) {
		ihold(target);
		*target_out = target;
	}
	if (parent_out && parent) {
		ihold(parent);
		*parent_out = parent;
	}
	kfree(*pathname);
	*pathname = resolved;
out_path:
	path_put(&path);
out:
	kfree(parent_name);
	kfree(buffer);
	return ret;
}

static SUMH_NOCFI int sumh_dispatch_cmd(unsigned int cmd, void __user *arg)
{
	struct sumh_syscall_arg req;
	struct sumh_entry *entry;
	struct sumh_hide_entry *hide_entry;
	struct sumh_inject_entry *inject_entry;
	char *src = NULL, *target = NULL;
	u32 hash;
	bool found = false;
	int ret = 0;

	if (cmd == SUMH_IOC_CLEAR_ALL) {
		sumh_hide_rules_clear();
		sumh_kernel_build_clear();
		mutex_lock(&sumh_config_mutex);
		sumh_cleanup_locked();
		mutex_unlock(&sumh_config_mutex);
		sumh_dirhijack_clear();
		sumh_fop_override_clear();
		sumh_sop_shadow_reap();
		sumh_fake_mi_invalidate_all();
		sumh_store_drain();
		return 0;
	}

	if (cmd == SUMH_IOC_GET_VERSION) {
		int ver = SUMH_PROTOCOL_VERSION;
		if (copy_to_user(arg, &ver, sizeof(ver)))
			return -EFAULT;
		return 0;
	}

	if (cmd == SUMH_IOC_GET_ENABLED) {
		int enabled = smp_load_acquire(&sumh_enabled);

		return copy_to_user(arg, &enabled, sizeof(enabled)) ? -EFAULT
								    : 0;
	}

	if (cmd == SUMH_IOC_SET_KERNEL_BUILD) {
		struct sumh_kernel_build_arg build;

		if (copy_from_user(&build, arg, sizeof(build)))
			return -EFAULT;
		return sumh_kernel_build_set(&build);
	}

	if (cmd == SUMH_IOC_GET_KERNEL_BUILD ||
	    cmd == SUMH_IOC_GET_ORIGINAL_KERNEL_BUILD) {
		struct sumh_kernel_build_arg build;

		ret = cmd == SUMH_IOC_GET_ORIGINAL_KERNEL_BUILD
			  ? sumh_kernel_build_get_original(&build)
			  : sumh_kernel_build_get(&build);
		if (ret)
			return ret;
		return copy_to_user(arg, &build, sizeof(build)) ? -EFAULT : 0;
	}

	if (cmd == SUMH_IOC_SET_DEBUG) {
		int val;
		if (copy_from_user(&val, arg, sizeof(val)))
			return -EFAULT;
		sumh_debug_enabled = !!val;
		sumh_log("debug mode %s\n",
			 sumh_debug_enabled ? "enabled" : "disabled");
		return 0;
	}

	if (cmd == SUMH_IOC_SET_STEALTH) {
		int val;
		if (copy_from_user(&val, arg, sizeof(val)))
			return -EFAULT;
		sumh_stealth_enabled = !!val;
		sumh_log("stealth mode %s\n",
			 sumh_stealth_enabled ? "enabled" : "disabled");
		return 0;
	}

	if (cmd == SUMH_IOC_SET_ENABLED) {
		int val;
		if (copy_from_user(&val, arg, sizeof(val)))
			return -EFAULT;
		if (!val)
			return -EPERM;
		if (val != 1)
			return -EINVAL;
		/* Retain the ioctl for old clients, but SUMH is always
		 * enabled once its bootstrap completed. */
		return sumh_is_ready() ? 0 : -EOPNOTSUPP;
	}

	if (cmd == SUMH_IOC_REORDER_MNT_ID) {
		/* struct mnt_namespace/mount not exposed to LKM; only KPM
		 * (built-in) supports this */
		return -EOPNOTSUPP;
	}

	if (cmd == SUMH_IOC_LIST_RULES) {
		struct sumh_syscall_list_arg list_arg;
		struct sumh_xattr_sb_entry *sb_entry;
		struct sumh_merge_entry *merge_entry;
		char *kbuf;
		size_t buf_size, written = 0;
		int bkt;

		if (copy_from_user(&list_arg, arg, sizeof(list_arg)))
			return -EFAULT;

		buf_size = list_arg.size;
		if (buf_size > 64 * 1024)
			buf_size = 64 * 1024;

		/* Rule dumps need no physically contiguous backing. Allow
		 * vmalloc fallback when a 64 KiB allocation would fail from
		 * fragmentation.
		 */
		kbuf = kvzalloc(buf_size, GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;

		rcu_read_lock();
		written +=
		    scnprintf(kbuf + written, buf_size - written,
			      "SUMH Protocol: %d\n", SUMH_PROTOCOL_VERSION);
		written +=
		    scnprintf(kbuf + written, buf_size - written,
			      "SUMH Enabled: %d\n", sumh_enabled ? 1 : 0);
		hash_for_each_rcu(sumh_paths, bkt, entry, node)
		{
			if (written >= buf_size)
				break;
			written += scnprintf(kbuf + written, buf_size - written,
					     "add %s %s %d\n", entry->src,
					     entry->target, entry->type);
		}
		hash_for_each_rcu(sumh_hide_paths, bkt, hide_entry, node)
		{
			if (written >= buf_size)
				break;
			written += scnprintf(kbuf + written, buf_size - written,
					     "hide %s\n", hide_entry->path);
		}
		hash_for_each_rcu(sumh_inject_dirs, bkt, inject_entry, node)
		{
			if (written >= buf_size)
				break;
			written += scnprintf(kbuf + written, buf_size - written,
					     "inject %s\n", inject_entry->dir);
		}
		hash_for_each_rcu(sumh_merge_dirs, bkt, merge_entry, node)
		{
			if (written >= buf_size)
				break;
			written += scnprintf(kbuf + written, buf_size - written,
					     "merge %s %s\n", merge_entry->src,
					     merge_entry->target);
		}
		hash_for_each_rcu(sumh_xattr_sbs, bkt, sb_entry, node)
		{
			if (written >= buf_size)
				break;
			written +=
			    scnprintf(kbuf + written, buf_size - written,
				      "hide_xattr_sb %p\n", sb_entry->sb);
		}
		/* Feature rules: mount_hide, maps_spoof, statfs_spoof, stealth
		 */
		if (sumh_feature_enabled_mask & SUMH_FEATURE_MOUNT_HIDE) {
			if (written < buf_size)
				written += scnprintf(
				    kbuf + written, buf_size - written,
				    "mount_hide enabled mode=%s\n",
				    READ_ONCE(sumh_mount_hide_mode) ==
					    SUMH_MOUNT_HIDE_MODE_AGGRESSIVE
					? "aggressive"
					: "normal");
		}
		if (sumh_feature_enabled_mask & SUMH_FEATURE_MAPS_SPOOF) {
			if (written < buf_size)
				written += scnprintf(kbuf + written,
						     buf_size - written,
						     "maps_spoof enabled\n");
		}
		if (sumh_feature_enabled_mask & SUMH_FEATURE_STATFS_SPOOF) {
			if (written < buf_size)
				written += scnprintf(kbuf + written,
						     buf_size - written,
						     "statfs_spoof enabled\n");
		}
		if (sumh_stealth_enabled) {
			if (written < buf_size)
				written += scnprintf(kbuf + written,
						     buf_size - written,
						     "stealth enabled\n");
		}
		if (sumh_kernel_build_enabled() && written < buf_size)
			written += scnprintf(kbuf + written, buf_size - written,
					     "kernel_build_spoof enabled\n");
		rcu_read_unlock();

		if (copy_to_user(list_arg.buf, kbuf, written)) {
			kvfree(kbuf);
			return -EFAULT;
		}
		list_arg.size = written;
		if (copy_to_user(arg, &list_arg, sizeof(list_arg))) {
			kvfree(kbuf);
			return -EFAULT;
		}
		kvfree(kbuf);
		return 0;
	}

	if (cmd == SUMH_IOC_SET_MIRROR_PATH) {
		/* ABI slot 14 is intentionally retained, but pure virtual mode
		 * no longer owns or consumes a mirror/workdir path. */
		return -EOPNOTSUPP;
	}

	if (cmd == SUMH_IOC_ADD_SPOOF_KSTAT ||
	    cmd == SUMH_IOC_UPDATE_SPOOF_KSTAT) {
		struct sumh_spoof_kstat __user *u =
		    (struct sumh_spoof_kstat __user *)arg;
		struct sumh_spoof_kstat *k;
		struct sumh_spoof_kstat_entry *e, *existing = NULL;
		size_t plen;
		u32 phash = 0;
		bool have_path;
		struct path resolved;
		unsigned long auto_ino = 0;

		k = kmalloc(sizeof(*k), GFP_KERNEL);
		if (!k)
			return -ENOMEM;
		if (copy_from_user(k, u, sizeof(*k))) {
			kfree(k);
			return -EFAULT;
		}
		k->target_pathname[SUMH_MAX_LEN_PATHNAME - 1] = '\0';
		have_path = (k->target_pathname[0] != '\0');

		/* Auto-resolve target_ino from path if userspace did not supply
		 * one. */
		if (have_path && k->target_ino == 0) {
			if (kern_path(k->target_pathname, LOOKUP_FOLLOW,
				      &resolved) == 0) {
				if (resolved.dentry &&
				    d_inode(resolved.dentry)) {
					struct inode *inode =
					    d_inode(resolved.dentry);

					auto_ino = (unsigned long)inode->i_ino;
					(void)sumh_iop_mark_spoof(inode);
				}
				path_put(&resolved);
			}
			if (auto_ino)
				k->target_ino = auto_ino;
		}
		if (have_path && k->target_ino != 0) {
			if (kern_path(k->target_pathname, LOOKUP_FOLLOW,
				      &resolved) == 0) {
				if (resolved.dentry && d_inode(resolved.dentry))
					(void)sumh_iop_mark_spoof(
					    d_inode(resolved.dentry));
				path_put(&resolved);
			}
		}

		if (!have_path && !k->target_ino) {
			k->err = -EINVAL;
			(void)copy_to_user(u, k, sizeof(*k));
			kfree(k);
			return -EINVAL;
		}

		if (have_path) {
			plen = strlen(k->target_pathname);
			phash = full_name_hash(NULL, k->target_pathname, plen);
		}

		mutex_lock(&sumh_config_mutex);

		/* Look for existing entry by path, then by ino. */
		if (have_path) {
			hlist_for_each_entry(e,
					     &sumh_spoof_kstat_path[hash_min(
						 phash, SUMH_HASH_BITS)],
					     path_node)
			{
				if (e->path_hash == phash &&
				    e->target_pathname &&
				    strcmp(e->target_pathname,
					   k->target_pathname) == 0) {
					existing = e;
					break;
				}
			}
		}
		if (!existing && k->target_ino) {
			hlist_for_each_entry(
			    e,
			    &sumh_spoof_kstat_ino[hash_min(k->target_ino,
							   SUMH_HASH_BITS)],
			    ino_node)
			{
				if (e->target_ino == k->target_ino &&
				    e->target_dev == 0) {
					existing = e;
					break;
				}
			}
		}

		if (existing && cmd == SUMH_IOC_ADD_SPOOF_KSTAT) {
			/* Idempotent ADD: treat as UPDATE. */
		}

		if (!existing) {
			e = kzalloc(sizeof(*e), GFP_KERNEL);
			if (!e) {
				mutex_unlock(&sumh_config_mutex);
				k->err = -ENOMEM;
				(void)copy_to_user(u, k, sizeof(*k));
				kfree(k);
				return -ENOMEM;
			}
			if (have_path) {
				e->target_pathname =
				    kstrdup(k->target_pathname, GFP_KERNEL);
				if (!e->target_pathname) {
					kfree(e);
					mutex_unlock(&sumh_config_mutex);
					k->err = -ENOMEM;
					(void)copy_to_user(u, k, sizeof(*k));
					kfree(k);
					return -ENOMEM;
				}
				e->path_hash = phash;
			}
			e->target_ino = k->target_ino;
			e->target_dev = 0;
			e->spoofed_ino = k->spoofed_ino;
			e->spoofed_dev = k->spoofed_dev;
			e->spoofed_nlink = k->spoofed_nlink;
			e->spoofed_size = k->spoofed_size;
			e->spoofed_atime_sec = k->spoofed_atime_sec;
			e->spoofed_atime_nsec = k->spoofed_atime_nsec;
			e->spoofed_mtime_sec = k->spoofed_mtime_sec;
			e->spoofed_mtime_nsec = k->spoofed_mtime_nsec;
			e->spoofed_ctime_sec = k->spoofed_ctime_sec;
			e->spoofed_ctime_nsec = k->spoofed_ctime_nsec;
			e->spoofed_blksize = k->spoofed_blksize;
			e->spoofed_blocks = k->spoofed_blocks;
			e->is_static = k->is_static;

			if (have_path)
				hlist_add_head_rcu(
				    &e->path_node,
				    &sumh_spoof_kstat_path[hash_min(
					phash, SUMH_HASH_BITS)]);
			if (e->target_ino)
				hlist_add_head_rcu(
				    &e->ino_node,
				    &sumh_spoof_kstat_ino[hash_min(
					e->target_ino, SUMH_HASH_BITS)]);
			atomic_inc(&sumh_spoof_kstat_count);
			sumh_log("spoof_kstat: add path=%s ino=%lu->%lu\n",
				 have_path ? k->target_pathname : "(none)",
				 e->target_ino, e->spoofed_ino);
		} else {
			/* Update fields in place; readers may see torn values
			 * briefly, acceptable for stat() spoof. */
			existing->spoofed_ino = k->spoofed_ino;
			existing->spoofed_dev = k->spoofed_dev;
			existing->spoofed_nlink = k->spoofed_nlink;
			existing->spoofed_size = k->spoofed_size;
			existing->spoofed_atime_sec = k->spoofed_atime_sec;
			existing->spoofed_atime_nsec = k->spoofed_atime_nsec;
			existing->spoofed_mtime_sec = k->spoofed_mtime_sec;
			existing->spoofed_mtime_nsec = k->spoofed_mtime_nsec;
			existing->spoofed_ctime_sec = k->spoofed_ctime_sec;
			existing->spoofed_ctime_nsec = k->spoofed_ctime_nsec;
			existing->spoofed_blksize = k->spoofed_blksize;
			existing->spoofed_blocks = k->spoofed_blocks;
			existing->is_static = k->is_static;

			/* If newly-resolved ino became available, link into ino
			 * table. */
			if (existing->target_ino == 0 && k->target_ino) {
				existing->target_ino = k->target_ino;
				hlist_add_head_rcu(
				    &existing->ino_node,
				    &sumh_spoof_kstat_ino[hash_min(
					k->target_ino, SUMH_HASH_BITS)]);
			}
			sumh_log("spoof_kstat: update path=%s ino=%lu->%lu\n",
				 have_path ? k->target_pathname : "(none)",
				 existing->target_ino, existing->spoofed_ino);
		}

		mutex_unlock(&sumh_config_mutex);

		k->err = 0;
		if (copy_to_user(u, k, sizeof(*k))) {
			kfree(k);
			return -EFAULT;
		}
		kfree(k);
		return 0;
	}

	if (cmd == SUMH_IOC_ADD_MAPS_RULE) {
		struct sumh_maps_rule __user *u =
		    (struct sumh_maps_rule __user *)arg;
		struct sumh_maps_rule k;
		struct sumh_maps_rule_entry *e;

		if (copy_from_user(&k, u, sizeof(k)))
			return -EFAULT;
		e = kmalloc(sizeof(*e), GFP_KERNEL);
		if (!e) {
			k.err = -ENOMEM;
			if (copy_to_user(u, &k, sizeof(k)))
				return -EFAULT;
			return -ENOMEM;
		}
		e->target_ino = k.target_ino;
		e->target_dev = k.target_dev;
		e->spoofed_ino = k.spoofed_ino;
		e->spoofed_dev = k.spoofed_dev;
		strscpy(e->spoofed_pathname, k.spoofed_pathname,
			sizeof(e->spoofed_pathname));
		k.err = 0;
		if (copy_to_user(u, &k, sizeof(k))) {
			kfree(e);
			return -EFAULT;
		}
		mutex_lock(&sumh_maps_mutex);
		list_add_tail(&e->list, &sumh_maps_rules);
		mutex_unlock(&sumh_maps_mutex);
		return 0;
	}

	if (cmd == SUMH_IOC_CLEAR_MAPS_RULES) {
		struct sumh_maps_rule_entry *e, *tmp;

		mutex_lock(&sumh_maps_mutex);
		list_for_each_entry_safe (e, tmp, &sumh_maps_rules, list) {
			list_del(&e->list);
			kfree(e);
		}
		mutex_unlock(&sumh_maps_mutex);
		return 0;
	}

	if (cmd == SUMH_IOC_SET_MOUNT_HIDE) {
		struct sumh_mount_hide_arg a;
		if (copy_from_user(&a, arg, sizeof(a)))
			return -EFAULT;
		if (a.enable)
			sumh_feature_enabled_mask |= SUMH_FEATURE_MOUNT_HIDE;
		else
			sumh_feature_enabled_mask &= ~SUMH_FEATURE_MOUNT_HIDE;
		sumh_fake_mi_invalidate_all();
		sumh_log("mount hide %s\n", a.enable ? "enabled" : "disabled");
		/* path_pattern reserved for future custom hide rules */
		return 0;
	}

	if (cmd == SUMH_IOC_SET_MOUNT_HIDE_MODE) {
		int mode;

		if (copy_from_user(&mode, arg, sizeof(mode)))
			return -EFAULT;
		if (mode != SUMH_MOUNT_HIDE_MODE_NORMAL &&
		    mode != SUMH_MOUNT_HIDE_MODE_AGGRESSIVE)
			return -EINVAL;
		/* A namespace is one kernel object across readlink/stat/fstat.
		 * The retired text-only projection could not preserve that
		 * identity.
		 */
		if (mode == SUMH_MOUNT_HIDE_MODE_AGGRESSIVE)
			return -EOPNOTSUPP;
		WRITE_ONCE(sumh_mount_hide_mode, mode);
		sumh_fake_mi_invalidate_all();
		sumh_log("mount hide mode: %s\n",
			 mode == SUMH_MOUNT_HIDE_MODE_AGGRESSIVE ? "aggressive"
								 : "normal");
		return 0;
	}

	if (cmd == SUMH_IOC_SET_MAPS_SPOOF) {
		struct sumh_maps_spoof_arg a;
		if (copy_from_user(&a, arg, sizeof(a)))
			return -EFAULT;
		if (a.enable)
			sumh_feature_enabled_mask |= SUMH_FEATURE_MAPS_SPOOF;
		else
			sumh_feature_enabled_mask &= ~SUMH_FEATURE_MAPS_SPOOF;
		/* reserved for future inline rule */
		return 0;
	}

	if (cmd == SUMH_IOC_SET_STATFS_SPOOF) {
		struct sumh_statfs_spoof_arg a;
		if (copy_from_user(&a, arg, sizeof(a)))
			return -EFAULT;
		if (a.enable && !sumh_statfs_view_available())
			return -EOPNOTSUPP;
		if (a.enable)
			sumh_feature_enabled_mask |= SUMH_FEATURE_STATFS_SPOOF;
		else
			sumh_feature_enabled_mask &= ~SUMH_FEATURE_STATFS_SPOOF;
		/* path/spoof_f_type reserved for future custom mappings */
		return 0;
	}

	if (cmd == SUMH_IOC_GET_FEATURES) {
		int features = 0;
		if (sumh_hide_rules_available())
			features |= SUMH_FEATURE_MANAGED_HIDE;
		if (sumh_kernel_build_available())
			features |= SUMH_FEATURE_KERNEL_BUILD_SPOOF;
		features |= SUMH_FEATURE_KSTAT_SPOOF;
		features |= SUMH_FEATURE_MERGE_DIR;
		if (sumh_overlay_xattr_available())
			features |= SUMH_FEATURE_OVERLAY_XATTR_HIDE;
		if (sumh_proc_proxy_registered ||
		    sumh_mount_hide_vfsmnt_registered ||
		    sumh_mount_hide_mountinfo_registered)
			features |= SUMH_FEATURE_MOUNT_HIDE;
		if (sumh_proc_proxy_registered && sumh_fake_mi_active())
			features |= SUMH_FEATURE_FAKE_MOUNTINFO;
		if (sumh_proc_proxy_registered)
			features |= SUMH_FEATURE_MAPS_SPOOF;
		if (sumh_statfs_view_available())
			features |= SUMH_FEATURE_STATFS_SPOOF;
		if (copy_to_user(arg, &features, sizeof(features)))
			return -EFAULT;
		return 0;
	}

	if (cmd == SUMH_IOC_GET_HOOKS) {
		struct sumh_syscall_list_arg list_arg;
		char *kbuf;
		size_t buf_size, written = 0;
		int n;

		if (copy_from_user(&list_arg, arg, sizeof(list_arg)))
			return -EFAULT;

		buf_size = list_arg.size;
		if (buf_size > 4096)
			buf_size = 4096;

		kbuf = kzalloc(buf_size, GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;

		written += scnprintf(kbuf + written, buf_size - written,
				     "control: KernelSU fd\n");

		/* Path redirect: served entirely through the VFS lookup/vnode
		 * layer; no syscall dispatcher or sys_enter tracepoint remains.
		 */
		n = scnprintf(kbuf + written, buf_size - written,
			      "path: none\n");
		written += n;
		{
			dev_t vnode_dev = sumh_vnode_device();

			n = scnprintf(
			    kbuf + written, buf_size - written,
			    "vnode: live=%u allocated=%llu dev=%u:%u\n",
			    sumh_vnode_live(),
			    (unsigned long long)sumh_vnode_allocated(),
			    MAJOR(vnode_dev), MINOR(vnode_dev));
			written += n;
		}
		n = scnprintf(
		    kbuf + written, buf_size - written, "overlay xattrs: %s\n",
		    sumh_overlay_xattr_available() ? "VFS get/list filter"
						   : "none");
		written += n;

		n = scnprintf(kbuf + written, buf_size - written,
			      "vfs: getattr=iop readdir=fop xattrs=%s\n",
			      sumh_overlay_xattr_available() ? "VFS-filter"
							     : "none");
		written += n;

		n = scnprintf(
		    kbuf + written, buf_size - written,
		    "mountinfo/mounts: %s\n",
		    sumh_proc_proxy_registered
			? "fd-install fop proxy (app and isolated readers)"
			: "none");
		written += n;
		n = scnprintf(
		    kbuf + written, buf_size - written, "fake mountinfo: %s\n",
		    sumh_proc_proxy_registered && sumh_fake_mi_active()
			? "per-open namespace snapshot, native mount IDs"
			: "none");
		written += n;
		n = scnprintf(kbuf + written, buf_size - written,
			      "mount namespace links: native\n");
		written += n;

		/* maps spoof */
		if (sumh_proc_proxy_registered)
			n = scnprintf(kbuf + written, buf_size - written,
				      "maps: fd-install fop proxy\n");
		else
			n = scnprintf(kbuf + written, buf_size - written,
				      "maps: none\n");
		written += n;
		if (sumh_statfs_view_available())
			n = scnprintf(kbuf + written, buf_size - written,
				      "statfs/fstatfs: path-aware VFS wrapper "
				      "(entries=%llu spoofs=%llu)\n",
				      (unsigned long long)atomic64_read(
					  &sumh_hook_stats.statfs_entries),
				      (unsigned long long)atomic64_read(
					  &sumh_hook_stats.statfs_spoofs));
		else
			n = scnprintf(kbuf + written, buf_size - written,
				      "statfs: none\n");
		written += n;

		list_arg.size = written;
		if (copy_to_user(arg, &list_arg, sizeof(list_arg))) {
			kfree(kbuf);
			return -EFAULT;
		}
		if (written && copy_to_user(list_arg.buf, kbuf, written)) {
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);
		return 0;
	}

	/* Commands that use sumh_syscall_arg */
	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	if (req.src) {
		src = strndup_user(req.src, PAGE_SIZE);
		if (IS_ERR(src))
			return PTR_ERR(src);
	}
	if (req.target) {
		target = strndup_user(req.target, PAGE_SIZE);
		if (IS_ERR(target)) {
			kfree(src);
			return PTR_ERR(target);
		}
	}
	if (src && ksu_sucompat_vfs_reserved_path(src)) {
		kfree(src);
		kfree(target);
		return -EPERM;
	}

	switch (cmd) {
	case SUMH_IOC_ADD_MERGE_RULE: {
		struct sumh_merge_entry *me;
		char *mat_src = NULL, *mat_tgt = NULL;

		if (!src || !target) {
			ret = -EINVAL;
			break;
		}

		ret = sumh_check_merge_target(target);
		if (ret)
			break;

		/* Resolve symlinks: d_absolute_path in iterate_dir returns
		 * canonical paths (e.g. /product/overlay), while userspace
		 * sends symlink paths (e.g. /system/product/overlay). Store the
		 * canonical form as resolved_src for iterate_dir matching. */
		{
			char *resolved_src = NULL;
			struct dentry *tgt_dentry = NULL;
			struct path mpath;

			if (kern_path(src, LOOKUP_FOLLOW, &mpath) == 0) {
				char *rbuf = kmalloc(PATH_MAX, GFP_KERNEL);
				if (rbuf) {
					char *res =
					    d_path(&mpath, rbuf, PATH_MAX);
					if (!IS_ERR(res) && res[0] == '/' &&
					    strcmp(res, src) != 0)
						resolved_src =
						    kstrdup(res, GFP_KERNEL);
					kfree(rbuf);
				}
				path_put(&mpath);
			}
			if (kern_path(target, LOOKUP_FOLLOW, &mpath) == 0) {
				tgt_dentry = dget(mpath.dentry);
				path_put(&mpath);
			}

			hash = full_name_hash(NULL, src, strlen(src));
			mutex_lock(&sumh_config_mutex);

			hlist_for_each_entry(
			    me,
			    &sumh_merge_dirs[hash_min(hash, SUMH_HASH_BITS)],
			    node)
			{
				if (strcmp(me->src, src) == 0 &&
				    strcmp(me->target, target) == 0) {
					found = true;
					break;
				}
			}
			if (!found) {
				me = kmalloc(sizeof(*me), GFP_KERNEL);
				if (me) {
					mat_src = kstrdup(src, GFP_KERNEL);
					mat_tgt = kstrdup(target, GFP_KERNEL);
					me->src = src;
					me->target = target;
					me->resolved_src = resolved_src;
					me->target_dentry = tgt_dentry;
					resolved_src = NULL;
					tgt_dentry = NULL;
					hlist_add_head_rcu(
					    &me->node,
					    &sumh_merge_dirs[hash_min(
						hash, SUMH_HASH_BITS)]);
					src = NULL;
					target = NULL;
				} else {
					ret = -ENOMEM;
				}
			} else {
				ret = -EEXIST;
			}
			mutex_unlock(&sumh_config_mutex);
			if (!found && !ret) {
				sumh_log("add merge rule: src=%s, target=%s\n",
					 me->src, me->target);
				sumh_add_inject_rule(
				    kstrdup(me->src, GFP_KERNEL));
				if (me->resolved_src)
					sumh_add_inject_rule(kstrdup(
					    me->resolved_src, GFP_KERNEL));
				sumh_mark_dir_has_inject(me->src);
				if (me->resolved_src)
					sumh_mark_dir_has_inject(
					    me->resolved_src);
				if (mat_src && mat_tgt)
					ret = sumh_materialize_merge(
					    mat_src, mat_tgt, 0);
			}
			kfree(resolved_src);
			if (tgt_dentry)
				dput(tgt_dentry);
			kfree(mat_src);
			kfree(mat_tgt);
		}
		break;
	}

	case SUMH_IOC_ADD_RULE: {
		char *parent_dir = NULL;
		char *resolved_src = NULL;
		struct sumh_entry *new_entry = NULL;
		struct path path;
		struct inode *target_inode = NULL;
		bool install_side_effects = true;
		char *tmp_buf;
		unsigned long dh_v_ino = 0;
		umode_t dh_src_mode = 0;
		bool dh_want = false;
		unsigned long dh_nofollow_ino = 0;
		bool dh_nofollow_valid = false;

		if (!src || !target) {
			ret = -EINVAL;
			break;
		}

		tmp_buf = kmalloc(PATH_MAX, GFP_KERNEL);
		if (!tmp_buf) {
			ret = -ENOMEM;
			break;
		}

		/* Try to resolve full path */
		if (kern_path(src, LOOKUP_FOLLOW, &path) == 0) {
			char *res = d_path(&path, tmp_buf, PATH_MAX);
			if (!IS_ERR(res)) {
				resolved_src = kstrdup(res, GFP_KERNEL);
				{
					char *ls = strrchr(res, '/');
					if (ls) {
						if (ls == res)
							parent_dir = kstrdup(
							    "/", GFP_KERNEL);
						else {
							size_t l = ls - res;
							parent_dir = kmalloc(
							    l + 1, GFP_KERNEL);
							if (parent_dir) {
								memcpy(
								    parent_dir,
								    res, l);
								parent_dir[l] =
								    '\0';
							}
						}
					}
				}
			}
			path_put(&path);
		} else {
			char *ls = strrchr(src, '/');
			if (ls) {
				size_t l = ls - src;
				char *p_str;

				if (ls == src) {
					p_str = kstrdup("/", GFP_KERNEL);
				} else {
					p_str = kmalloc(l + 1, GFP_KERNEL);
					if (p_str) {
						memcpy(p_str, src, l);
						p_str[l] = '\0';
					}
				}
				if (p_str) {
					if (kern_path(p_str, LOOKUP_FOLLOW,
						      &path) == 0) {
						char *res = d_path(
						    &path, tmp_buf, PATH_MAX);
						if (!IS_ERR(res)) {
							if (!strcmp(res, "/")) {
								resolved_src =
								    kstrdup(
									ls,
									GFP_KERNEL);
							} else {
								size_t rl =
								    strlen(res);
								size_t nl =
								    strlen(ls);

								resolved_src =
								    kmalloc(
									rl +
									    nl +
									    1,
									GFP_KERNEL);
								if (resolved_src) {
									strcpy(
									    resolved_src,
									    res);
									strcat(
									    resolved_src,
									    ls);
								}
							}
							parent_dir = kstrdup(
							    res, GFP_KERNEL);
						}
						path_put(&path);
					}
					kfree(p_str);
				}
			}
		}
		kfree(tmp_buf);

		if (resolved_src) {
			kfree(src);
			src = resolved_src;
		}

		hash = full_name_hash(NULL, src, strlen(src));
		new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
		if (!new_entry) {
			ret = -ENOMEM;
			goto add_rule_done;
		}
		new_entry->src = kstrdup(src, GFP_KERNEL);
		new_entry->target = kstrdup(target, GFP_KERNEL);
		new_entry->type = req.type;
		new_entry->src_hash = hash;
		if (!new_entry->src || !new_entry->target) {
			ret = -ENOMEM;
			goto add_rule_done;
		}
		ret = sumh_entry_capture_source(new_entry, target);
		if (ret)
			goto add_rule_done;
		dh_v_ino = new_entry->visible_ino;
		dh_src_mode = new_entry->source_mode;
		dh_want = true;
		/* Captured before the config_mutex section because new_entry is
		 * set to NULL once it is inserted; these feed the symlink
		 * dirhijack branch. */
		dh_nofollow_valid = new_entry->source_nofollow_path_valid;
		dh_nofollow_ino = new_entry->nofollow_visible_ino;

		install_side_effects = sumh_store_upsert(new_entry);
		new_entry = NULL;

		if (install_side_effects && parent_dir) {
			sumh_mark_dir_has_inject(parent_dir);
			sumh_add_inject_rule(parent_dir);
			parent_dir = NULL;
		}
		if (install_side_effects && target &&
		    kern_path(target, LOOKUP_FOLLOW, &path) == 0) {
			if (path.dentry && d_inode(path.dentry)) {
				target_inode = d_inode(path.dentry);
				ihold(target_inode);
			}
			path_put(&path);
		}
		if (target_inode) {
			(void)sumh_iop_mark_spoof(target_inode);
			iput(target_inode);
		}

		/* Tier 3: when the lookup hijack is enabled, also register this
		 * rule's visible child so VFS lookup resolves it to a SUMH
		 * virtual inode. v1 handled regular-file sources; Slice 3 adds
		 * symlink sources, which are registered as a symlink vnode
		 * pinned to the link itself (nofollow) so lstat/readlink see
		 * the link and the kernel follows it natively. */
		if (dh_want && sumh_dirhijack_enabled()) {
			struct path dsrc;

			if (dh_nofollow_valid &&
			    kern_path(target, 0, &dsrc) == 0) {
				struct inode *di = d_inode(dsrc.dentry);

				if (di && S_ISLNK(di->i_mode))
					(void)sumh_dirhijack_add(
					    src, &dsrc, dh_nofollow_ino,
					    SUMH_VNODE_F_LNK);
				path_put(&dsrc);
			} else if (S_ISREG(dh_src_mode) &&
				   kern_path(target, LOOKUP_FOLLOW, &dsrc) ==
				       0) {
				(void)sumh_dirhijack_add(src, &dsrc, dh_v_ino,
							 0);
				path_put(&dsrc);
			} else if (S_ISDIR(dh_src_mode) &&
				   kern_path(target, LOOKUP_FOLLOW, &dsrc) ==
				       0) {
				/* Slice 4c: a directory-source redirect
				 * resolves to a SUMH directory vnode whose
				 * lookup/iterate delegate to the pinned source
				 * dir. */
				(void)sumh_dirhijack_add(src, &dsrc, dh_v_ino,
							 SUMH_VNODE_F_DIR);
				path_put(&dsrc);
			} else if ((S_ISCHR(dh_src_mode) ||
				    S_ISBLK(dh_src_mode) ||
				    S_ISFIFO(dh_src_mode)) &&
				   sumh_device_sources_enabled &&
				   kern_path(target, LOOKUP_FOLLOW, &dsrc) ==
				       0) {
				/* char/blk/fifo source: resolves to a special
				 * vnode wrapper (S_IFREG inode to clear
				 * may_open_dev on the nodev visible mount;
				 * .open delegates to the real device/fifo,
				 * getattr projects the source type+rdev). */
				(void)sumh_dirhijack_add(src, &dsrc, dh_v_ino,
							 SUMH_VNODE_F_SPECIAL);
				path_put(&dsrc);
			}
		}

		/* Exact rules are injected when the visible dentry is absent.
		 * Existing real names are de-duplicated by the directory view,
		 * so no hide marker is needed for either form. */
	add_rule_done:
		if (new_entry) {
			sumh_entry_release_source(new_entry);
			kfree(new_entry->src);
			kfree(new_entry->target);
			kfree(new_entry->source_canonical);
			kfree(new_entry);
		}
		kfree(parent_dir);
		break;
	}

	case SUMH_IOC_HIDE_RULE: {
		struct sumh_hide_entry *new_hide = NULL;
		struct inode *target_inode = NULL;
		struct inode *parent_inode = NULL;

		ret =
		    sumh_resolve_rule_path(&src, &target_inode, &parent_inode);
		if (ret)
			break;
		new_hide = kzalloc(sizeof(*new_hide), GFP_KERNEL);
		if (!new_hide) {
			ret = -ENOMEM;
			goto hide_done;
		}
		new_hide->path = kstrdup(src, GFP_KERNEL);
		if (!new_hide->path) {
			ret = -ENOMEM;
			goto hide_done;
		}
		new_hide->storage_managed =
		    sumh_hide_storage_parent(parent_inode);
		/* Do not publish a rule that the VFS cannot enforce. */
		ret = sumh_dirhijack_hide(src);
		if (ret)
			goto hide_done;
		if (target_inode)
			sumh_mark_inode_hidden(target_inode);
		if (parent_inode && parent_inode->i_mapping) {
			set_bit(AS_FLAGS_SUMH_DIR_HAS_HIDDEN,
				&parent_inode->i_mapping->flags);
		}

		hash = full_name_hash(NULL, src, strlen(src));
		mutex_lock(&sumh_config_mutex);
		hlist_for_each_entry(
		    hide_entry,
		    &sumh_hide_paths[hash_min(hash, SUMH_HASH_BITS)], node)
		{
			if (hide_entry->path_hash == hash &&
			    strcmp(hide_entry->path, src) == 0) {
				WRITE_ONCE(hide_entry->storage_managed,
					   new_hide->storage_managed);
				found = true;
				break;
			}
		}
		if (!found) {
			unsigned long h1 =
			    jhash(src, strlen(src), 0) & (SUMH_BLOOM_SIZE - 1);
			unsigned long h2 =
			    jhash(src, strlen(src), 1) & (SUMH_BLOOM_SIZE - 1);

			new_hide->path_hash = hash;
			set_bit(h1, sumh_hide_bloom);
			set_bit(h2, sumh_hide_bloom);
			atomic_inc(&sumh_hide_count);
			hlist_add_head_rcu(
			    &new_hide->node,
			    &sumh_hide_paths[hash_min(hash, SUMH_HASH_BITS)]);
			new_hide = NULL;
			sumh_log("hide rule: src=%s\n", src);
		}
		mutex_unlock(&sumh_config_mutex);
	hide_done:
		if (new_hide) {
			kfree(new_hide->path);
			kfree(new_hide);
		}
		if (target_inode)
			iput(target_inode);
		if (parent_inode)
			iput(parent_inode);
		break;
	}

	case SUMH_IOC_HIDE_OVERLAY_XATTRS:
		ret = src ? sumh_overlay_xattr_mark(src) : -EINVAL;
		break;

	case SUMH_IOC_DEL_RULE: {
		struct inode *del_inode = NULL;

		ret = sumh_resolve_rule_path(&src, &del_inode, NULL);
		if (ret)
			break;
		/* Keep indexed state intact if VFS unbinding fails. */
		if (sumh_dirhijack_enabled()) {
			ret = sumh_dirhijack_del(src);
			if (ret && ret != -ENOENT) {
				if (del_inode)
					iput(del_inode);
				break;
			}
			ret = 0;
		}

		hash = full_name_hash(NULL, src, strlen(src));
		mutex_lock(&sumh_config_mutex);

		hlist_for_each_entry(
		    entry, &sumh_paths[hash_min(hash, SUMH_HASH_BITS)], node)
		{
			if (entry->src_hash == hash &&
			    strcmp(entry->src, src) == 0) {
				sumh_clear_inode_flags_for_path(
				    entry->target, AS_FLAGS_SUMH_SPOOF_KSTAT);
				hlist_del_rcu(&entry->node);
				atomic_dec(&sumh_rule_count);
				sumh_log("del rule: src=%s\n", src);
				call_rcu(&entry->rcu, sumh_entry_free_rcu);
				goto del_done;
			}
		}
		hlist_for_each_entry(
		    hide_entry,
		    &sumh_hide_paths[hash_min(hash, SUMH_HASH_BITS)], node)
		{
			if (hide_entry->path_hash == hash &&
			    strcmp(hide_entry->path, src) == 0) {
				hlist_del_rcu(&hide_entry->node);
				atomic_dec(&sumh_hide_count);
				sumh_log("del rule: src=%s\n", src);
				call_rcu(&hide_entry->rcu,
					 sumh_hide_entry_free_rcu);
				goto del_done;
			}
		}
		hlist_for_each_entry(
		    inject_entry,
		    &sumh_inject_dirs[hash_min(hash, SUMH_HASH_BITS)], node)
		{
			if (strcmp(inject_entry->dir, src) == 0) {
				hlist_del_rcu(&inject_entry->node);
				atomic_dec(&sumh_rule_count);
				sumh_log("del rule: src=%s\n", src);
				call_rcu(&inject_entry->rcu,
					 sumh_inject_entry_free_rcu);
				goto del_done;
			}
		}
	del_done:
		mutex_unlock(&sumh_config_mutex);
		if (del_inode) {
			if (del_inode->i_mapping)
				clear_bit(AS_FLAGS_SUMH_HIDE,
					  &del_inode->i_mapping->flags);
			iput(del_inode);
		} else {
			/* A cached hidden negative may have blocked the first
			 * lookup. */
			sumh_clear_inode_flags_for_path(src,
							AS_FLAGS_SUMH_HIDE);
		}
		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	kfree(src);
	kfree(target);
	return ret;
}

/* ======================================================================
 * Part 16: Ioctl Handler
 * ====================================================================== */

/*
 * Rule updates have side effects outside sumh_config_mutex (inode/dentry
 * overrides and injected-directory state). Serialize the control plane so a
 * concurrent CLEAR_ALL cannot return while an older update is still applying
 * those side effects or re-enable the module afterwards.
 */
long sumh_handle_ioctl(unsigned int cmd, void __user *arg)
{
	long ret = sumh_control_begin();

	if (ret)
		return ret;

	mutex_lock(&sumh_mutation_mutex);
	atomic_long_set(&sumh_ioctl_tgid, (long)task_tgid_vnr(current));
	switch (cmd) {
	case SUMH_IOC_USER_HIDE_UPSERT:
	case SUMH_IOC_USER_HIDE_DELETE:
	case SUMH_IOC_USER_HIDE_QUERY:
	case SUMH_IOC_USER_HIDE_RETRY:
	case SUMH_IOC_USER_HIDE_CLEAR:
		ret = sumh_hide_rules_ioctl(cmd, arg);
		break;
	case SUMH_IOC_GET_VERSION:
	case SUMH_IOC_GET_ENABLED:
	case SUMH_IOC_SET_KERNEL_BUILD:
	case SUMH_IOC_GET_KERNEL_BUILD:
	case SUMH_IOC_GET_ORIGINAL_KERNEL_BUILD:
	case SUMH_IOC_SET_ENABLED:
	case SUMH_IOC_ADD_RULE:
	case SUMH_IOC_DEL_RULE:
	case SUMH_IOC_HIDE_RULE:
	case SUMH_IOC_CLEAR_ALL:
	case SUMH_IOC_LIST_RULES:
	case SUMH_IOC_SET_DEBUG:
	case SUMH_IOC_REORDER_MNT_ID:
	case SUMH_IOC_SET_STEALTH:
	case SUMH_IOC_HIDE_OVERLAY_XATTRS:
	case SUMH_IOC_ADD_MERGE_RULE:
	case SUMH_IOC_SET_MIRROR_PATH:
	case SUMH_IOC_GET_HOOKS:
	case SUMH_IOC_ADD_MAPS_RULE:
	case SUMH_IOC_CLEAR_MAPS_RULES:
	case SUMH_IOC_GET_FEATURES:
	case SUMH_IOC_SET_MOUNT_HIDE:
	case SUMH_IOC_SET_MOUNT_HIDE_MODE:
	case SUMH_IOC_SET_MAPS_SPOOF:
	case SUMH_IOC_SET_STATFS_SPOOF:
	case SUMH_IOC_ADD_SPOOF_KSTAT:
	case SUMH_IOC_UPDATE_SPOOF_KSTAT:
		ret = sumh_dispatch_cmd(cmd, arg);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	atomic_long_set(&sumh_ioctl_tgid, 0);
	if (ret >= 0 && ksu_sucompat_vfs_enabled() &&
	    (cmd == SUMH_IOC_SET_ENABLED || cmd == SUMH_IOC_CLEAR_ALL ||
	     cmd == SUMH_IOC_ADD_RULE || cmd == SUMH_IOC_ADD_MERGE_RULE ||
	     cmd == SUMH_IOC_DEL_RULE)) {
		int refresh = ksu_sucompat_vfs_refresh();
		if (refresh)
			pr_warn("sumh: failed to rebind internal su node: %d\n",
				refresh);
	}
	mutex_unlock(&sumh_mutation_mutex);
	sumh_control_end();
	return ret;
}
