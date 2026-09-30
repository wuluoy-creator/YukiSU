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
#include "kasumi_runtime.h"
#include "kasumi_dirhijack.h"
#include "kasumi_store.h"
#include "kasumi_entrypoints.h"
#include "kasumi_path_policy.h"
#include "kasumi_overlay.h"
#include "kasumi_vfs_hooks.h"
#include "kasumi_proc_read_hooks.h"
#include "kasumi_fake_mountinfo.h"
#include "kasumi_iop_override.h"
#include "kasumi_fop_override.h"

#define KASUMI_MAGIC_POS 0x1000000000000000ULL

KASUMI_NOCFI bool kasumi_filldir_filter(struct dir_context *ctx,
					const char *name, int namlen,
					loff_t offset, u64 ino,
					unsigned int d_type)
{
	struct kasumi_filldir_wrapper *w =
	    container_of(ctx, struct kasumi_filldir_wrapper, wrap_ctx);
	bool ret;
	struct inode *parent =
	    w->parent_dentry ? d_inode(w->parent_dentry) : NULL;

	/* Inject phase: before first real entry, emit entries from merge
	 * targets and kasumi_paths into the directory listing. */
	if (w->view_allowed && w->dir_has_inject && !w->inject_done &&
	    w->dir_path && w->parent_dentry) {
		struct list_head head;
		struct kasumi_name_list *item, *tmp;
		loff_t inj_pos = KASUMI_MAGIC_POS;

		w->inject_done = true;
		INIT_LIST_HEAD(&head);
		kasumi_populate_injected_list(w->dir_path, w->parent_dentry,
					      &head);

		list_for_each_entry_safe (item, tmp, &head, list) {
			int nlen = strlen(item->name);
			if (parent &&
			    kasumi_dirhijack_hidden(parent, item->name, nlen)) {
				list_del(&item->list);
				kfree(item->name);
				kfree(item);
				continue;
			}
			if (unlikely(!w->orig_ctx || !w->orig_ctx->actor))
				break;
			ret = w->orig_ctx->actor(w->orig_ctx, item->name, nlen,
						 inj_pos, item->ino ?: 1,
						 item->type);
			atomic64_inc(&kasumi_hook_stats.filldir_injected);
			list_del(&item->list);
			kfree(item->name);
			kfree(item);
			if (!ret) {
				list_for_each_entry_safe (item, tmp, &head,
							  list) {
					list_del(&item->list);
					kfree(item->name);
					kfree(item);
				}
				return ret;
			}
			inj_pos++;
		}
	}

	if (parent && kasumi_dirhijack_hidden(parent, name, namlen))
		return true;

	if (unlikely(namlen <= 2 && name[0] == '.')) {
		if (namlen == 1 || (namlen == 2 && name[1] == '.'))
			goto passthrough;
	}

	/* Hide real entries that also exist in merge targets. This prevents
	 * duplicates: the injected version (from populate_injected_list)
	 * replaces the original, just like original kasumi.c does.
	 * Skip when merge target IS the dir we're listing (e.g. target path
	 * resolved to same inode via symlink) - otherwise we'd hide everything.
	 * This is independent of app-hide policy because explicit merge rules
	 * must not emit duplicate names even during root/manual validation. */
	if (w->view_allowed && kasumi_d_hash_and_lookup &&
	    w->merge_target_count > 0 && w->parent_dentry) {
		int i;
		for (i = 0; i < w->merge_target_count; i++) {
			struct dentry *tgt = w->merge_target_dentries[i];
			if (!tgt || tgt == w->parent_dentry)
				continue;
			if (d_inode(tgt) &&
			    d_inode(tgt) == d_inode(w->parent_dentry))
				continue;
			{
				struct dentry *child = kasumi_d_hash_and_lookup(
				    tgt, &(struct qstr)QSTR_INIT(name, namlen));
				if (child) {
					dput(child);
					atomic64_inc(
					    &kasumi_hook_stats.filldir_hidden);
					return true;
				}
			}
		}
	}

	if (kasumi_d_hash_and_lookup && w->dir_has_hidden && w->parent_dentry) {
		struct dentry *child;

		child = kasumi_d_hash_and_lookup(
		    w->parent_dentry, &(struct qstr)QSTR_INIT(name, namlen));
		if (child) {
			struct inode *cinode = d_inode(child);
			if (cinode && cinode->i_mapping &&
			    test_bit(AS_FLAGS_KASUMI_HIDE,
				     &cinode->i_mapping->flags)) {
				dput(child);
				atomic64_inc(&kasumi_hook_stats.filldir_hidden);
				return true;
			}
			dput(child);
		}
	}

passthrough:
	if (unlikely(!w->orig_ctx || !w->orig_ctx->actor))
		return true;
	return w->orig_ctx->actor(w->orig_ctx, name, namlen, offset, ino,
				  d_type);
}

/*
 * vfs_getattr kretprobe ret: spoof kstat for redirect targets.
 * Makes the file appear to belong to /system with root ownership.
 */
/*
 * Apply kstat spoofing in place. Shared by:
 *   - vfs_getattr kretprobe ret handler (legacy slow path)
 *   - kasumi_shadow_getattr in kasumi_iop_override.c (fast path after install)
 *
 * Note: `inode` may be NULL for the legacy path (we still have stat / mapping
 * via the kretprobe ri data); the shadow path always provides it.
 */
void kasumi_apply_kstat_spoof(struct inode *inode, struct kstat *stat)
{
	struct kasumi_spoof_kstat_entry *e = NULL;

	if (!stat)
		return;
	if (kasumi_vfs_internal_current())
		return;
	if (!kasumi_policy_current_is_view_target())
		return;

	/* Explicit per-inode spoof rule (api15) takes precedence. */
	if (inode && atomic_read(&kasumi_spoof_kstat_count) > 0) {
		rcu_read_lock();
		e = kasumi_spoof_kstat_lookup_by_ino(
		    (unsigned long)inode->i_ino, (unsigned long)stat->dev);
		if (e) {
			if (e->spoofed_dev)
				stat->dev = e->spoofed_dev;
			if (e->spoofed_ino)
				stat->ino = e->spoofed_ino;
			if (e->spoofed_nlink)
				stat->nlink = e->spoofed_nlink;
			if (e->spoofed_size)
				stat->size = e->spoofed_size;
			if (e->spoofed_blksize)
				stat->blksize = e->spoofed_blksize;
			if (e->spoofed_blocks)
				stat->blocks = e->spoofed_blocks;
			if (e->spoofed_atime_sec || e->spoofed_atime_nsec) {
				stat->atime.tv_sec = e->spoofed_atime_sec;
				stat->atime.tv_nsec = e->spoofed_atime_nsec;
			}
			if (e->spoofed_mtime_sec || e->spoofed_mtime_nsec) {
				stat->mtime.tv_sec = e->spoofed_mtime_sec;
				stat->mtime.tv_nsec = e->spoofed_mtime_nsec;
			}
			if (e->spoofed_ctime_sec || e->spoofed_ctime_nsec) {
				stat->ctime.tv_sec = e->spoofed_ctime_sec;
				stat->ctime.tv_nsec = e->spoofed_ctime_nsec;
			}
		}
		rcu_read_unlock();
	}

	if (!e) {
		dev_t system_dev = READ_ONCE(kasumi_system_dev);

		/* Generic fallback: add_rule redirect targets reaching the
		 * getattr path.  Publish the SAME synthetic ino every other
		 * surface emits: kasumi_vnode_source_ino() resolves the rule's
		 * allocated inode from the resolved source (dev,ino) the kstat
		 * already carries, so stat here == syscall stat == getdents ==
		 * /proc/maps (and no 19-digit bit-63 tell).  Compute it BEFORE
		 * overwriting stat->dev.
		 *
		 * dev stays the captured /system dev: this callback has no
		 * trustworthy visible path (its @path may be the rewritten
		 * target, whose sb dev would leak), and this holdout only fires
		 * for /system redirect targets where the visible dev already IS
		 * the /system dev. */
		stat->ino = kasumi_vnode_source_ino(stat->dev, (u64)stat->ino);
		if (system_dev)
			stat->dev = system_dev;
		if (S_ISREG(stat->mode))
			stat->nlink = 1;
	}

	stat->uid = GLOBAL_ROOT_UID;
	stat->gid = GLOBAL_ROOT_GID;

	kasumi_log("kstat: spoofed ino %lu (explicit=%d)\n",
		   (unsigned long)stat->ino, e ? 1 : 0);

	if (inode && inode->i_mapping)
		set_bit(AS_FLAGS_KASUMI_SPOOF_KSTAT, &inode->i_mapping->flags);
}

KASUMI_NOCFI struct kasumi_filldir_wrapper *
kasumi_iterate_prepare_wrapper(struct file *file, struct dir_context *orig_ctx)
{
	struct kasumi_filldir_wrapper *w;
	struct inode *dir_inode;
	const char *dname;
	enum kasumi_policy_scope scope;

	if (atomic_long_read(&kasumi_ioctl_tgid) ==
	    (long)task_tgid_vnr(current))
		return NULL;
	if (kasumi_this_cpu()->in_populate_inject)
		return NULL;
	if (!READ_ONCE(kasumi_enabled))
		return NULL;
	if (atomic_long_read(&kasumi_ioctl_tgid) > 0 &&
	    task_tgid_vnr(current) == atomic_long_read(&kasumi_ioctl_tgid))
		return NULL;
	if (!orig_ctx || !orig_ctx->actor)
		return NULL;
	if (orig_ctx->actor == kasumi_filldir_filter)
		return NULL;
	scope = kasumi_policy_current_scope();
	if (scope == KASUMI_POLICY_SCOPE_NONE)
		return NULL;

	w = kmem_cache_zalloc(kasumi_filldir_cache, GFP_ATOMIC);
	if (!w)
		return NULL;

	w->orig_ctx = orig_ctx;
	w->wrap_ctx.actor = kasumi_filldir_filter;
	w->wrap_ctx.pos = orig_ctx->pos;
	w->view_allowed = scope == KASUMI_POLICY_SCOPE_VIEW;
	w->spoof_allowed = scope == KASUMI_POLICY_SCOPE_SPOOF;
	w->parent_dentry =
	    file && file->f_path.dentry ? file->f_path.dentry : NULL;
	w->inject_done = orig_ctx->pos != 0;

	if (w->parent_dentry) {
		dir_inode = d_inode(w->parent_dentry);
		if (dir_inode && dir_inode->i_mapping) {
			w->dir_has_hidden =
			    kasumi_policy_current_is_hide_target(dir_inode) &&
			    test_bit(AS_FLAGS_KASUMI_DIR_HAS_HIDDEN,
				     &dir_inode->i_mapping->flags);
			/* Fast path: if dir has no inject flag, skip
			 * rcu_read_lock + hash traversal */
			w->dir_has_inject =
			    w->view_allowed &&
			    test_bit(AS_FLAGS_KASUMI_DIR_HAS_INJECT,
				     &dir_inode->i_mapping->flags);
		}
		dname = w->parent_dentry->d_name.name;
		if (dname[0] == 'd' && dname[1] == 'e' && dname[2] == 'v' &&
		    dname[3] == '\0')
			w->dir_path_len = 4;

		/*
		 * Only when dir_has_inject (from flag) is true: build full path
		 * and traverse hash to get merge_target_dentries. Most dirs
		 * skip this.
		 */
		if (w->view_allowed && atomic_read(&kasumi_rule_count) > 0 &&
		    w->dir_has_inject) {
			char *buf =
			    kasumi_iterate_buf_base +
			    (smp_processor_id() * KASUMI_ITERATE_PATH_BUF);
			char *dp = ERR_PTR(-ENOENT);

			if (kasumi_d_absolute_path)
				dp = kasumi_d_absolute_path(
				    &file->f_path, buf,
				    KASUMI_ITERATE_PATH_BUF);
			if (IS_ERR(dp))
				dp = dentry_path_raw(w->parent_dentry, buf,
						     KASUMI_ITERATE_PATH_BUF);

			if (!IS_ERR_OR_NULL(dp) && *dp == '/') {
				struct kasumi_inject_entry *ie;
				struct kasumi_merge_entry *me;
				u32 h;
				int mbkt;
				size_t plen = strlen(dp);

				if (plen < KASUMI_ITERATE_PATH_BUF) {
					memcpy(w->dir_path_buf, dp, plen + 1);
					w->dir_path = w->dir_path_buf;
				}
				h = full_name_hash(NULL, dp, strlen(dp));

				rcu_read_lock();
				hlist_for_each_entry_rcu(
				    ie,
				    &kasumi_inject_dirs[hash_min(
					h, KASUMI_HASH_BITS)],
				    node)
				{
					if (strcmp(ie->dir, dp) == 0) {
						w->dir_has_inject = true;
						break;
					}
				}
				/* Scan all merge entries (few) to match both
				 * src and resolved_src; cache target dentries.
				 */
				hash_for_each_rcu(kasumi_merge_dirs, mbkt, me,
						  node)
				{
					if (strcmp(me->src, dp) == 0 ||
					    (me->resolved_src &&
					     strcmp(me->resolved_src, dp) ==
						 0)) {
						w->dir_has_inject = true;
						if (me->target_dentry &&
						    w->merge_target_count <
							KASUMI_MAX_MERGE_TARGETS)
							w->merge_target_dentries
							    [w->merge_target_count++] =
							    me->target_dentry;
					}
				}
				rcu_read_unlock();
			}
		}
	}

	if (!w->dir_has_hidden && !w->dir_has_inject &&
	    (!w->spoof_allowed || !kasumi_stealth_enabled ||
	     w->dir_path_len != 4)) {
		kmem_cache_free(kasumi_filldir_cache, w);
		return NULL;
	}

	return w;
}

void kasumi_iterate_finish_wrapper(struct kasumi_filldir_wrapper *wrapper)
{
	if (!wrapper)
		return;
	if (wrapper->orig_ctx)
		wrapper->orig_ctx->pos = wrapper->wrap_ctx.pos;
	kmem_cache_free(kasumi_filldir_cache, wrapper);
}
