#ifndef SUMH_PROC_READ_HOOKS_H
#define SUMH_PROC_READ_HOOKS_H

int sumh_proc_proxy_get(void);
void sumh_proc_proxy_put(void);
void sumh_proc_read_hooks_init(void);
void sumh_proc_read_hooks_stop_new(void);
void sumh_proc_read_hooks_exit(void);

#endif
