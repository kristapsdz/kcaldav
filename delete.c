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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

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
		kerrx("%s: principal does not "
			"have delete acccess: %s", 
			st->path, st->prncpl->name);
		http_error(r, KHTTP_403);
		return;
	} 

	digest = NULL;
	if (NULL != r->reqmap[KREQU_IF_MATCH]) {
		digest = r->reqmap[KREQU_IF_MATCH]->val;
		if (32 != (sz = strlen(digest))) {
			kerrx("%s: \"If-Match\" digest "
				"invalid: %zuB", st->path, sz);
			http_error(r, KHTTP_400);
			return;
		}
	}

	if (NULL != digest && ! st->isdir) {
		cur = ical_parsefile_open(st->path, &fd);
		if (NULL == cur) {
			http_error(r, KHTTP_404);
		} else if (strcmp(cur->digest, digest)) {
			kerrx("%s: fail digest", st->path);
			http_error(r, KHTTP_412);
		} else if (-1 == unlink(st->path)) {
			kerr("%s: unlink", st->path);
			http_error(r, KHTTP_505);
		} else {
			ctagcache_update(st->ctagfile);
			http_error(r, KHTTP_204);
		}
		ical_parsefile_close(st->path, fd);
		ical_free(cur);
		return;
	} else if ( ! st->isdir) {
		kerrx("%s: WARNING: unsafe delete", st->path);
		if (-1 != unlink(st->path))  {
			ctagcache_update(st->ctagfile);
			http_error(r, KHTTP_204);
		} else {
			kerr("%s: unlink", st->path);
			http_error(r, KHTTP_505);
		}
		return;
	} 

	/*
	 * This is NOT safe: we're blindly removing the contents
	 * of a collection (NOT its configuration) without any
	 * sort of checks on the collection data.
	 */
	kerrx("%s: WARNING: unsafe delete", st->path);

	assert(st->isdir);
	if (NULL == (dirp = opendir(st->path))) {
		kerr("%s: opendir", st->path);
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
			kerrx("%s: path too long", buf);
			errs = 1;
		} else if (-1 == unlink(buf)) {
			kerr("%s: unlink", buf);
			errs = 1;
		} 
	}
	if (-1 == closedir(dirp))
		kerr("%s: closedir", st->path);

	if (errs)
		http_error(r, KHTTP_505);
	else
		http_error(r, KHTTP_204);
	ctagcache_update(st->ctagfile);
}
