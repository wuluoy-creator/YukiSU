#ifndef SUMH_OWNER_H
#define SUMH_OWNER_H

#include <linux/fs.h>
#include <linux/types.h>

int sumh_owner_init(void);
void sumh_owner_exit(void);
bool sumh_owner_available(void);
void sumh_owner_observe(struct file *file);
uid_t sumh_owner_freeze(void);

#endif
