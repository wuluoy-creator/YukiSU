#ifndef _SUMH_HIDE_EVENTS_H
#define _SUMH_HIDE_EVENTS_H

#include <linux/cred.h>
#include <linux/path.h>
#include <linux/poll.h>
#include <linux/wait.h>

struct sumh_hide_events {
	struct path root;
	const struct cred *cred;
	struct file *mountinfo;
	struct fsnotify_group *directories;
	poll_table table;
	wait_queue_entry_t wait;
	wait_queue_head_t *head;
	void (*notify)(void *data);
	void *data;
	bool stopping;
	int error;
};

struct sumh_hide_watch;
int sumh_hide_events_watch_path(struct sumh_hide_events *events,
				const char *parent, const char *leaf,
				struct path *resolved,
				struct sumh_hide_watch **watches);
void sumh_hide_events_unwatch(struct sumh_hide_watch *watches);

int sumh_hide_events_open(struct sumh_hide_events *events,
			  void (*notify)(void *), void *data);
int sumh_hide_events_ack(struct sumh_hide_events *events);
int sumh_hide_events_resolve(struct sumh_hide_events *events, const char *name,
			     struct path *path);
void sumh_hide_events_stop(struct sumh_hide_events *events);
/* The caller drains its resolver before closing the scope. */
void sumh_hide_events_close(struct sumh_hide_events *events);

#endif
