#ifndef __KSU_SUCOMPAT_MODULE_GUARD_H
#define __KSU_SUCOMPAT_MODULE_GUARD_H

int ksu_sucompat_module_guard_init(void);
void ksu_sucompat_module_guard_exit(void);
int ksu_sucompat_module_guard_acquire(void);
void ksu_sucompat_module_guard_release(void);

#endif
