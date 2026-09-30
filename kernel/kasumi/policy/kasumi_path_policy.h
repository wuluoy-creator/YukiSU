#ifndef _KASUMI_PATH_POLICY_H
#define _KASUMI_PATH_POLICY_H

#include <linux/list.h>
#include <linux/path.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <uapi/linux/magic.h>

struct kasumi_entry;
struct inode;

struct kasumi_rule_source {
	struct path path;
	struct kstat stat;
	umode_t source_mode;
	unsigned long visible_ino;
	unsigned long visible_dev;
	bool stat_valid;
	bool preserve_visible_metadata;
	int error;
};

void kasumi_project_visible_stat(struct kstat *result,
				 const struct kstat *source_stat,
				 const struct kstat *visible_template,
				 bool preserve_visible_metadata,
				 unsigned long visible_ino,
				 unsigned long visible_dev);

enum kasumi_policy_scope {
	KASUMI_POLICY_SCOPE_NONE = 0,
	KASUMI_POLICY_SCOPE_VIEW,
	KASUMI_POLICY_SCOPE_SPOOF,
};

bool kasumi_is_privileged_process(void);
bool kasumi_policy_current_is_isolated(void);
enum kasumi_policy_scope kasumi_policy_current_scope(void);
bool kasumi_policy_current_is_view_target(void);
bool kasumi_hide_storage_parent(const struct inode *parent);
bool kasumi_policy_current_is_hide_target(const struct inode *parent);
bool kasumi_policy_current_is_spoof_target(void);
bool kasumi_policy_uid_is_spoof_target(uid_t uid);
bool kasumi_rule_get_source_flags(const char *pathname,
				  unsigned int lookup_flags,
				  struct kasumi_rule_source *source);
bool kasumi_should_hide(const char *pathname);

/*
 * Slice 4c-v2 pure-virtual directory topology: resolve names under a synthetic
 * (source-less) directory against the rule table.  A F_VIRTUAL_DIR vnode uses
 * these to decide whether a child name is an exact redirect (a leaf) or the
 * prefix of one (a deeper virtual dir), and to enumerate its children.
 */
enum kasumi_vpath_kind {
	KASUMI_VPATH_NONE = 0, /* no rule under dir/child */
	KASUMI_VPATH_LEAF, /* dir/child is an exact rule -> a real redirect */
	KASUMI_VPATH_VDIR, /* dir/child is a prefix of some rule -> virtual dir
			    */
};

/*
 * Resolve @child within virtual directory @dir.  On KASUMI_VPATH_LEAF pins
 * *leaf_src (caller path_put) and fills *leaf_mode / *leaf_ino with the
 * leaf's source identity (the nofollow link for a symlink target). Non-sleeping
 * apart from a GFP_KERNEL scratch alloc; the rule-table scan runs under RCU.
 */
int kasumi_rule_vpath_child(const char *dir, const char *child,
			    struct path *leaf_src, umode_t *leaf_mode,
			    unsigned long *leaf_ino);

/*
 * Collect the direct children of virtual directory @dir into @out as
 * struct kasumi_name_list nodes (name/ino/type), deduplicated.  Caller emits
 * and frees each node (kfree(name) then kfree(node)).  Returns the child count.
 */
int kasumi_rule_vpath_emit(const char *dir, struct list_head *out);

#endif /* _KASUMI_PATH_POLICY_H */
