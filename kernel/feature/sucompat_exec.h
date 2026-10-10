#ifndef __KSU_H_SUCOMPAT_EXEC
#define __KSU_H_SUCOMPAT_EXEC

#include <linux/types.h>

struct file;
struct inode;

int ksu_sucompat_exec_init(void);
void ksu_sucompat_exec_exit(void);
bool ksu_sucompat_exec_ready(void);

int ksu_sucompat_exec_file_open(struct file *file);
int ksu_sucompat_exec_file_release(struct inode *inode, struct file *file);

#endif // __KSU_H_SUCOMPAT_EXEC
