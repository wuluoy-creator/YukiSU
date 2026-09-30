#ifndef _KASUMI_BOOTSTRAP_H
#define _KASUMI_BOOTSTRAP_H

#include <linux/types.h>

bool kasumi_is_ready(void);
int kasumi_control_begin(void);
void kasumi_control_end(void);
void ksu_kasumi_init(void);
void ksu_kasumi_post_fs_data(void);
void ksu_kasumi_exit(void);

#endif /* _KASUMI_BOOTSTRAP_H */
