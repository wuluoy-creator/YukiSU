#ifndef __KSU_H_SUCOMPAT
#define __KSU_H_SUCOMPAT
#include <linux/types.h>

struct pt_regs;

extern const bool ksu_su_compat_enabled;

bool ksu_sucompat_exec_enabled(void);

void ksu_sucompat_init(void);
void ksu_sucompat_exit(void);
int ksu_sucompat_sumh_init(void);
void ksu_sucompat_sumh_exit(void);

// Handler functions exported for hook_manager
long ksu_handle_faccessat_sucompat(int orig_nr, const struct pt_regs *regs);
long ksu_handle_stat_sucompat(int orig_nr, const struct pt_regs *regs);
long ksu_handle_execve_sucompat(const char __user **filename_user, int orig_nr,
				const struct pt_regs *regs);
long ksu_handle_execveat_sucompat(const char __user **filename_user,
				  int orig_nr, const struct pt_regs *regs);

#endif // #ifndef __KSU_H_SUCOMPAT
