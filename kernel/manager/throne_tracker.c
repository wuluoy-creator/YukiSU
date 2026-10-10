#include <linux/err.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "policy/allowlist.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/apk_sign.h"
#include "manager/dynamic_manager.h"
#include "manager/manager_identity.h"
#include "manager/throne_tracker.h"
#include "ksu.h"
#include "util.h"

uid_t ksu_manager_uid = KSU_INVALID_UID;
uid_t ksu_manager_appid = KSU_INVALID_UID;
uid_t ksu_manager_appids[KSU_MAX_MANAGER_KEYS] = {KSU_INVALID_UID};

static uid_t locked_manager_appids[KSU_MAX_MANAGER_KEYS] = {KSU_INVALID_UID};

static void update_primary_manager(void)
{
	int i;
	ksu_manager_appid = KSU_INVALID_UID;
	ksu_manager_uid = KSU_INVALID_UID;
	for (i = 0; i < KSU_MAX_MANAGER_KEYS; i++) {
		if (ksu_manager_appids[i] != KSU_INVALID_UID) {
			ksu_manager_appid = ksu_manager_appids[i];
			/* full uid: assume user 0 if not in process context */
			ksu_manager_uid = ksu_manager_appids[i];
			break;
		}
	}
}

void ksu_set_manager_appid_for_index(uid_t appid, int signature_index)
{
	int i;

	if (signature_index < 0 || signature_index >= KSU_MAX_MANAGER_KEYS)
		return;

	/*
	 * Keep multi-manager coexistence stable:
	 * - If this appid is already present in any slot, reuse that slot.
	 * - If target signature slot is occupied by another app, place this app
	 *   into an empty slot instead of overwriting.
	 */
	for (i = 0; i < KSU_MAX_MANAGER_KEYS; i++) {
		if (ksu_manager_appids[i] == appid) {
			ksu_manager_appids[i] = appid;
			locked_manager_appids[i] = appid;
			update_primary_manager();
			return;
		}
	}

	if (ksu_manager_appids[signature_index] == KSU_INVALID_UID ||
	    ksu_manager_appids[signature_index] == appid) {
		ksu_manager_appids[signature_index] = appid;
		locked_manager_appids[signature_index] = appid;
		update_primary_manager();
		return;
	}

	for (i = 0; i < KSU_MAX_MANAGER_KEYS; i++) {
		if (ksu_manager_appids[i] == KSU_INVALID_UID) {
			ksu_manager_appids[i] = appid;
			locked_manager_appids[i] = appid;
			update_primary_manager();
			return;
		}
	}

	/* No empty slot left, keep historical behavior. */
	ksu_manager_appids[signature_index] = appid;
	locked_manager_appids[signature_index] = appid;
	update_primary_manager();
}

#define KSU_UID_LIST_PATH "/data/misc/user_uid/uid_list"
#define SYSTEM_PACKAGES_LIST_PATH "/data/system/packages.list"

struct uid_data {
	struct list_head list;
	u32 appid;
	char package[KSU_MAX_PACKAGE_NAME];
};

// Try read /data/misc/user_uid/uid_list
static int uid_from_um_list(struct list_head *uid_list)
{
	struct file *fp;
	char *buf = NULL;
	loff_t size, pos = 0;
	ssize_t nr;
	char *line = NULL;
	char *next = NULL;
	int cnt = 0;

	fp = filp_open(KSU_UID_LIST_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		return -ENOENT;
	}

	size = i_size_read(file_inode(fp));
	if (size <= 0) {
		filp_close(fp, NULL);
		return -ENODATA;
	}

	buf = kzalloc(size + 1, GFP_ATOMIC);
	if (!buf) {
		pr_err("uid_list: OOM %lld B\n", size);
		filp_close(fp, NULL);
		return -ENOMEM;
	}

	nr = kernel_read(fp, buf, size, &pos);
	filp_close(fp, NULL);
	if (nr != size) {
		pr_err("uid_list: short read %zd/%lld\n", nr, size);
		kfree(buf);
		return -EIO;
	}
	buf[size] = '\0';

	for (line = buf; line; line = next) {
		char *uid_str = NULL;
		char *pkg = NULL;
		u32 uid;
		struct uid_data *d = NULL;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		while (*line == ' ' || *line == '\t' || *line == '\r')
			++line;
		if (!*line)
			continue;

		uid_str = strsep(&line, " \t");
		pkg = line;
		if (!pkg)
			continue;
		while (*pkg == ' ' || *pkg == '\t')
			++pkg;
		if (!*pkg)
			continue;

		if (kstrtou32(uid_str, 10, &uid)) {
			pr_warn_once("uid_list: bad uid <%s>\n", uid_str);
			continue;
		}

		d = kzalloc(sizeof(*d), GFP_ATOMIC);
		if (unlikely(!d)) {
			pr_err("uid_list: OOM appid=%u\n", uid);
			continue;
		}

		d->appid = uid;
		strscpy(d->package, pkg, KSU_MAX_PACKAGE_NAME);
		list_add_tail(&d->list, uid_list);
		++cnt;
		// Log first few entries for debug
		if (cnt <= 5) {
		}
	}

	kfree(buf);
	pr_info("uid_list: loaded %d entries\n", cnt);
	return cnt > 0 ? 0 : -ENODATA;
}

static int get_pkg_from_apk_path(char *pkg, const char *path)
{
	int len = strlen(path);
	/* The installation path also contains random directory suffixes. Only
	 * the extracted package name is bounded by KSU_MAX_PACKAGE_NAME. */
	if (len < 1)
		return -1;

	const char *last_slash = NULL;
	const char *second_last_slash = NULL;

	int i;
	for (i = len - 1; i >= 0; i--) {
		if (path[i] == '/') {
			if (!last_slash) {
				last_slash = &path[i];
			} else {
				second_last_slash = &path[i];
				break;
			}
		}
	}

	if (!last_slash || !second_last_slash)
		return -1;

	const char *last_hyphen = strchr(second_last_slash, '-');
	if (!last_hyphen || last_hyphen > last_slash)
		return -1;

	int pkg_len = last_hyphen - second_last_slash - 1;
	if (pkg_len >= KSU_MAX_PACKAGE_NAME || pkg_len <= 0)
		return -1;

	// Copying the package name
	memcpy(pkg, second_last_slash + 1, pkg_len);
	pkg[pkg_len] = '\0';

	return 0;
}

static void crown_manager(const char *apk, struct list_head *uid_data,
			  int signature_index)
{
	char pkg[KSU_MAX_PACKAGE_NAME];
	if (get_pkg_from_apk_path(pkg, apk) < 0) {
		pr_err("Failed to get package name from apk path: %s\n", apk);
		return;
	}

	pr_info("manager pkg: %s, signature_index: %d\n", pkg, signature_index);

#ifdef KSU_MANAGER_PACKAGE
	// pkg is `/<real package>`
	if (strncmp(pkg, KSU_MANAGER_PACKAGE, sizeof(KSU_MANAGER_PACKAGE))) {
		pr_info("manager package is inconsistent with kernel build: %s "
			"vs %s\n",
			pkg, KSU_MANAGER_PACKAGE);
		return;
	}
#endif // #ifdef KSU_MANAGER_PACKAGE
	struct list_head *list = (struct list_head *)uid_data;
	struct uid_data *np;

	list_for_each_entry (np, list, list) {
		if (strncmp(np->package, pkg, KSU_MAX_PACKAGE_NAME) == 0) {
			pr_info("Crowning manager: %s (appid=%d) "
				"signature_index=%d\n",
				pkg, np->appid, signature_index);

			if (signature_index >= 0 &&
			    signature_index < KSU_MAX_MANAGER_KEYS) {
				ksu_set_manager_appid_for_index(
				    np->appid, signature_index);
			} else {
				ksu_set_manager_appid(np->appid);
			}
			break;
		}
	}
}

static void note_scanned_manager(const char *apk, struct list_head *uid_data,
				 const struct apk_sign_match *match)
{
	char pkg[KSU_MAX_PACKAGE_NAME];
	struct list_head *list = (struct list_head *)uid_data;
	struct uid_data *np;

	if (get_pkg_from_apk_path(pkg, apk) < 0) {
		pr_err("Failed to get package name from apk path: %s\n", apk);
		return;
	}

	list_for_each_entry (np, list, list) {
		if (strncmp(np->package, pkg, KSU_MAX_PACKAGE_NAME) == 0) {
			pr_info("Noting dynamic manager candidate: %s "
				"(appid=%d) "
				"signature=%s\n",
				pkg, np->appid,
				match && match->name ? match->name : "unknown");
			ksu_dynamic_manager_note_scanned(np->appid, match);
			break;
		}
	}
}

#define DATA_PATH_LEN 384 // 384 is enough for /data/app/<package>/base.apk

struct data_path {
	char dirpath[DATA_PATH_LEN];
	int depth;
	struct list_head list;
};

struct apk_scan_entry {
	char path[DATA_PATH_LEN];
	u32 appid;
	struct kstat stat;
	bool exists;
	struct list_head list;
};

/* All scan entry points, cache updates and forced rescans share this lock. */
static DEFINE_MUTEX(throne_scan_lock);
static LIST_HEAD(apk_scan_cache);
static bool manager_scan_forced;

static void clear_apk_scan_cache(void)
{
	struct apk_scan_entry *entry, *next;

	list_for_each_entry_safe (entry, next, &apk_scan_cache, list) {
		list_del(&entry->list);
		kfree(entry);
	}
}

static struct apk_scan_entry *find_apk_scan_entry(const char *path)
{
	struct apk_scan_entry *entry;

	list_for_each_entry (entry, &apk_scan_cache, list) {
		if (!strcmp(entry->path, path))
			return entry;
	}
	return NULL;
}

static bool get_apk_appid(const char *path, struct list_head *uid_list,
			  u32 *appid)
{
	char package[KSU_MAX_PACKAGE_NAME];
	struct uid_data *entry;

	if (get_pkg_from_apk_path(package, path))
		return false;
	list_for_each_entry (entry, uid_list, list) {
		if (!strcmp(entry->package, package)) {
			*appid = entry->appid;
			return true;
		}
	}
	return false;
}

static bool stat_apk(const char *name, struct kstat *stat)
{
	struct path path;
	int ret;
	u32 required =
	    STATX_TYPE | STATX_INO | STATX_SIZE | STATX_MTIME | STATX_CTIME;

	/* Metadata lookup does not open or enumerate the package directory. */
	if (kern_path(name, 0, &path))
		return false;
	memset(stat, 0, sizeof(*stat));
	ret =
	    vfs_getattr(&path, stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
	path_put(&path);
	return !ret && S_ISREG(stat->mode) &&
	       (stat->result_mask & required) == required;
}

static bool same_apk_stat(const struct kstat *a, const struct kstat *b)
{
	return a->dev == b->dev && a->ino == b->ino && a->size == b->size &&
	       a->mtime.tv_sec == b->mtime.tv_sec &&
	       a->mtime.tv_nsec == b->mtime.tv_nsec &&
	       a->ctime.tv_sec == b->ctime.tv_sec &&
	       a->ctime.tv_nsec == b->ctime.tv_nsec;
}

static bool cached_apk_directory(const char *directory,
				 struct list_head *uid_list)
{
	char apk[DATA_PATH_LEN];
	struct apk_scan_entry *entry;
	struct kstat stat;
	u32 appid;

	if (snprintf(apk, sizeof(apk), "%s/base.apk", directory) >= sizeof(apk))
		return false;
	entry = find_apk_scan_entry(apk);
	if (!entry || !get_apk_appid(apk, uid_list, &appid) ||
	    entry->appid != appid || !stat_apk(apk, &stat) ||
	    !same_apk_stat(&entry->stat, &stat))
		return false;

	entry->exists = true;
	return true;
}

static void scan_manager_apk(char *path, struct list_head *uid_list)
{
	struct apk_scan_entry *entry = find_apk_scan_entry(path);
	struct apk_sign_match match = {.index = -1};
	struct kstat before, after;
	u32 appid;
	bool cacheable =
	    get_apk_appid(path, uid_list, &appid) && stat_apk(path, &before);

	if (match_apk_signature(path, &match)) {
		if (match.trusted && match.index >= 0)
			crown_manager(path, uid_list, match.index);
		else
			note_scanned_manager(path, uid_list, &match);
	}

	/* Trusted managers are always revalidated when a search is needed.
	 * Keep unrelated cache entries when one is found. Never cache a file
	 * that changed while its signature was being read. */
	if (match.trusted || !cacheable || !stat_apk(path, &after) ||
	    !same_apk_stat(&before, &after)) {
		if (entry) {
			list_del(&entry->list);
			kfree(entry);
		}
		return;
	}

	if (!entry) {
		entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
		if (!entry)
			return;
		strscpy(entry->path, path, sizeof(entry->path));
		list_add_tail(&entry->list, &apk_scan_cache);
	}
	entry->appid = appid;
	entry->stat = after;
	entry->exists = true;
}

struct my_dir_context {
	struct dir_context ctx;
	struct list_head *data_path_list;
	char *parent_dir;
	void *private_data;
	int depth;
	int *stop;
};

static bool my_actor(struct dir_context *ctx, const char *name, int namelen,
		     loff_t off, u64 ino, unsigned int d_type)
{
	struct my_dir_context *my_ctx =
	    container_of(ctx, struct my_dir_context, ctx);
	char dirpath[DATA_PATH_LEN];

	if (!my_ctx) {
		pr_err("Invalid context\n");
		return false;
	}
	if (my_ctx->stop && *my_ctx->stop) {
		pr_info("Stop searching\n");
		return false;
	}

	if (!strncmp(name, "..", namelen) || !strncmp(name, ".", namelen))
		return true; // Skip "." and ".."

	if (d_type == DT_DIR && namelen >= 8 && !strncmp(name, "vmdl", 4) &&
	    !strncmp(name + namelen - 4, ".tmp", 4)) {
		pr_info("Skipping directory: %.*s\n", namelen, name);
		return true; // Skip staging package
	}

	if (snprintf(dirpath, DATA_PATH_LEN, "%s/%.*s", my_ctx->parent_dir,
		     namelen, name) >= DATA_PATH_LEN) {
		pr_err("Path too long: %s/%.*s\n", my_ctx->parent_dir, namelen,
		       name);
		return true;
	}

	if (d_type == DT_DIR && my_ctx->depth > 0 &&
	    (my_ctx->stop && !*my_ctx->stop)) {
		struct data_path *data =
		    kzalloc(sizeof(struct data_path), GFP_ATOMIC);

		if (!data) {
			pr_err("Failed to allocate memory for %s\n", dirpath);
			return true;
		}

		strscpy(data->dirpath, dirpath, DATA_PATH_LEN);
		data->depth = my_ctx->depth - 1;
		list_add_tail(&data->list, my_ctx->data_path_list);
	} else if (namelen == 8 && !strncmp(name, "base.apk", namelen)) {
		scan_manager_apk(dirpath, my_ctx->private_data);
	}

	return true;
}

static void search_manager(const char *path, int depth,
			   struct list_head *uid_data)
{
	int i, stop = 0;
	unsigned long data_app_magic = 0;
	struct apk_scan_entry *pos, *n;
	struct list_head data_path_list;
	struct data_path data;

	INIT_LIST_HEAD(&data_path_list);

	// Initialize APK cache list
	list_for_each_entry (pos, &apk_scan_cache, list) {
		pos->exists = false;
	}

	// First depth
	strscpy(data.dirpath, path, DATA_PATH_LEN);
	data.depth = depth;
	list_add_tail(&data.list, &data_path_list);

	for (i = depth; i >= 0; i--) {
		struct data_path *pos, *n;

		list_for_each_entry_safe (pos, n, &data_path_list, list) {
			struct my_dir_context ctx = {.ctx.actor = my_actor,
						     .data_path_list =
							 &data_path_list,
						     .parent_dir = pos->dirpath,
						     .private_data = uid_data,
						     .depth = pos->depth,
						     .stop = &stop};
			struct file *file;

			if (!stop) {
				if (cached_apk_directory(pos->dirpath,
							 uid_data))
					goto skip_iterate;
				file = ksu_filp_open_nonotify(
				    pos->dirpath,
				    O_RDONLY | O_NOFOLLOW | O_NOATIME);
				if (IS_ERR(file)) {
					pr_err("Failed to open directory: %s, "
					       "err: %ld\n",
					       pos->dirpath, PTR_ERR(file));
					goto skip_iterate;
				}

				// grab magic on first folder, which is
				// /data/app
				if (!data_app_magic) {
					if (file->f_inode->i_sb->s_magic) {
						data_app_magic =
						    file->f_inode->i_sb
							->s_magic;
						pr_info("%s: dir: %s got "
							"magic! 0x%lx\n",
							__func__, pos->dirpath,
							data_app_magic);
					} else {
						filp_close(file, NULL);
						goto skip_iterate;
					}
				}

				if (file->f_inode->i_sb->s_magic !=
				    data_app_magic) {
					pr_info("%s: skip: %s magic: 0x%lx "
						"expected: 0x%lx\n",
						__func__, pos->dirpath,
						file->f_inode->i_sb->s_magic,
						data_app_magic);
					filp_close(file, NULL);
					goto skip_iterate;
				}

				iterate_dir(file, &ctx.ctx);
				filp_close(file, NULL);
			}
		skip_iterate:
			list_del(&pos->list);
			if (pos != &data)
				kfree(pos);
		}
	}

	// Remove stale cached APK entries
	list_for_each_entry_safe (pos, n, &apk_scan_cache, list) {
		if (!pos->exists) {
			list_del(&pos->list);
			kfree(pos);
		}
	}
}

static bool is_uid_exist(uid_t uid, char *package, void *data)
{
	struct list_head *list = (struct list_head *)data;
	struct uid_data *np;
	u32 appid = uid % 100000;
	bool exist = false;

	list_for_each_entry (np, list, list) {
		if (np->appid == appid &&
		    strncmp(np->package, package, KSU_MAX_PACKAGE_NAME) == 0) {
			exist = true;
			break;
		}
	}
	return exist;
}

static void track_throne_locked(bool prune_only)
{
	const struct cred *old_cred = override_creds(ksu_cred);
	struct list_head uid_list;
	struct uid_data *np, *n;
	struct file *fp;
	loff_t pos = 0;
	loff_t size;
	ssize_t nr;
	char *buf = NULL;
	char *line = NULL;
	char *next = NULL;
	bool manager_exist;
	bool need_search = false;

	// init uid list head
	INIT_LIST_HEAD(&uid_list);

	fp = filp_open(SYSTEM_PACKAGES_LIST_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("%s: open " SYSTEM_PACKAGES_LIST_PATH " failed: %ld\n",
		       __func__, PTR_ERR(fp));
		goto out;
	}

	size = i_size_read(file_inode(fp));
	if (size <= 0) {
		filp_close(fp, 0);
		goto out;
	}

	buf = kzalloc(size + 1, GFP_KERNEL);
	if (!buf) {
		filp_close(fp, 0);
		goto out;
	}

	nr = kernel_read(fp, buf, size, &pos);
	filp_close(fp, 0);
	if (nr != size) {
		pr_err("track_throne: read packages.list failed: %zd\n", nr);
		goto out;
	}
	buf[nr] = '\0';

	for (line = buf; line && *line; line = next) {
		struct uid_data *data = NULL;
		const char *delim = " \t";
		char *package = NULL;
		char *tmp = NULL;
		char *uid = NULL;
		u32 res;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		while (*line == ' ' || *line == '\t' || *line == '\r')
			line++;
		if (!*line)
			continue;

		tmp = line;
		package = strsep(&tmp, delim);
		while (tmp && (*tmp == ' ' || *tmp == '\t'))
			tmp++;
		uid = strsep(&tmp, delim);
		if (!uid || !package || !*package ||
		    strlen(package) >= KSU_MAX_PACKAGE_NAME) {
			pr_err("update_uid: package or uid is NULL!\n");
			goto out;
		}

		if (kstrtou32(uid, 10, &res)) {
			pr_err("track_throne: appid parse err\n");
			goto out;
		}

		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data) {
			pr_err("track_throne: OOM appid=%u\n", res);
			goto out;
		}
		data->appid = res;
		strscpy(data->package, package, KSU_MAX_PACKAGE_NAME);
		list_add_tail(&data->list, &uid_list);
	}
	kfree(buf);
	buf = NULL;

	if (list_empty(&uid_list))
		goto out;
	if (prune_only)
		goto prune;

	/* For each manager slot, clear it if that appid is no longer in
	 * packages.list */
	{
		int i;
		for (i = 0; i < KSU_MAX_MANAGER_KEYS; i++) {
			uid_t aid = ksu_manager_appids[i];
			bool slot_still_exists = false;

			if (aid == KSU_INVALID_UID)
				continue;
			list_for_each_entry (np, &uid_list, list) {
				if (np->appid == aid) {
					slot_still_exists = true;
					break;
				}
			}
			if (!slot_still_exists) {
				pr_info("Manager slot %d (appid=%d) removed, "
					"clearing\n",
					i, aid);
				ksu_manager_appids[i] = KSU_INVALID_UID;
				locked_manager_appids[i] = KSU_INVALID_UID;
			}
		}
		update_primary_manager();
		manager_exist = (ksu_manager_appid != KSU_INVALID_UID);
		if (!manager_exist) {
#ifdef CONFIG_KSU_SUPERKEY
			extern void ksu_superkey_register_prctl_hook(void);
			ksu_superkey_register_prctl_hook();
#endif // #ifdef CONFIG_KSU_SUPERKEY
		}
	}

	need_search = !manager_exist || manager_scan_forced;
	if (manager_scan_forced)
		clear_apk_scan_cache();
	manager_scan_forced = false;

	if (need_search) {
		pr_info("Searching for manager(s)...\n");
		search_manager("/data/app", 2, &uid_list);
		pr_info("Manager search finished\n");
	}

prune:
	// then prune the allowlist
	ksu_prune_allowlist(is_uid_exist, &uid_list);
out:
	kfree(buf);
	// free uid_list
	list_for_each_entry_safe (np, n, &uid_list, list) {
		list_del(&np->list);
		kfree(np);
	}
	revert_creds(old_cred);
}

void track_throne(bool prune_only)
{
	mutex_lock(&throne_scan_lock);
	track_throne_locked(prune_only);
	mutex_unlock(&throne_scan_lock);
}

/* Shared by package events and the initial search after a late load. */
static struct delayed_work throne_search_work;
static DEFINE_SPINLOCK(throne_work_lock);
static bool throne_work_stopping = true;

static void do_throne_search(struct work_struct *work)
{
	pr_info("throne_tracker: delayed search for manager...\n");
	track_throne(false);
}

void ksu_schedule_manager_scan(void)
{
	unsigned long flags;

	/* Package notifications may run with VFS directory locks held. Do not
	 * wait for throne_scan_lock or perform filesystem I/O in that context.
	 * Coalesce replacement events before reading the completed list. */
	spin_lock_irqsave(&throne_work_lock, flags);
	if (!throne_work_stopping)
		mod_delayed_work(system_wq, &throne_search_work,
				 msecs_to_jiffies(100));
	spin_unlock_irqrestore(&throne_work_lock, flags);
}

void ksu_request_manager_rescan(void)
{
	mutex_lock(&throne_scan_lock);
	manager_scan_forced = true;
	track_throne_locked(false);
	mutex_unlock(&throne_scan_lock);
}

void ksu_throne_tracker_init(void)
{
	unsigned long flags;

	INIT_DELAYED_WORK(&throne_search_work, do_throne_search);
	spin_lock_irqsave(&throne_work_lock, flags);
	throne_work_stopping = false;
	schedule_delayed_work(&throne_search_work, msecs_to_jiffies(3000));
	spin_unlock_irqrestore(&throne_work_lock, flags);
	pr_info("throne_tracker: init, scheduled manager search in 3s\n");
}

void ksu_throne_tracker_exit(void)
{
	unsigned long flags;

	/* Serialize the last possible callback's queue operation with teardown,
	 * then wait outside the spinlock for any running scan to finish. */
	spin_lock_irqsave(&throne_work_lock, flags);
	throne_work_stopping = true;
	spin_unlock_irqrestore(&throne_work_lock, flags);
	cancel_delayed_work_sync(&throne_search_work);
	mutex_lock(&throne_scan_lock);
	clear_apk_scan_cache();
	mutex_unlock(&throne_scan_lock);
	pr_info("throne_tracker: exit\n");
}
