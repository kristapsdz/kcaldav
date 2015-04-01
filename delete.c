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
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "main.h"

void
method_delete(struct kreq *r)
{
	struct state	*st = r->arg;
	DIR		*dirp;
	struct dirent	*dp;
	struct ical	*cur;
	const char	*digest;
	int		 errs, fd;
	size_t		 sz;
	char		 buf[PATH_MAX];

	if ( ! (PERMS_DELETE & st->cfg->perms)) {
		fprintf(stderr, "%s: principal does not "
			"have delete acccess\n", st->path);
		http_error(r, KHTTP_403);
		return;
	} 

	digest = NULL;
	if (NULL != r->reqmap[KREQU_IF_MATCH]) {
		digest = r->reqmap[KREQU_IF_MATCH]->val;
		if (32 != (sz = strlen(digest))) {
			fprintf(stderr, "%s: \"If-Match\" digest "
				"invalid: %zuB\n", st->path, sz);
			http_error(r, KHTTP_400);
			return;
		}
	}

	if (NULL != digest && ! st->isdir) {
		cur = ical_parsefile_open(st->path, &fd);
		if (NULL == cur) {
			fprintf(stderr, "%s: fail open\n", st->path);
			http_error(r, KHTTP_404);
		} else if (strcmp(cur->digest, digest)) {
			fprintf(stderr, "%s: fail digest\n", st->path);
			http_error(r, KHTTP_412);
		} else if (-1 == unlink(st->path)) {
			perror(st->path);
			fprintf(stderr, "%s: fail unlink\n", st->path);
			http_error(r, KHTTP_505);
		} else
			http_error(r, KHTTP_204);
		ical_parsefile_close(st->path, fd);
		ical_free(cur);
		return;
	} else if ( ! st->isdir) {
		fprintf(stderr, "%s: WARNING: unsafe "
			"delete of resource\n", st->path);
		if (-1 != unlink(st->path))  {
			http_error(r, KHTTP_204);
			return;
		}
		perror(st->path);
		fprintf(stderr, "%s: fail unlink\n", st->path);
		http_error(r, KHTTP_505);
		return;
	} 

	/*
	 * This is NOT safe: we're blindly removing the contents
	 * of a collection (NOT its configuration) without any
	 * sort of checks on the collection data.
	 */
	fprintf(stderr, "%s: WARNING: unsafe "
		"delete of collection\n", st->path);

	assert(st->isdir);
	if (NULL == (dirp = opendir(st->path))) {
		perror(st->path);
		fprintf(stderr, "%s: fail opendir\n", st->path);
		http_error(r, KHTTP_505);
		return;
	}

	errs = 0;
	while (NULL != dirp && NULL != (dp = readdir(dirp))) {
		if ('.' == dp->d_name[0])
			continue;
		if (0 == strcmp(dp->d_name, "kcaldav.conf"))
			continue;
		if (0 == strcmp(dp->d_name, "kcaldav.ctag"))
			continue;
		strlcpy(buf, st->path, sizeof(buf));
		sz = strlcat(buf, dp->d_name, sizeof(buf));
		if (sz >= sizeof(buf)) {
			fprintf(stderr, "%s: too long "
				"(not removed)\n", buf);
			errs = 1;
		} else if (-1 == unlink(st->path)) {
			perror(st->path);
			fprintf(stderr, "%s: fail unlink\n", buf);
			errs = 1;
		} else 
			fprintf(stderr, "%s: delete\n", buf);
	}
	closedir(dirp);

	if (errs)
		http_error(r, KHTTP_505);
	else
		http_error(r, KHTTP_204);
}

