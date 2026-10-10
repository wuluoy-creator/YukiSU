/*
 * Bridge old fops_get() implementations to module-owned per-open fops.
 *
 * Android kernels before 6.12 evaluate fops_get(inode->i_fop) multiple
 * times.  Replacing inode->i_fop between those evaluations can otherwise pin
 * one table's owner and return another table.  The inode therefore points to
 * an ingress copy whose owner matches the original table.  The first
 * android_vh_check_file_open callback transfers the reference to a live
 * SUMH-owned table before any file callback can run.
 */
#include "sumh_fop_bridge.h"
#include "sumh_runtime.h"

#include <linux/hashtable.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

#define SUMH_FOP_BRIDGE_HASH_BITS 8

static DEFINE_HASHTABLE(sumh_fop_bridge_table, SUMH_FOP_BRIDGE_HASH_BITS);
static DEFINE_SPINLOCK(sumh_fop_bridge_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
static struct tracepoint *sumh_check_file_open_tp;
static bool sumh_fop_bridge_registered;
#endif
static bool sumh_fop_bridge_available;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
static struct sumh_fop_bridge_entry *
sumh_fop_bridge_lookup_rcu(const struct file_operations *ingress)
{
	struct sumh_fop_bridge_entry *entry;

	hash_for_each_possible_rcu(sumh_fop_bridge_table, entry, node,
				   (unsigned long)ingress)
	{
		if (entry->ingress == ingress)
			return entry;
	}
	return NULL;
}

static void sumh_fop_bridge_check_file_open(void *data,
					    const struct file *const_file)
{
	struct sumh_fop_bridge_entry *entry;
	struct file *file = (struct file *)const_file;
	const struct file_operations *old_fops;
	const struct file_operations *live;

	(void)data;
	if (!file)
		return;

	old_fops = READ_ONCE(file->f_op);
	rcu_read_lock();
	entry = sumh_fop_bridge_lookup_rcu(old_fops);
	if (!entry)
		goto out;

	/* current owns the same module as orig. Transfer that reference to the
	 * SUMH-owned live table before security_file_open() or ->open().
	 */
	live = fops_get(entry->live);
	if (likely(live)) {
		WRITE_ONCE(file->f_op, live);
		fops_put(old_fops);
	} else {
		/* During an abnormal module state, never leave a file pointing
		 * at unpinned ingress storage. The existing owner reference
		 * also belongs to orig because ingress and orig deliberately
		 * share an owner.
		 */
		WRITE_ONCE(file->f_op, entry->orig);
	}
out:
	rcu_read_unlock();
}

static void sumh_fop_bridge_find_tracepoint(struct tracepoint *tp, void *priv)
{
	struct tracepoint **result = priv;

	if (!*result && tp->name &&
	    !strcmp(tp->name, "android_vh_check_file_open"))
		*result = tp;
}

static bool sumh_fop_bridge_is_first(void)
{
	struct tracepoint_func *funcs;
	bool first = false;

	rcu_read_lock_sched();
	funcs = rcu_dereference_sched(sumh_check_file_open_tp->funcs);
	if (funcs && funcs[0].func == (void *)sumh_fop_bridge_check_file_open)
		first = true;
	rcu_read_unlock_sched();
	return first;
}
#endif

int sumh_fop_bridge_init(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	int ret;
#endif

	hash_init(sumh_fop_bridge_table);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	return 0;
#else
	for_each_kernel_tracepoint(sumh_fop_bridge_find_tracepoint,
				   &sumh_check_file_open_tp);
	if (!sumh_check_file_open_tp)
		return -ENOENT;

	ret = tracepoint_probe_register_prio(
	    sumh_check_file_open_tp, (void *)sumh_fop_bridge_check_file_open,
	    NULL, INT_MAX);
	if (ret)
		return ret;
	sumh_fop_bridge_registered = true;
	if (!sumh_fop_bridge_is_first()) {
		tracepoint_probe_unregister(
		    sumh_check_file_open_tp,
		    (void *)sumh_fop_bridge_check_file_open, NULL);
		tracepoint_synchronize_unregister();
		sumh_fop_bridge_registered = false;
		sumh_check_file_open_tp = NULL;
		return -EBUSY;
	}

	sumh_fop_bridge_available = true;
	pr_info("sumh: old-KMI fops bridge initialized\n");
	return 0;
#endif
}

bool sumh_fop_bridge_register(struct sumh_fop_bridge_entry *entry,
			      const struct file_operations *ingress,
			      const struct file_operations *live,
			      const struct file_operations *orig)
{
	if (!entry || !ingress || !live || !orig ||
	    !READ_ONCE(sumh_fop_bridge_available))
		return false;

	entry->ingress = ingress;
	entry->live = live;
	entry->orig = orig;
	spin_lock(&sumh_fop_bridge_lock);
	hash_add_rcu(sumh_fop_bridge_table, &entry->node,
		     (unsigned long)ingress);
	entry->registered = true;
	spin_unlock(&sumh_fop_bridge_lock);
	return true;
}

void sumh_fop_bridge_unregister(struct sumh_fop_bridge_entry *entry)
{
	if (!entry || !entry->registered)
		return;
	spin_lock(&sumh_fop_bridge_lock);
	if (entry->registered) {
		hash_del_rcu(&entry->node);
		entry->registered = false;
	}
	spin_unlock(&sumh_fop_bridge_lock);
}

void sumh_fop_bridge_stop_new(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	if (!READ_ONCE(sumh_fop_bridge_registered))
		return;

	/* Callers restore every inode->i_fop before entering here. Tasks-RCU
	 * forces any task which fetched an ingress pointer to reach our
	 * highest-priority, non-sleeping tracepoint callback first.
	 */
	synchronize_rcu_tasks();
	tracepoint_probe_unregister(sumh_check_file_open_tp,
				    (void *)sumh_fop_bridge_check_file_open,
				    NULL);
	tracepoint_synchronize_unregister();
	WRITE_ONCE(sumh_fop_bridge_registered, false);
#endif
}

void sumh_fop_bridge_exit(void)
{
	sumh_fop_bridge_stop_new();
	synchronize_rcu();
	WARN_ON_ONCE(!hash_empty(sumh_fop_bridge_table));
	pr_info("sumh: fops bridge exited\n");
}
