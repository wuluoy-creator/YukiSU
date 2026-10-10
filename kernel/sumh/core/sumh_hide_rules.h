#ifndef _SUMH_HIDE_RULES_H
#define _SUMH_HIDE_RULES_H

#include <linux/mutex.h>
#include <linux/types.h>

extern struct mutex sumh_mutation_mutex;

bool sumh_hide_rules_available(void);
long sumh_hide_rules_ioctl(unsigned int cmd, void __user *arg);
void sumh_hide_rules_clear(void);
void sumh_hide_rules_changed(void);
bool sumh_hide_rules_resolving(void);
/* Called with mutation_mutex held. Drops it while draining the resolver. */
void sumh_hide_rules_stop(void);
bool sumh_hide_rules_quiesce(void);

#endif
