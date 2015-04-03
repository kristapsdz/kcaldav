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

#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif

#include "extern.h"
#include "md5.h"

int
main(int argc, char *argv[])
{
	int	 	 c, fd, newfd, rc;
	char	 	 buf[PATH_MAX];
	const char	*file, *pname, *auser;
	size_t		 sz, len, line;
	struct pentry	 entry;
	char		*cp;
	FILE		*f;

	setlinebuf(stderr);

	if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;

	sz = strlcpy(buf, CALDIR, sizeof(buf));
	if (sz >= sizeof(buf)) {
		kerrx("%s: path too long", buf);
		return(EXIT_FAILURE);
	} else if ('/' == buf[sz - 1])
		buf[sz - 1] = '\0';

	sz = strlcat(buf, "/kcaldav.passwd", sizeof(buf));
	if (sz >= sizeof(buf)) {
		kerrx("%s: path too long", buf);
		return(EXIT_FAILURE);
	}

	file = buf;

	while (-1 != (c = getopt(argc, argv, "f:"))) 
		switch (c) {
		case ('f'):
			file = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;
	if (0 == argc) 
		goto usage;

	auser = argv[0];

	fd = open_lock_sh(file, O_RDONLY, 0600);
	if (-1 == fd) 
		return(EXIT_FAILURE);

	/* 
	 * Make a copy of our file descriptor because fclose() will
	 * close() the one that we currently have.
	 */
	if (-1 == (newfd = dup(fd))) {
		kerr("%s: dup", file);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	} else if (NULL == (f = fdopen(newfd, "r"))) {
		kerr("%s: fopen", file);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	}

	rc = -2;
	line = 0;
	while (NULL != (cp = fgetln(f, &len))) {
		if ( ! prncpl_line(cp, len, file, ++line, &entry)) {
			rc = 0;
			explicit_bzero(cp, len);
			continue;
		}

		/* Is this the user we're looking for? */
		if (0 == strcmp(entry.user, auser)) {
			explicit_bzero(cp, len);
			rc = 1;
			break;
		}

		explicit_bzero(cp, len);
	}

	/* If we had errors, bail out. */
	if ( ! feof(f) && rc <= 0) {
		if (-2 == rc)
			kerr("%s: fgetln", file);
		if (-1 == fclose(f))
			kerr("%s: fclose", file);
		fclose(f);
		close_unlock(file, fd);
		return(rc < 0 ? -1 : rc);
	} else if (-1 == fclose(f))
		kerr("%s: fclose", file);

	close_unlock(file, fd);

	if (1 != rc)
		fprintf(stderr, "%s: no such user\n", auser);

	return(1 == rc ? EXIT_SUCCESS : EXIT_FAILURE);

usage:
	fprintf(stderr, "usage: %s [-f passwd] user\n", pname);
	return(EXIT_FAILURE);
}
