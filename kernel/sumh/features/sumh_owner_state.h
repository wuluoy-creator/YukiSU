#ifndef SUMH_OWNER_STATE_H
#define SUMH_OWNER_STATE_H

/* Shared with the host test; zero denotes an unresolved owner. */
struct sumh_owner_state {
	unsigned int uid;
	unsigned int generation;
	bool inherited;
	bool ambiguous;
	bool frozen;
	bool inferred;
};

static inline void sumh_owner_inherit(struct sumh_owner_state *child,
				      const struct sumh_owner_state *parent,
				      unsigned int uid)
{
	unsigned int appid = uid % 100000;

	if (appid == 1053) {
		child->ambiguous = true;
	} else if (parent &&
		   (parent->uid || parent->frozen || parent->ambiguous)) {
		*child = *parent;
		if (child->uid)
			child->inherited = true;
	} else if (appid >= 10000 && appid < 20000) {
		child->uid = uid;
		child->inherited = true;
	}
}

static inline void sumh_owner_candidate(struct sumh_owner_state *state,
					unsigned int uid,
					unsigned int generation)
{
	if (!uid || state->frozen || state->inherited || state->ambiguous)
		return;
	if (state->uid && state->uid != uid) {
		state->uid = 0;
		state->ambiguous = true;
	} else {
		state->uid = uid;
		state->generation = generation;
		state->inferred = true;
	}
}

static inline unsigned int sumh_owner_bind(struct sumh_owner_state *state,
					   unsigned int generation)
{
	if (!state->frozen && state->inferred &&
	    state->generation != generation) {
		state->uid = 0;
		state->ambiguous = true;
	}
	state->frozen = true;
	return state->uid;
}

#endif
