/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h> /* fopen, getline */
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "libkcaldav.h"
#include "db.h"
#include "server.h"

/*
 * Parse a key-value pair.  Return zero on nothing parsed (empty line),
 * -1 on failure, 1 on success where "key" and "val" are assigned
 *  NUL-terminated values.
 */
static int
conf_parse_keyval(char *buf, const char **key, const char **val)
{
	char	*cp;

	while (*buf != '\0' && isspace((unsigned char)*buf))
		buf++;
	*key = buf;
	while (*buf != '\0' && *buf != '=')
		buf++;
	if (*buf == '\0' || buf == *key)
		return -1;

	/* Strip from before and after delim. */

	assert(buf > *key && *buf == '=');
	for (cp = &buf[-1];
	     cp > *key && isspace((unsigned char)*cp); cp--)
		if (isspace((unsigned char)*cp))
			*cp = '\0';

	*buf++ = '\0';
	while (*buf != '\0' && isspace((unsigned char)*buf))
		buf++;

	*val = buf;
	return **val != '\0';
}

/*
 * Optionally read our configuration.  This is in a regular
 * configuration file format and has only two possible key-value pairs:
 *
 *  logfile=/path/to/logfile
 *  verbose=[0--3]
 *
 * If the configuration file does not exist, do not enact any processing
 * and accept this as not an error.
 *
 * Return -1 on system error (exit with err), 0 on soft error (exit with errx),
 * 1 on success.
 */
int
conf_read(const char *fn, struct conf *conf)
{
	const char	*key, *val, *er;
	FILE		*f = NULL;
	char		*line = NULL;
	size_t		 linesize = 0, i;
	ssize_t		 linelen;
	int		 rc = 0, rrc;

	memset(conf, 0, sizeof(struct conf));

	/*
	 * Deprecated compile-time constants.  Allow these to be overriden by
	 * the configuration file.
	 */

#ifdef LOGFILE
#warning "LOGFILE" is deprecated: use "CFGFILE" instead.
	if ((conf->logfile = strdup(LOGFILE)) == NULL)
		return -1;
#endif
#if defined(DEBUG) && DEBUG >= 0
#warning "DEBUG" is deprecated: use "CFGFILE" instead.
	conf->verbose = DEBUG;
#endif

	if (fn != NULL && fn[0] != '\0')
		if ((f = fopen(fn, "r")) == NULL && errno != ENOENT)
			return -1;

	if (f == NULL)
		return 1;

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		/*
		 * Strip everything following the first
		 * unescaped hash, un-escaping along the way.
		 */
		for (i = 0; line[i] != '\0'; ) {
			if (line[i] != '#') {
				i++;
				continue;
			}
			if (i == 0 || line[i - 1] != '\\') {
				line[i] = '\0';
				linelen = i;
				break;
			}
			assert(i > 0);
			assert(line[i - 1] == '\\');
			/*
			 * Shift the buffer left, including the
			 * trailing NUL terminator.
			 */
			memmove(&line[i - 1], &line[i], linelen - i);
		}

		/* Strip trailing white-space. */

		while (linelen > 0 &&
		    isspace((unsigned char)line[linelen - 1]))
			line[--linelen] = '\0';

		if (linelen == 0)
			continue;

		/* Parse key-value pairs. */

		if ((rrc = conf_parse_keyval(line, &key, &val)) == -1)
			break;
		else if (rrc == 0)
			continue;

		if (strcmp(key, "logfile") == 0) {
			free(conf->logfile);
			if ((conf->logfile = strdup(val)) == NULL)
				return -1;
		} else if (strcmp(key, "debug") == 0) {
			conf->verbose = strtonum(val, 0, 10, &er);
			if (er != NULL)
				break;
		} else
			break;
	}

	free(line);
	if (!ferror(f) && fclose(f) != EOF)
		rc = 1;

	return rc;
}
