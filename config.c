/*	$Id$ */
/*
 * Copyright (c) 2015 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_FPARSELN
#include <util.h>
#endif
#include <unistd.h>

#include "extern.h"

static int
set(char **p, const char *val)
{

	free(*p);
	if (NULL != (*p = strdup(val)))
		return(1);
	kerr(NULL);
	return(0);
}

static int
config_defaults(const char *file, struct config *cfg)
{

	if (NULL == cfg->displayname)
		if ( ! set(&cfg->displayname, ""))
			return(-1);

	return(1);
}

static int
priv(struct config *cfg, const struct prncpl *prncpl,
	const char *file, size_t line, const char *val)
{
	const char	*name;
	size_t		 i, namesz;
	unsigned int	 perms;
	void		*pp;

	if ('\0' == *val) {
		kerrx("%s:%zu: empty privilege", file, line);
		return(0);
	}

	name = val;
	while ('\0' != *val && ! isspace((int)*val))
		val++;

	if (0 == (namesz = val - name)) {
		kerrx("%s:%zu: empty privilege", file, line);
		return(0);
	}

	perms = PERMS_NONE;

	while ('\0' != *val) {
		while (isspace((int)*val))
			val++;
		if (0 == strncasecmp(val, "READ", 4)) {
			perms |= PERMS_READ;
			val += 4;
			continue;
		} else if (0 == strncasecmp(val, "WRITE", 5)) {
			perms |= PERMS_WRITE;
			val += 5;
			continue;
		} else if (0 == strncasecmp(val, "NONE", 4)) {
			perms = PERMS_NONE;
			break;
		} else if (0 == strncasecmp(val, "DELETE", 6)) {
			perms |= PERMS_DELETE;
			val += 6;
			continue;
		} else if (0 == strncasecmp(val, "ALL", 3)) {
			perms = PERMS_ALL;
			val += 3;
			continue;
		} 
		kerrx("%s:%zu: bad priv token", file, line);
		return(0);
	}

	for (i = 0; i < cfg->privsz; i++) {
		if (namesz != strlen(cfg->privs[i].prncpl))
			continue;
		else if (strncmp(cfg->privs[i].prncpl, name, namesz))
			continue;
		kerrx("%s:%zu: duplicate principal", file, line);
		return(0);
	}

	pp = reallocarray(cfg->privs, 
		cfg->privsz + 1, sizeof(struct priv));

	if (NULL == pp) {
		kerr(NULL);
		return(0);
	}

	cfg->privs = pp;
	cfg->privs[cfg->privsz].prncpl = malloc(namesz + 1);
	if (NULL == cfg->privs[cfg->privsz].prncpl) {
		kerr(NULL);
		return(0);
	}
	memcpy(cfg->privs[cfg->privsz].prncpl, name, namesz);
	cfg->privs[cfg->privsz].prncpl[namesz] = '\0';
	cfg->privs[cfg->privsz].perms = perms;
	cfg->privsz++;

	if (namesz != strlen(prncpl->name))
		return(1);
	else if (strncmp(prncpl->name, name, namesz))
		return(1);

	cfg->perms = perms;
	return(1);
}

int
config_replace(const char *file, const struct config *p)
{
	int	 	 fd, rc;
	size_t		 i;
	FILE		*f;

	if (-1 == (fd = open_lock_ex(file, O_RDWR|O_TRUNC, 0)))
		return(0);
	else if (NULL == (f = fdopen_lock(file, fd, "a"))) 
		return(0);
	
	i = 0;

	rc = fprintf(f, "displayname = \"%s\"\n", p->displayname);
	if (-1 == rc) {
		kerr("%s: fprintf", file);
		goto out;
	}

	if (NULL != p->colour) {
		rc = fprintf(f, "colour = \"%s\"\n", p->colour);
		if (-1 == rc) {
			kerr("%s: fprintf", file);
			goto out;
		}
	}

	for ( ; i < p->privsz; i++) {
		rc = fprintf(f, "privilege = %s", p->privs[i].prncpl);
		if (-1 == rc) {
			kerr("%s: fprintf", file);
			break;
		} else if (PERMS_NONE == p->privs[i].perms) {
			if (-1 != fprintf(f, " NONE\n")) 
				continue;
			kerr("%s: fprintf", file);
			break;
		} else if (PERMS_ALL == p->privs[i].perms) {
			if (-1 != fprintf(f, " ALL\n"))
				continue;
			kerr("%s: fprintf", file);
			break;
		}
		if (PERMS_READ & p->privs[i].perms)
			if (-1 == fprintf(f, " READ")) {
				kerr("%s: fprintf", file);
				break;
			}
		if (PERMS_WRITE & p->privs[i].perms)
			if (-1 == fprintf(f, " WRITE")) {
				kerr("%s: fprintf", file);
				break;
			}
		if (PERMS_DELETE & p->privs[i].perms)
			if (-1 == fprintf(f, " DELETE")) {
				kerr("%s: fprintf", file);
				break;
			}
		if (-1 == fprintf(f, "\n")) {
			kerr("%s: fprintf", file);
			break;
		}
	}

out:
	fclose_unlock(file, f, fd);

	if (i < p->privsz)
		kerrx("%s: WARNING: FILE IN "
			"INCONSISTENT STATE\n", file);
	return(i == p->privsz);
}

/*
 * Parse our configuration file.
 * Return 0 on parse failure or file-not-found.
 * Return <0 on memory allocation failure.
 * Return >0 otherwise.
 */
int
config_parse(const char *file, 
	struct config **pp, const struct prncpl *prncpl)
{
	FILE		*f;
	size_t		 len, line;
	char		*key, *val, *cp, *end;
	int		 rc, fl, fd;
	long long	 bytesused, bytesavail;

	if (-1 == (fd = open_lock_sh(file, O_RDONLY, 0600)))
		return(0);

	if ( ! quota(file, fd, &bytesused, &bytesavail)) {
		kerrx("%s: quota failure", file);
		close_unlock(file, fd);
		return(0);
	} else if (NULL == (f = fdopen_lock(file, fd, "r"))) 
		return(0);

	line = 0;
	cp = NULL;
	if (NULL == (*pp = calloc(1, sizeof(struct config)))) {
		kerr(NULL);
		fclose_unlock(file, f, fd);
		return(-1);
	}

	(*pp)->writable = -1 != access(file, W_OK);
	(*pp)->bytesused = bytesused;
	(*pp)->bytesavail = bytesavail;

	rc = 1;
	fl = FPARSELN_UNESCALL;
	while (NULL != (cp = fparseln(f, &len, &line, NULL, fl))) {
		/* Strip trailing whitespace. */
		while (len > 0 && isspace((int)cp[len - 1]))
			cp[--len] = '\0';

		/* Strip leading whitespace. */
		for (key = cp; isspace((int)*key); key++)
			/* Spin. */ ;

		/* Skip blank lines. */
		if ('\0' == *key) {
			free(cp);
			continue;
		}

		if (NULL == (val = end = strchr(key, '='))) {
			kerrx("%s:%zu: no \"=\"", file, line + 1);
			rc = 0;
			break;
		}
		*val++ = '\0';

		/* Ignore space before equal sign. */
		for (--end; end > key && isspace((int)*end); end--)
			*end = '\0';

		/* Ignore space after equal sign. */
		while (isspace((int)*val))
			val++;

		if (0 == strcmp(key, "privilege"))
			rc = priv(*pp, prncpl, file, line + 1, val);
		else if (0 == strcmp(key, "displayname"))
			rc = set(&(*pp)->displayname, val);
		else if (0 == strcmp(key, "colour"))
			rc = set(&(*pp)->colour, val);
		else
			rc = 1;

		if (rc <= 0) 
			break;

		free(cp);
		cp = NULL;
	}

	/* 
	 * Error-check.
	 * This is confusing between fparseln() doesn't tell us what's
	 * really wrong.
	 * We should be at EOF, so check that.
	 */
	if ( ! feof(f)) {
		if (rc > 0)
			kerr("%s: fparseln", file);
		else
			kerrx("%s: fail parse", file);
		fclose_unlock(file, f, fd);
		free(cp);
		config_free(*pp);
		*pp = NULL;
		/* Elevate to -1 if fparseln() errors occur. */
		return(rc > 1 ? -1 : rc);
	}

	assert(rc > 0);
	free(cp);
	fclose_unlock(file, f, fd);

	if ((rc = config_defaults(file, *pp)) <= 0) {
		config_free(*pp);
		*pp = NULL;
	}

	return(rc);
}

void
config_free(struct config *p)
{
	size_t	 i;

	if (NULL == p)
		return;

	for (i = 0; i < p->privsz; i++) 
		free(p->privs[i].prncpl);

	free(p->privs);
	free(p->displayname);
	free(p->colour);
	free(p);
}
