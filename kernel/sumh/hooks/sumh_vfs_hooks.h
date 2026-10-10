#ifndef _SUMH_VFS_HOOKS_H
#define _SUMH_VFS_HOOKS_H

struct dir_context;
struct file;
struct sumh_filldir_wrapper;

struct sumh_filldir_wrapper *
sumh_iterate_prepare_wrapper(struct file *file, struct dir_context *orig_ctx);
void sumh_iterate_finish_wrapper(struct sumh_filldir_wrapper *wrapper);

#endif
