#include "sumh_entrypoints.h"
#include "sumh_iop_override.h"
#include "sumh_runtime.h"

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/namei.h>

#define SUMH_IOP_HASH_BITS 10

struct sumh_iop_meta {
	struct inode *inode; /* hash key */
	const struct inode_operations
	    *orig_iop; /* what i_op pointed to before */
	struct inode_operations shadow_iop; /* our copy with .getattr patched */
	struct hlist_node node;
	struct list_head retire_node;
};

static DEFINE_HASHTABLE(sumh_iop_table, SUMH_IOP_HASH_BITS);
static DEFINE_SPINLOCK(sumh_iop_lock);
static atomic_t sumh_iop_active_callbacks = ATOMIC_INIT(0);
static atomic_t sumh_iop_live = ATOMIC_INIT(0);
static DEFINE_MUTEX(sumh_iop_quiesce_lock);
static DECLARE_WAIT_QUEUE_HEAD(sumh_iop_quiesce_wait);
static bool sumh_iop_ready;
static bool sumh_iop_first_tasks_sync;
static bool sumh_iop_callbacks_quiesced;

/* ------------------------------------------------------------------ */
/* hash table helpers                                                  */
/* ------------------------------------------------------------------ */

static struct sumh_iop_meta *sumh_iop_lookup_rcu(struct inode *inode)
{
	struct sumh_iop_meta *m;

	hash_for_each_possible_rcu(sumh_iop_table, m, node,
				   (unsigned long)inode)
	{
		if (m->inode == inode)
			return m;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* shadow getattr - signature varies across kernel versions             */
/* ------------------------------------------------------------------ */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
SUMH_NOCFI static int sumh_shadow_getattr(struct mnt_idmap *idmap,
					  const struct path *path,
					  struct kstat *stat, u32 request_mask,
					  unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct sumh_iop_meta *m;
	const struct inode_operations *orig = NULL;
	int ret;

	atomic_inc(&sumh_iop_active_callbacks);
	atomic64_inc(&sumh_hook_stats.iop_getattr_entries);
	rcu_read_lock();
	m = sumh_iop_lookup_rcu(inode);
	if (m)
		orig = m->orig_iop;
	rcu_read_unlock();

	if (orig && orig->getattr)
		ret =
		    orig->getattr(idmap, path, stat, request_mask, query_flags);
	else {
		generic_fillattr(idmap, request_mask, inode, stat);
		ret = 0;
	}

	if (ret == 0 && inode && inode->i_mapping &&
	    test_bit(AS_FLAGS_SUMH_SPOOF_KSTAT, &inode->i_mapping->flags)) {
		sumh_apply_kstat_spoof(inode, stat);
		atomic64_inc(&sumh_hook_stats.iop_getattr_spoofs);
	}
	if (atomic_dec_and_test(&sumh_iop_active_callbacks))
		wake_up_all(&sumh_iop_quiesce_wait);
	return ret;
}
#else
SUMH_NOCFI static int sumh_shadow_getattr(struct user_namespace *userns,
					  const struct path *path,
					  struct kstat *stat, u32 request_mask,
					  unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct sumh_iop_meta *m;
	const struct inode_operations *orig = NULL;
	int ret;

	atomic_inc(&sumh_iop_active_callbacks);
	atomic64_inc(&sumh_hook_stats.iop_getattr_entries);
	rcu_read_lock();
	m = sumh_iop_lookup_rcu(inode);
	if (m)
		orig = m->orig_iop;
	rcu_read_unlock();

	if (orig && orig->getattr)
		ret = orig->getattr(userns, path, stat, request_mask,
				    query_flags);
	else {
		generic_fillattr(userns, inode, stat);
		ret = 0;
	}

	if (ret == 0 && inode && inode->i_mapping &&
	    test_bit(AS_FLAGS_SUMH_SPOOF_KSTAT, &inode->i_mapping->flags)) {
		sumh_apply_kstat_spoof(inode, stat);
		atomic64_inc(&sumh_hook_stats.iop_getattr_spoofs);
	}
	if (atomic_dec_and_test(&sumh_iop_active_callbacks))
		wake_up_all(&sumh_iop_quiesce_wait);
	return ret;
}
#endif

/* ------------------------------------------------------------------ */
/* install / uninstall                                                  */
/* ------------------------------------------------------------------ */

SUMH_NOCFI int sumh_iop_install(struct inode *inode)
{
	struct sumh_iop_meta *m, *existing;
	const struct inode_operations *orig;

	if (!READ_ONCE(sumh_iop_ready))
		return -ESHUTDOWN;
	if (!inode || !inode->i_op)
		return -EINVAL;
	if (!inode->i_mapping)
		return -EINVAL;
	if (!inode->i_sb)
		return -EINVAL;

	if (test_bit(AS_FLAGS_SUMH_IOP_INSTALLED, &inode->i_mapping->flags))
		return 0;

	orig = inode->i_op;

	m = kzalloc(sizeof(*m), GFP_ATOMIC);
	if (!m)
		return -ENOMEM;

	m->inode = inode;
	m->orig_iop = orig;
	memcpy(&m->shadow_iop, orig, sizeof(struct inode_operations));
	m->shadow_iop.getattr = sumh_shadow_getattr;
	INIT_LIST_HEAD(&m->retire_node);
	ihold(inode);

	spin_lock(&sumh_iop_lock);
	if (!READ_ONCE(sumh_iop_ready)) {
		spin_unlock(&sumh_iop_lock);
		iput(inode);
		kfree(m);
		return -ESHUTDOWN;
	}
	/* Race: another CPU may have installed concurrently. */
	existing = sumh_iop_lookup_rcu(inode);
	if (existing) {
		spin_unlock(&sumh_iop_lock);
		iput(inode);
		kfree(m);
		return 0;
	}
	if (READ_ONCE(inode->i_op) != orig) {
		spin_unlock(&sumh_iop_lock);
		iput(inode);
		kfree(m);
		return -EAGAIN;
	}
	hash_add_rcu(sumh_iop_table, &m->node, (unsigned long)inode);
	atomic_inc(&sumh_iop_live);
	/* Publish: set flag THEN swap pointer so readers seeing new i_op
	 * also see the flag. */
	set_bit(AS_FLAGS_SUMH_IOP_INSTALLED, &inode->i_mapping->flags);
	smp_wmb();
	WRITE_ONCE(inode->i_op, &m->shadow_iop);
	spin_unlock(&sumh_iop_lock);

	sumh_log("iop_override: installed on inode %p (orig=%p)\n", inode,
		 orig);
	return 0;
}

int sumh_iop_mark_spoof(struct inode *inode)
{
	int ret;

	if (!inode || !inode->i_mapping)
		return -EINVAL;
	if (!READ_ONCE(sumh_iop_ready))
		return -ESHUTDOWN;
	set_bit(AS_FLAGS_SUMH_SPOOF_KSTAT, &inode->i_mapping->flags);
	ret = sumh_iop_install(inode);
	if (ret)
		clear_bit(AS_FLAGS_SUMH_SPOOF_KSTAT, &inode->i_mapping->flags);
	return ret;
}

/*
 * Internal uninstall (called from super_operations destroy_inode and from
 * exit). Restores original i_op pointer and frees metadata after RCU grace
 * period. Safe to call when not installed.
 */
/* ------------------------------------------------------------------ */
/* init / exit                                                          */
/* ------------------------------------------------------------------ */

int sumh_iop_override_init(void)
{
	hash_init(sumh_iop_table);
	atomic_set(&sumh_iop_active_callbacks, 0);
	atomic_set(&sumh_iop_live, 0);
	sumh_iop_first_tasks_sync = false;
	sumh_iop_callbacks_quiesced = false;
	WRITE_ONCE(sumh_iop_ready, true);
	pr_info("sumh: iop_override initialized\n");
	return 0;
}

void sumh_iop_override_stop_new(void)
{
	struct sumh_iop_meta *m;
	int bkt;

	mutex_lock(&sumh_iop_quiesce_lock);
	WRITE_ONCE(sumh_iop_ready, false);
	spin_lock(&sumh_iop_lock);
	hash_for_each(sumh_iop_table, bkt, m, node)
	{
		if (m->inode && m->inode->i_op == &m->shadow_iop)
			WRITE_ONCE(m->inode->i_op, m->orig_iop);
		if (m->inode && m->inode->i_mapping)
			clear_bit(AS_FLAGS_SUMH_IOP_INSTALLED,
				  &m->inode->i_mapping->flags);
	}
	spin_unlock(&sumh_iop_lock);
	synchronize_rcu();
	if (!atomic_read(&sumh_iop_live)) {
		sumh_iop_callbacks_quiesced = true;
	} else if (!sumh_iop_first_tasks_sync) {
		/* Close the load-pointer-to-callback-entry window for ownerless
		 * inode_operations before the control fd can be released.
		 */
		synchronize_rcu_tasks();
		sumh_iop_first_tasks_sync = true;
	}
	mutex_unlock(&sumh_iop_quiesce_lock);
}

bool sumh_iop_override_quiesced(void)
{
	bool quiesced;

	mutex_lock(&sumh_iop_quiesce_lock);
	if (!sumh_iop_callbacks_quiesced && sumh_iop_first_tasks_sync &&
	    !atomic_read(&sumh_iop_active_callbacks)) {
		/* The first Tasks-RCU grace period forced every stale pre-entry
		 * caller into the wrapper.  This second one covers the wrapper
		 * epilogue after its active count reaches zero.
		 */
		synchronize_rcu_tasks();
		sumh_iop_callbacks_quiesced = true;
	}
	quiesced = sumh_iop_callbacks_quiesced;
	mutex_unlock(&sumh_iop_quiesce_lock);
	return quiesced;
}

void sumh_iop_override_exit(void)
{
	struct sumh_iop_meta *m;
	struct hlist_node *tmp;
	LIST_HEAD(retired);
	int bkt;

	sumh_iop_override_stop_new();
	if (!sumh_iop_override_quiesced()) {
		wait_event(sumh_iop_quiesce_wait,
			   !atomic_read(&sumh_iop_active_callbacks));
		WARN_ON_ONCE(!sumh_iop_override_quiesced());
	}
	spin_lock(&sumh_iop_lock);
	hash_for_each_safe(sumh_iop_table, bkt, tmp, m, node)
	{
		hash_del_rcu(&m->node);
		list_add_tail(&m->retire_node, &retired);
	}
	spin_unlock(&sumh_iop_lock);
	synchronize_rcu();
	while (!list_empty(&retired)) {
		m = list_first_entry(&retired, struct sumh_iop_meta,
				     retire_node);
		list_del(&m->retire_node);
		atomic_dec(&sumh_iop_live);
		iput(m->inode);
		kfree(m);
	}

	rcu_barrier();
	pr_info("sumh: iop_override exited\n");
}
