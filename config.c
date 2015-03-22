#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "extern.h"

static int
xstrlcat(char *file, const char *p, size_t sz)
{

	if (strlcat(file, p, sz) < sz)
		return(1);
	fprintf(stderr, "%s: name too long\n", file);
	return(0);
}

static int
config_set(char **p, const char *val)
{

	free(*p);
	if (NULL != (*p = strdup(val)))
		return(1);
	perror(NULL);
	return(0);
}

static int
config_defaults(const char *path, struct config *cfg)
{
	char		 file[PATH_MAX];
	size_t		 sz;

	file[0] = '\0';

	if ( ! xstrlcat(file, path, sizeof(file)))
		return(0);

	sz = strlen(path);
	if (0 == sz || '/' != path[sz - 1]) 
		if ( ! xstrlcat(file, "/", sizeof(file)))
			return(0);

	if ( ! xstrlcat(file, "calendars", sizeof(file)))
		return(0);

	if (NULL == cfg->calendarhome)
		if ( ! config_set(&cfg->calendarhome, file))
			return(0);
	if (NULL == cfg->calendaruseraddress)
		if ( ! config_set(&cfg->calendaruseraddress, "mailto:foo@example.com"))
			return(0);
	if (NULL == cfg->displayname)
		if ( ! config_set(&cfg->displayname, ""))
			return(0);
	if (NULL == cfg->emailaddress)
		if ( ! config_set(&cfg->emailaddress, "foo@example.com"))
			return(0);

	return(1);
}

struct config *
config_parse(const char *root, const char *path)
{
	FILE		*f;
	char		 file[PATH_MAX];
	size_t		 sz, len, line;
	char		*key, *val, *cp;
	int		 rc;
	struct config	*cfg;

	file[0] = '\0';
	if ( ! xstrlcat(file, path, sizeof(file)))
		return(NULL);

	sz = strlen(path);
	if (0 == sz || '/' != path[sz - 1]) 
		if ( ! xstrlcat(file, "/", sizeof(file)))
			return(NULL);

	if ( ! xstrlcat(file, "kcaldav.conf", sizeof(file)))
		return(NULL);

	if (NULL == (f = fopen(file, "r"))) {
		perror(file);
		return(NULL);
	}

	line = 0;
	cp = NULL;
	if (NULL == (cfg = calloc(1, sizeof(struct config)))) {
		perror(NULL);
		fclose(f);
		return(NULL);
	}

	while (NULL != (cp = fparseln(f, &len, &line, NULL, 0))) {
		key = cp;
		if (NULL == (val = strchr(key, '='))) {
			fprintf(stderr, "%s:%zu: no "
				"\"=\"", file, line + 1);
			break;
		}
		*val++ = '\0';
		if (0 == strcmp(key, "calendar-home-set"))
			rc = config_set(&cfg->calendarhome, val);
		else if (0 == strcmp(key, "calendar-user-address-set"))
			rc = config_set(&cfg->calendaruseraddress, val);
		else if (0 == strcmp(key, "displayname"))
			rc = config_set(&cfg->displayname, val);
		else if (0 == strcmp(key, "email-address-set"))
			rc = config_set(&cfg->emailaddress, val);
		else
			rc = 1;

		if (0 == rc) 
			break;

		free(cp);
		cp = NULL;
	}

	free(cp);
	fclose(f);
	if (NULL != cp || ! config_defaults(root, cfg)) {
		config_free(cfg);
		cfg = NULL;
	}
	return(cfg);
}

void
config_free(struct config *p)
{

	if (NULL == p)
		return;

	free(p->calendarhome);
	free(p->calendaruseraddress);
	free(p->displayname);
	free(p->emailaddress);
	free(p);
}
