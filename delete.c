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
			ctag_update(st->ctagfile);
			http_error(r, KHTTP_204);
		}
		ical_parsefile_close(st->path, fd);
		ical_free(cur);
		kinfo("%s: resource deleted: %s",
			st->path, st->auth.user);
		return;
	} else if ( ! st->isdir) {
		kerrx("%s: WARNING: unsafe delete", st->path);
		if (-1 != unlink(st->path))  {
			ctag_update(st->ctagfile);
			http_error(r, KHTTP_204);
		} else {
			kerr("%s: unlink", st->path);
			http_error(r, KHTTP_505);
		}
		kinfo("%s: resource deleted: %s",
			st->path, st->auth.user);
		return;
	} 

	/*
	 * This is NOT safe: we're blindly removing the contents
	 * of a collection (NOT its configuration) without any
	 * sort of checks on the collection data.
	 */
	assert(st->isdir);
	strlcpy(buf, st->path, sizeof(buf));
	sz = strlcat(buf, "kcaldav.conf", sizeof(buf));
	if (sz >= sizeof(buf)) {
		kerrx("%s: path too long", buf);
		http_error(r, KHTTP_505);
		return;
	}

	/*
	 * Begin by removing the kcaldav.conf file.
	 * We do this within an exclusive lock to protect corrupting any
	 * other sort of work on the file.
	 */
	fd = open_lock_ex(buf, O_RDONLY, 0600);
	if (-1 == fd) {
		http_error(r, KHTTP_403);
		return;
	} else if (-1 == unlink(buf)) {
		kerr("%s: unlink", buf);
		http_error(r, KHTTP_403);
		return;
	} 
	close_unlock(buf, fd);

	kinfo("%s: collection unlinked: %s", buf, st->auth.user);
	http_error(r, KHTTP_204);

	/*
	 * Now the configuration file has been removed, we remove
	 * everything else in the collection.
	 * Since the kcaldav.conf file doesn't exist any more, the
	 * directory is no longer visible to kcaldav, so just return
	 * that everything went alright.
	 */
	if (NULL == (dirp = opendir(st->path))) {
		kerr("%s: opendir", st->path);
		kinfo("%s: directory remains for unlinked "
			"collection: %s", st->path, st->auth.user);
		return;
	}

	errs = 0;
	while (NULL != dirp && NULL != (dp = readdir(dirp))) {
		if (0 == strcmp(dp->d_name, "."))
			continue;
		if (0 == strcmp(dp->d_name, ".."))
			continue;
		strlcpy(buf, st->path, sizeof(buf));
		sz = strlcat(buf, dp->d_name, sizeof(buf));
		if (sz >= sizeof(buf))
			kerrx("%s: path too long", buf);
		else if (-1 == unlink(buf))
			kerr("%s: unlink", buf);
		else
			kinfo("%s: deleted: %s", buf, st->auth.user);
	}
	if (-1 == closedir(dirp))
		kerr("%s: closedir", st->path);
	if (-1 == rmdir(st->path)) 
		kerr("%s: rmdir", st->path);
}
