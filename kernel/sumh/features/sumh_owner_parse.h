#ifndef SUMH_OWNER_PARSE_H
#define SUMH_OWNER_PARSE_H

#define SUMH_OWNER_NAME_MAX 256

struct sumh_owner_package {
	unsigned int appid;
	char name[SUMH_OWNER_NAME_MAX];
};

/* Parse the first two packages.list fields without accepting partial lines. */
static inline bool sumh_owner_parse_line(const char *line, size_t length,
					 struct sumh_owner_package *out)
{
	size_t at = 0, n = 0;
	unsigned int uid = 0, digits = 0;

	while (at < length && (line[at] == ' ' || line[at] == '\t'))
		at++;
	while (at < length && line[at] != ' ' && line[at] != '\t') {
		char c = line[at++];

		if (n + 1 >= sizeof(out->name) ||
		    !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '.'))
			return false;
		out->name[n++] = c;
	}
	if (!n)
		return false;
	out->name[n] = '\0';
	while (at < length && (line[at] == ' ' || line[at] == '\t'))
		at++;
	while (at < length && line[at] >= '0' && line[at] <= '9') {
		if (uid > 429496729U || (uid == 429496729U && line[at] > '5'))
			return false;
		uid = uid * 10 + (unsigned int)(line[at++] - '0');
		digits++;
	}
	if (!digits || at == length || (line[at] != ' ' && line[at] != '\t'))
		return false;
	out->appid = uid;
	return true;
}

#endif
