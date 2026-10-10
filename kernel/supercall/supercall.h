#ifndef __KSU_H_SUPERCALL
#define __KSU_H_SUPERCALL

#include <linux/fs.h>

#include "uapi/supercall.h"

int ksu_install_fd(void);
int ksu_install_su_fd(void);
bool ksu_is_su_session_fd(const struct file *filp);
void ksu_supercalls_init(void);
void ksu_supercalls_exit(void);

#ifdef CONFIG_KSU_SUPERKEY
void ksu_superkey_unregister_prctl_hook(void);
void ksu_superkey_register_prctl_hook(void);
#endif // #ifdef CONFIG_KSU_SUPERKEY

#endif // #ifndef __KSU_H_SUPERCALL
