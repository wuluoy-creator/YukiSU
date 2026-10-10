#ifndef SUMH_FOP_BRIDGE_H
#define SUMH_FOP_BRIDGE_H

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/types.h>

struct sumh_fop_bridge_entry {
	const struct file_operations *ingress;
	const struct file_operations *live;
	const struct file_operations *orig;
	struct hlist_node node;
	bool registered;
};

int sumh_fop_bridge_init(void);
void sumh_fop_bridge_stop_new(void);
void sumh_fop_bridge_exit(void);
bool sumh_fop_bridge_register(struct sumh_fop_bridge_entry *entry,
			      const struct file_operations *ingress,
			      const struct file_operations *live,
			      const struct file_operations *orig);
void sumh_fop_bridge_unregister(struct sumh_fop_bridge_entry *entry);

#endif /* SUMH_FOP_BRIDGE_H */
