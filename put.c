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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <bsd/stdlib.h>
#endif

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "main.h"

/*
 * If found, convert the (already-validated) iCalendar.
 */
static struct ical *
req2ical(struct kreq *r)
{
	struct ical	*p = NULL;

	if (NULL != r->fieldmap[0]) 
		p = ical_parse(NULL, 
			r->fieldmap[0]->val, 
			r->fieldmap[0]->valsz);

	return(p);
}

/*
 * Try creating a unique lock-file.
 * This involves taking the "temp" file in our state, filling the last 8
 * bytes with random alnums, then trying to create a unique filename.
 * Returns the new fd or -1 if something really goes wrong.
 */
static int
req2temp(struct kreq *r)
{
	struct state	*st = r->arg;
	size_t		 sz, i;
	int 		 fd;

	assert( ! st->isdir);
	sz = strlen(st->temp);
	assert(sz > 9);
	sz -= 8;
	assert('.' == st->temp[sz - 1]);

	do {
		for (i = 0; i < 8; i++)
			snprintf(&st->temp[sz + i], 3, 
				"%.1X", arc4random_uniform(128));
		fd = open(st->temp, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd < 0 && EEXIST != errno) {
			fprintf(stderr, "%s: open: %s\n",
				st->temp, strerror(errno));
			return(-1);
		}
	} while (-1 == fd);

	return(fd);
}

/*
 * Satisfy RFC 4791, 5.3.2, PUT.
 */
void
method_put(struct kreq *r)
{
	struct ical	*p, *cur;
	struct state	*st = r->arg;
	const char	*digest;
	size_t		 sz;
	int		 fd, er;

	/* Need write-content or bind permissions. */
	if ( ! (PERMS_WRITE & st->cfg->perms)) {
		fprintf(stderr, "%s: principal does "
			"not have write acccess: %s\n", 
			st->path, st->prncpl->name);
		http_error(r, KHTTP_403);
		return;
	} else if (NULL == (p = req2ical(r))) {
		fprintf(stderr, "%s: failed parse ICS "
			"in client request\n", st->path);
		http_error(r, KHTTP_400);
		return;
	}

	/* Check if PUT is conditional upon existing etag. */
	digest = NULL;
	if (NULL != r->reqmap[KREQU_IF_MATCH]) {
		digest = r->reqmap[KREQU_IF_MATCH]->val;
		if (32 != (sz = strlen(digest))) {
			fprintf(stderr, "%s: \"If-Match\" digest "
				"invalid: %zuB\n", st->path, sz);
			http_error(r, KHTTP_400);
			ical_free(p);
			return;
		}
	} else if (NULL != r->reqmap[KREQU_IF]) {
		digest = r->reqmap[KREQU_IF]->val;
		if (36 != (sz = strlen(digest)) ||
			 '(' != digest[0] || '[' != digest[1] ||
			 ']' != digest[34] || ')' != digest[35]) {
			fprintf(stderr, "%s: \"If\" digest "
				"invalid: %zuB\n", st->path, sz);
			http_error(r, KHTTP_400);
			ical_free(p);
			return;
		}
		digest += 2;
	}

	/*
	 * Begin by writing into a temporary file.
	 * We use the same scheme for all temporary files (start the
	 * file with a dot), so no worries about pollution.
	 */
	if (-1 == (fd = req2temp(r))) {
		http_error(r, KHTTP_505);
		ical_free(p);
		return;
	} else if (0 == ical_printfile(fd, p)) {
		er = errno;
		fprintf(stderr, "%s: write: %s\n", 
			st->temp, strerror(errno));
		http_error(r, (EDQUOT == er ||
			ENOSPC == er || EFBIG == er) ?
			KHTTP_507 : KHTTP_505);
		close(fd);
		ical_free(p);
		if (-1 != unlink(st->temp))
			return;
		fprintf(stderr, "%s: unlink: %s\n", 
			st->temp, strerror(errno));
		return;
	} else if (-1 == close(fd)) 
		fprintf(stderr, "%s: close: %s\n",
			st->temp, strerror(errno));

	/*
	 * If we have a digest, then we want to replace an existing
	 * file.
	 * First check if the destination file is a match, then rename
	 * into the real file (all while the real file is locked).
	 */
	if (NULL != digest) {
		/* 
		 * We now have our new iCal "p" stored in the temporary
		 * file st->temp.
		 * Now we want to lock the old file (st->path) and flip
		 * the temporary into the new file.
		 */
		cur = ical_parsefile_open(st->path, &fd);
		if (NULL == cur) {
			http_error(r, KHTTP_415);
			ical_free(p);
			if (-1 != unlink(st->temp)) 
				return;
			fprintf(stderr, "%s: unlink: %s\n", 
				st->temp, strerror(errno));
			return;
		} else if (-1 == rename(st->temp, st->path)) {
			er = errno;
			fprintf(stderr, "%s: rename(%s): %s\n",
				st->temp, st->path, strerror(errno));
			http_error(r, KHTTP_505);
			ical_parsefile_close(st->path, fd);
			ical_free(p);
			ical_free(cur);
			ctagcache_update(st->ctagfile);
			if (-1 != unlink(st->temp) || ENOENT == er)
				return;
			fprintf(stderr, "%s: unlink: %s\n", 
				st->temp, strerror(errno));
			return;
		}
		/* It worked! */
		ctagcache_update(st->ctagfile);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_201]);
		khttp_head(r, kresps[KRESP_ETAG], 
			"%s", p->digest);
		khttp_body(r);
		ical_parsefile_close(st->path, fd);
		ical_free(p);
		ical_free(cur);
		fprintf(stderr, "%s: updated\n", st->path);
		return;
	}

	/*
	 * We don't have a digest, thus, we're trying to write into a
	 * new destination file.
	 * Make sure that this doesn't exist: create a new unique file,
	 * lock it, then swap into it.
	 */
	fd = open_lock_ex(st->path, O_CREAT | O_EXCL | O_RDWR, 0600);

	if (-1 == fd) {
		er = errno;
		if (EDQUOT == er || ENOSPC == er)
			http_error(r, KHTTP_507);
		else if (EEXIST == er)
			http_error(r, KHTTP_412);
		else
			http_error(r, KHTTP_505);
		ical_free(p);
		if (-1 != unlink(st->temp)) 
			return;
		fprintf(stderr, "%s: unlink: %s\n", 
			st->temp, strerror(errno));
		return;
	} else if (-1 == rename(st->temp, st->path)) {
		er = errno;
		fprintf(stderr, "%s: rename(%s): %s\n", 
			st->temp, st->path, strerror(errno));
		http_error(r, KHTTP_505);
		if (-1 == unlink(st->temp) && ENOENT != er) 
			fprintf(stderr, "%s: unlink: %s\n", 
				st->temp, strerror(errno));
		if (-1 == unlink(st->path)) 
			fprintf(stderr, "%s: unlink: %s\n", 
				st->path, strerror(errno));
		close_unlock(st->path, fd);
		ical_free(p);
		ctagcache_update(st->ctagfile);
		return;
	}

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_201]);
	khttp_head(r, kresps[KRESP_ETAG], 
		"%s", p->digest);
	khttp_body(r);
	close_unlock(st->path, fd);
	ical_free(p);
	fprintf(stderr, "%s: added\n", st->path);
	ctagcache_update(st->ctagfile);
}

