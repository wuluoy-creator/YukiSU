#ifndef SUMH_VFS_VIEW_H
#define SUMH_VFS_VIEW_H

#include <linux/types.h>

struct sumh_xattr_sb_entry;

int sumh_vfs_view_init(void);
void sumh_vfs_view_stop(void);
void sumh_vfs_view_drain(void);
void sumh_vfs_view_exit(void);
bool sumh_overlay_xattr_available(void);
bool sumh_statfs_view_available(void);
int sumh_overlay_xattr_mark(const char *path);
void sumh_overlay_xattr_retire(struct sumh_xattr_sb_entry *entry);

#endif
