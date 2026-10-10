#ifndef _SUMH_OVERLAY_H
#define _SUMH_OVERLAY_H

#include <linux/fs.h>
#include <linux/list.h>

void sumh_add_inject_rule(char *dir);
int sumh_check_merge_target(const char *target);
int sumh_materialize_merge(const char *src_prefix, const char *target_dir,
			   int depth);
bool sumh_is_merge_context(const struct dir_context *ctx);
void sumh_populate_injected_list(const char *dir_path, struct dentry *parent,
				 struct list_head *head);

#endif /* _SUMH_OVERLAY_H */
