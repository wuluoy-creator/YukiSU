#include <linux/cred.h>
#include <linux/hashtable.h>
#include <linux/file.h>
#include <linux/fs_struct.h>
#include <linux/fsnotify_backend.h>
#include <linux/namei.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/tracepoint.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>
#include <trace/events/sched.h>

#include "kasumi_owner.h"
#include "kasumi_owner_state.h"
#include "kasumi_owner_parse.h"
#include "kasumi_runtime.h"

#define KASUMI_OWNER_TASK_LIMIT 8192
#define KASUMI_OWNER_DEPTH 8

#define KASUMI_OWNER_PACKAGE_LIMIT 8192
#define KASUMI_OWNER_DATABASE_MAX (2UL * 1024 * 1024)

struct kasumi_code_table {
	struct path app_root;
	u32 count;
	u32 generation;
	size_t database_size;
	char *database;
	struct kasumi_owner_package entries[];
};

struct kasumi_owner_group {
	struct rcu_head rcu;
	unsigned int refs;
	bool pending;
	bool bound;
	struct kasumi_owner_state state;
};

struct kasumi_owner_task {
	struct hlist_node node;
	struct rcu_head rcu;
	struct task_struct *task;
	struct kasumi_owner_group *group;
};

static struct kasumi_code_table __rcu *kasumi_code_table;
static DEFINE_HASHTABLE(kasumi_owner_tasks, 10);
static DEFINE_SPINLOCK(kasumi_owner_lock);
static unsigned int kasumi_owner_count;
static bool kasumi_owner_enabled;
static struct tracepoint *kasumi_owner_fork_tp;
static struct tracepoint *kasumi_owner_free_tp;

static struct kasumi_owner_task *kasumi_owner_find(struct task_struct *task)
{
	struct kasumi_owner_task *entry;

	hash_for_each_possible(kasumi_owner_tasks, entry, node,
			       (unsigned long)task)
	{
		if (entry->task == task)
			return entry;
	}
	return NULL;
}

static struct kasumi_owner_task *kasumi_owner_find_rcu(struct task_struct *task)
{
	struct kasumi_owner_task *entry;

	hash_for_each_possible_rcu(kasumi_owner_tasks, entry, node,
				   (unsigned long)task)
	{
		if (entry->task == task)
			return entry;
	}
	return NULL;
}

static void kasumi_owner_fork(void *unused, struct task_struct *parent,
			      struct task_struct *child)
{
	struct kasumi_owner_task *entry, *source;
	struct kasumi_owner_group *group = NULL;
	unsigned long flags;
	bool thread = child->tgid == parent->tgid;
	uid_t uid;

	(void)unused;
	if (!READ_ONCE(kasumi_owner_enabled) || (child->flags & PF_KTHREAD))
		return;
	rcu_read_lock();
	if (thread && !kasumi_owner_find_rcu(parent))
		goto unlock;
	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		goto unlock;
	if (!thread) {
		group = kzalloc(sizeof(*group), GFP_ATOMIC);
		if (!group) {
			kfree(entry);
			goto unlock;
		}
		group->refs = 1;
	}
	entry->task = child;
	uid = __kuid_val(task_uid(parent));
	spin_lock_irqsave(&kasumi_owner_lock, flags);
	source = kasumi_owner_find(parent);
	if (kasumi_owner_count >= KASUMI_OWNER_TASK_LIMIT ||
	    (thread && !source)) {
		spin_unlock_irqrestore(&kasumi_owner_lock, flags);
		kfree(group);
		kfree(entry);
		goto unlock;
	}
	if (thread) {
		group = source->group;
		group->refs++;
	} else {
		kasumi_owner_inherit(
		    &group->state, source ? &source->group->state : NULL, uid);
		group->pending = !group->state.frozen &&
				 !group->state.inherited &&
				 !group->state.ambiguous;
		group->bound = group->state.frozen;
	}
	entry->group = group;
	hash_add_rcu(kasumi_owner_tasks, &entry->node,
		     (unsigned long)entry->task);
	kasumi_owner_count++;
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
unlock:
	rcu_read_unlock();
}

static void kasumi_owner_free(void *unused, struct task_struct *task)
{
	struct kasumi_owner_task *entry;
	struct kasumi_owner_group *group = NULL;
	unsigned long flags;

	(void)unused;
	if (task->flags & PF_KTHREAD)
		return;
	rcu_read_lock();
	entry = kasumi_owner_find_rcu(task);
	if (!entry) {
		rcu_read_unlock();
		return;
	}
	spin_lock_irqsave(&kasumi_owner_lock, flags);
	hash_del_rcu(&entry->node);
	kasumi_owner_count--;
	if (!--entry->group->refs)
		group = entry->group;
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
	rcu_read_unlock();
	if (group)
		kfree_rcu(group, rcu);
	kfree_rcu(entry, rcu);
}

static void kasumi_owner_find_tp(struct tracepoint *tp, void *unused)
{
	(void)unused;
	if (!strcmp(tp->name, "sched_process_fork"))
		kasumi_owner_fork_tp = tp;
	else if (!strcmp(tp->name, "sched_process_free"))
		kasumi_owner_free_tp = tp;
}

static struct path kasumi_owner_root;
static const struct cred *kasumi_owner_cred;
static struct fsnotify_group *kasumi_owner_group;
static struct fsnotify_mark *kasumi_owner_mark;
static bool kasumi_owner_watch_stale;
static atomic_t kasumi_owner_package_generation = ATOMIC_INIT(1);
static typeof(file_open_root) *owner_file_open_root;
static void (*owner_fput_sync)(struct file *);
static typeof(fsnotify_alloc_group) *owner_alloc_group;
static typeof(fsnotify_destroy_group) *owner_destroy_group;
static typeof(fsnotify_init_mark) *owner_init_mark;
static typeof(fsnotify_add_mark) *owner_add_mark;
static typeof(fsnotify_destroy_mark) *owner_destroy_mark;
static typeof(fsnotify_put_mark) *owner_put_mark;

static void kasumi_owner_refresh(struct work_struct *work);
static DECLARE_DELAYED_WORK(kasumi_owner_work, kasumi_owner_refresh);

static void kasumi_owner_schedule(unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&kasumi_owner_lock, flags);
	if (kasumi_owner_enabled)
		mod_delayed_work(system_wq, &kasumi_owner_work, delay);
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
}

static int kasumi_owner_event(struct fsnotify_mark *mark, u32 mask,
			      struct inode *inode, struct inode *dir,
			      const struct qstr *name, u32 cookie)
{
	(void)mark;
	(void)inode;
	(void)dir;
	(void)cookie;
	if (mask & (FS_DELETE_SELF | FS_MOVE_SELF | FS_UNMOUNT))
		WRITE_ONCE(kasumi_owner_watch_stale, true);
	if (READ_ONCE(kasumi_owner_watch_stale) ||
	    (name && name->len == 13 &&
	     !memcmp(name->name, "packages.list", 13))) {
		atomic_inc(&kasumi_owner_package_generation);
		kasumi_owner_schedule(msecs_to_jiffies(250));
	}
	return 0;
}

static void kasumi_owner_mark_free(struct fsnotify_mark *mark)
{
	kfree(mark);
}

static const struct fsnotify_ops kasumi_owner_notify_ops = {
    .handle_inode_event = kasumi_owner_event,
    .free_mark = kasumi_owner_mark_free,
};

static int kasumi_owner_root_init(void)
{
	struct task_struct *task;
	struct pid *pid = find_get_pid(1);

	if (!pid)
		return -ESRCH;
	task = get_pid_task(pid, PIDTYPE_PID);
	put_pid(pid);
	if (!task)
		return -ESRCH;
	task_lock(task);
	if (task->fs)
		get_fs_root(task->fs, &kasumi_owner_root);
	task_unlock(task);
	if (kasumi_owner_root.dentry)
		kasumi_owner_cred = get_task_cred(task);
	put_task_struct(task);
	return kasumi_owner_root.dentry ? 0 : -ESRCH;
}

static struct file *KASUMI_NOCFI kasumi_owner_open(const char *path, int flags)
{
	const struct cred *old = override_creds(kasumi_owner_cred);
	struct file *file;

	file = owner_file_open_root(&kasumi_owner_root, path, flags, 0);
	revert_creds(old);
	return file;
}

static int KASUMI_NOCFI kasumi_owner_watch(void)
{
	struct fsnotify_mark *mark;
	struct file *file;
	int ret;

	if (kasumi_owner_mark && !READ_ONCE(kasumi_owner_watch_stale))
		return 0;
	if (kasumi_owner_mark) {
		owner_destroy_mark(kasumi_owner_mark, kasumi_owner_group);
		owner_put_mark(kasumi_owner_mark);
		kasumi_owner_mark = NULL;
	}
	file = kasumi_owner_open("/data/system", O_PATH | O_DIRECTORY);
	if (IS_ERR(file))
		return (int)PTR_ERR(file);
	mark = kzalloc(sizeof(*mark), GFP_KERNEL);
	if (!mark) {
		owner_fput_sync(file);
		return -ENOMEM;
	}
	owner_init_mark(mark, kasumi_owner_group);
	mark->mask = FS_MOVED_TO | FS_CREATE | FS_DELETE | FS_CLOSE_WRITE |
		     FS_DELETE_SELF | FS_MOVE_SELF | FS_UNMOUNT |
		     FS_EVENT_ON_CHILD;
	WRITE_ONCE(kasumi_owner_watch_stale, false);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	ret =
	    owner_add_mark(mark, file_inode(file), FSNOTIFY_OBJ_TYPE_INODE, 0);
#else
	ret = owner_add_mark(mark, &file_inode(file)->i_fsnotify_marks,
			     FSNOTIFY_OBJ_TYPE_INODE, 0, NULL);
#endif
	owner_fput_sync(file);
	if (ret)
		owner_put_mark(mark);
	else
		kasumi_owner_mark = mark;
	return ret;
}

static int kasumi_package_compare(const void *left, const void *right)
{
	const struct kasumi_owner_package *a = left, *b = right;

	return strcmp(a->name, b->name);
}

static void kasumi_owner_table_free(struct kasumi_code_table *table)
{
	if (!table)
		return;
	if (table->app_root.dentry)
		path_put(&table->app_root);
	kvfree(table);
}

static bool kasumi_owner_database_equal(const struct kasumi_code_table *table,
					const char *buffer, size_t size)
{
	return table && table->database && table->database_size == size &&
	       !memcmp(table->database, buffer, size);
}

static int KASUMI_NOCFI kasumi_owner_load(void)
{
	struct kasumi_code_table *table, *old;
	struct file *file;
	const struct cred *saved_cred;
	char *buffer, *line, *end;
	loff_t pos = 0, size;
	ssize_t got;
	int ret = -EINVAL;
	u32 i, capacity = 0;
	u32 generation = (u32)atomic_read(&kasumi_owner_package_generation);

	file = kasumi_owner_open("/data/system/packages.list",
				 O_RDONLY | O_NOFOLLOW | O_NOATIME);
	if (IS_ERR(file))
		return (int)PTR_ERR(file);
	size = i_size_read(file_inode(file));
	if (size <= 0 || size > KASUMI_OWNER_DATABASE_MAX) {
		owner_fput_sync(file);
		return -EFBIG;
	}
	buffer = kvmalloc(size + 1, GFP_KERNEL);
	if (!buffer) {
		owner_fput_sync(file);
		return -ENOMEM;
	}
	saved_cred = override_creds(kasumi_owner_cred);
	while (pos < size) {
		got = kernel_read(file, buffer + pos, size - pos, &pos);
		if (got <= 0)
			break;
	}
	revert_creds(saved_cred);
	if (pos != size || i_size_read(file_inode(file)) != size ||
	    buffer[size - 1] != '\n') {
		owner_fput_sync(file);
		kvfree(buffer);
		return -EAGAIN;
	}
	owner_fput_sync(file);
	buffer[size] = '\0';
	if (generation != (u32)atomic_read(&kasumi_owner_package_generation)) {
		kvfree(buffer);
		return -EAGAIN;
	}
	rcu_read_lock();
	old = rcu_dereference(kasumi_code_table);
	if (kasumi_owner_database_equal(old, buffer, size)) {
		WRITE_ONCE(old->generation, generation);
		rcu_read_unlock();
		kvfree(buffer);
		return 0;
	}
	rcu_read_unlock();
	for (line = buffer; line < buffer + size; line++)
		if (*line == '\n')
			capacity++;
	if (!capacity || capacity > KASUMI_OWNER_PACKAGE_LIMIT) {
		kvfree(buffer);
		return -E2BIG;
	}
	table = kvzalloc(sizeof(*table) + capacity * sizeof(table->entries[0]) +
			     size,
			 GFP_KERNEL);
	if (table) {
		table->database = (char *)(table->entries + capacity);
		table->database_size = size;
		memcpy(table->database, buffer, size);
	}
	if (!table) {
		kvfree(buffer);
		return -ENOMEM;
	}
	for (line = buffer; line < buffer + size; line = end + 1) {
		struct kasumi_owner_package package = {};

		end = strchr(line, '\n');
		if (!end ||
		    !kasumi_owner_parse_line(line, end - line, &package))
			goto out;
		if (package.appid < 10000 || package.appid >= 20000)
			continue;
		if (table->count == KASUMI_OWNER_PACKAGE_LIMIT) {
			ret = -E2BIG;
			goto out;
		}
		table->entries[table->count++] = package;
	}
	if (!table->count)
		goto out;
	sort(table->entries, table->count, sizeof(table->entries[0]),
	     kasumi_package_compare, NULL);
	for (i = 1; i < table->count; i++) {
		if (!kasumi_package_compare(&table->entries[i - 1],
					    &table->entries[i]))
			goto out;
	}
	file = kasumi_owner_open("/data/app", O_PATH | O_DIRECTORY);
	if (IS_ERR(file)) {
		ret = (int)PTR_ERR(file);
		goto out;
	}
	table->app_root = file->f_path;
	path_get(&table->app_root);
	owner_fput_sync(file);
	if (generation != (u32)atomic_read(&kasumi_owner_package_generation)) {
		ret = -EAGAIN;
		goto out;
	}
	table->generation = generation;
	old = rcu_dereference_protected(kasumi_code_table, 1);
	rcu_assign_pointer(kasumi_code_table, table);
	synchronize_rcu();
	kasumi_owner_table_free(old);
	pr_info("kasumi: owner registry loaded %u packages\n", table->count);
	kvfree(buffer);
	return 0;
out:
	kasumi_owner_table_free(table);
	kvfree(buffer);
	return ret;
}

static void kasumi_owner_refresh(struct work_struct *work)
{
	int ret;

	(void)work;
	if (!READ_ONCE(kasumi_owner_enabled))
		return;
	if (!kasumi_owner_root.dentry) {
		ret = kasumi_owner_root_init();
		if (ret)
			goto retry;
	}
	ret = kasumi_owner_watch();
	if (!ret)
		ret = kasumi_owner_load();
retry:
	if (ret)
		kasumi_owner_schedule(10UL * HZ);
}

static u32 kasumi_code_lookup(const struct kasumi_code_table *table,
			      struct dentry *dentry)
{
	struct dentry *parent;
	struct kasumi_owner_package key = {};
	size_t length;
	u32 lo = 0, hi = table->count;
	bool installed;

	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent;
	for (length = 0; length < dentry->d_name.len; length++) {
		if (dentry->d_name.name[length] == '-')
			break;
	}
	if (!length || length >= sizeof(key.name) ||
	    length == dentry->d_name.len || d_unhashed(dentry)) {
		spin_unlock(&dentry->d_lock);
		return 0;
	}
	memcpy(key.name, dentry->d_name.name, length);
	spin_unlock(&dentry->d_lock);
	if (!parent)
		return 0;
	installed = parent == table->app_root.dentry;
	if (!installed) {
		spin_lock(&parent->d_lock);
		installed = parent->d_parent == table->app_root.dentry &&
			    !d_unhashed(parent) && parent->d_name.len > 2 &&
			    parent->d_name.name[0] == '~' &&
			    parent->d_name.name[1] == '~';
		spin_unlock(&parent->d_lock);
	}
	if (!installed)
		return 0;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		int cmp = kasumi_package_compare(&key, &table->entries[mid]);

		if (!cmp)
			return table->entries[mid].appid;
		if (cmp < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return 0;
}

static bool kasumi_owner_code_name(struct dentry *dentry)
{
	const struct qstr *name = &dentry->d_name;
	bool match = false;
	static const char *const suffixes[] = {".apk", ".dex", ".odex", ".vdex",
					       ".so"};
	size_t i;

	spin_lock(&dentry->d_lock);
	for (i = 0; i < ARRAY_SIZE(suffixes); i++) {
		size_t length = strlen(suffixes[i]);

		if (name->len >= length &&
		    !memcmp(name->name + name->len - length, suffixes[i],
			    length)) {
			match = true;
			break;
		}
	}
	spin_unlock(&dentry->d_lock);
	return match;
}

void kasumi_owner_observe(struct file *file)
{
	struct kasumi_owner_task *entry;
	const struct kasumi_code_table *table;
	struct dentry *dentry;
	unsigned long flags;
	uid_t uid = __kuid_val(current_uid());
	u32 appid = 0, generation;
	unsigned int depth;

	if (!READ_ONCE(kasumi_owner_enabled) || uid % 100000 < 90000 || !file ||
	    !file->f_path.dentry || !file_inode(file) ||
	    !S_ISREG(file_inode(file)->i_mode))
		return;
	rcu_read_lock();
	entry = kasumi_owner_find_rcu(current);
	if (!entry || !READ_ONCE(entry->group->pending))
		goto unlock;
	generation = (u32)atomic_read(&kasumi_owner_package_generation);
	table = rcu_dereference(kasumi_code_table);
	if (!table || READ_ONCE(table->generation) != generation ||
	    file_inode(file)->i_sb != table->app_root.dentry->d_sb ||
	    !kasumi_owner_code_name(file->f_path.dentry))
		goto unlock;
	dentry = file->f_path.dentry;
	for (depth = 0; dentry && depth < KASUMI_OWNER_DEPTH; depth++) {
		const struct inode *inode = d_inode(dentry);

		if (inode)
			appid = kasumi_code_lookup(table, dentry);
		if (appid || dentry == READ_ONCE(dentry->d_parent))
			break;
		dentry = READ_ONCE(dentry->d_parent);
	}
unlock:
	rcu_read_unlock();
	if (!appid)
		return;
	spin_lock_irqsave(&kasumi_owner_lock, flags);
	entry = kasumi_owner_find(current);
	if (entry &&
	    generation == (u32)atomic_read(&kasumi_owner_package_generation)) {
		kasumi_owner_candidate(&entry->group->state,
				       uid - uid % 100000 + appid, generation);
		if (entry->group->state.ambiguous)
			WRITE_ONCE(entry->group->pending, false);
	}
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
}

uid_t kasumi_owner_freeze(void)
{
	struct kasumi_owner_task *entry;
	unsigned long flags;
	uid_t uid = 0;

	rcu_read_lock();
	entry = kasumi_owner_find_rcu(current);
	if (!entry || smp_load_acquire(&entry->group->bound)) {
		if (entry)
			uid = READ_ONCE(entry->group->state.uid);
		rcu_read_unlock();
		return uid;
	}
	rcu_read_unlock();
	spin_lock_irqsave(&kasumi_owner_lock, flags);
	entry = kasumi_owner_find(current);
	if (entry) {
		uid = entry->group->state.uid;
		if (!entry->group->state.frozen && uid &&
		    uid / 100000 != __kuid_val(current_uid()) / 100000) {
			entry->group->state.uid = 0;
			entry->group->state.ambiguous = true;
		}
		uid = kasumi_owner_bind(
		    &entry->group->state,
		    (u32)atomic_read(&kasumi_owner_package_generation));
		WRITE_ONCE(entry->group->pending, false);
		smp_store_release(&entry->group->bound, true);
	}
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
	return uid;
}

bool kasumi_owner_available(void)
{
	return READ_ONCE(kasumi_owner_enabled);
}

int KASUMI_NOCFI kasumi_owner_init(void)
{
	int ret;

	owner_file_open_root = (void *)kasumi_lookup_callable("file_open_root");
	owner_fput_sync = (void *)kasumi_lookup_callable("__fput_sync");
	owner_alloc_group =
	    (void *)kasumi_lookup_callable("fsnotify_alloc_group");
	owner_destroy_group =
	    (void *)kasumi_lookup_callable("fsnotify_destroy_group");
	owner_init_mark = (void *)kasumi_lookup_callable("fsnotify_init_mark");
	owner_add_mark = (void *)kasumi_lookup_callable("fsnotify_add_mark");
	owner_destroy_mark =
	    (void *)kasumi_lookup_callable("fsnotify_destroy_mark");
	owner_put_mark = (void *)kasumi_lookup_callable("fsnotify_put_mark");
	if (!owner_file_open_root || !owner_fput_sync || !owner_alloc_group ||
	    !owner_destroy_group || !owner_init_mark || !owner_add_mark ||
	    !owner_destroy_mark || !owner_put_mark)
		return -EOPNOTSUPP;

	for_each_kernel_tracepoint(kasumi_owner_find_tp, NULL);
	if (!kasumi_owner_fork_tp || !kasumi_owner_free_tp)
		return -ENOENT;
	check_trace_callback_type_sched_process_fork(kasumi_owner_fork);
	check_trace_callback_type_sched_process_free(kasumi_owner_free);
	ret = tracepoint_probe_register(kasumi_owner_free_tp,
					(void *)kasumi_owner_free, NULL);
	if (ret)
		return ret;
	ret = tracepoint_probe_register(kasumi_owner_fork_tp,
					(void *)kasumi_owner_fork, NULL);
	if (ret) {
		tracepoint_probe_unregister(kasumi_owner_free_tp,
					    (void *)kasumi_owner_free, NULL);
		tracepoint_synchronize_unregister();
		return ret;
	}
	kasumi_owner_group =
	    owner_alloc_group(&kasumi_owner_notify_ops, FSNOTIFY_GROUP_DUPS);
	if (IS_ERR(kasumi_owner_group)) {
		ret = (int)PTR_ERR(kasumi_owner_group);
		kasumi_owner_group = NULL;
		tracepoint_probe_unregister(kasumi_owner_fork_tp,
					    (void *)kasumi_owner_fork, NULL);
		tracepoint_probe_unregister(kasumi_owner_free_tp,
					    (void *)kasumi_owner_free, NULL);
		tracepoint_synchronize_unregister();
		return ret;
	}
	WRITE_ONCE(kasumi_owner_enabled, true);
	schedule_delayed_work(&kasumi_owner_work, 0);
	return 0;
}

void KASUMI_NOCFI kasumi_owner_exit(void)
{
	struct kasumi_owner_task *entry;
	struct kasumi_code_table *table;
	struct hlist_node *tmp;
	unsigned int bucket;
	unsigned long flags;
	bool enabled;

	spin_lock_irqsave(&kasumi_owner_lock, flags);
	enabled = kasumi_owner_enabled;
	WRITE_ONCE(kasumi_owner_enabled, false);
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
	if (enabled) {
		tracepoint_probe_unregister(kasumi_owner_fork_tp,
					    (void *)kasumi_owner_fork, NULL);
		tracepoint_probe_unregister(kasumi_owner_free_tp,
					    (void *)kasumi_owner_free, NULL);
		tracepoint_synchronize_unregister();
	}
	cancel_delayed_work_sync(&kasumi_owner_work);
	if (kasumi_owner_mark) {
		owner_destroy_mark(kasumi_owner_mark, kasumi_owner_group);
		owner_put_mark(kasumi_owner_mark);
		kasumi_owner_mark = NULL;
	}
	if (kasumi_owner_group) {
		owner_destroy_group(kasumi_owner_group);
		kasumi_owner_group = NULL;
	}
	if (kasumi_owner_cred) {
		put_cred(kasumi_owner_cred);
		kasumi_owner_cred = NULL;
	}
	if (kasumi_owner_root.dentry) {
		path_put(&kasumi_owner_root);
		kasumi_owner_root = (struct path){};
	}
	table = rcu_dereference_protected(kasumi_code_table, 1);
	RCU_INIT_POINTER(kasumi_code_table, NULL);
	synchronize_rcu();
	kasumi_owner_table_free(table);
	spin_lock_irqsave(&kasumi_owner_lock, flags);
	hash_for_each_safe(kasumi_owner_tasks, bucket, tmp, entry, node)
	{
		hash_del_rcu(&entry->node);
		if (!--entry->group->refs)
			kfree_rcu(entry->group, rcu);
		kfree_rcu(entry, rcu);
	}
	kasumi_owner_count = 0;
	spin_unlock_irqrestore(&kasumi_owner_lock, flags);
}
