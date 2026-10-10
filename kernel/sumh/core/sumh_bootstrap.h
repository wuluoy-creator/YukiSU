#ifndef _SUMH_BOOTSTRAP_H
#define _SUMH_BOOTSTRAP_H

#include <linux/types.h>

bool sumh_is_ready(void);
int sumh_control_begin(void);
void sumh_control_end(void);
void ksu_sumh_init(void);
void ksu_sumh_post_fs_data(void);
void ksu_sumh_exit(void);

#endif /* _SUMH_BOOTSTRAP_H */
