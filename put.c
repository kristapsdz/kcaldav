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
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

static struct ical *
req2ical(struct kreq *r)
{
	struct state	*st = r->arg;

	if (NULL == r->fieldmap[VALID_BODY]) {
		kerrx("%s: failed iCalendar parse", st->prncpl->name);
		http_error(r, KHTTP_400);
		return(NULL);
	} 
	
	if (KMIME_TEXT_CALENDAR != r->fieldmap[VALID_BODY]->ctypepos) {
		kerrx("%s: bad iCalendar MIME", st->prncpl->name);
		http_error(r, KHTTP_415);
		return(NULL);
	}

	return(ical_parse(NULL, 
		r->fieldmap[VALID_BODY]->val, 
		r->fieldmap[VALID_BODY]->valsz));
}

/*
 * Satisfy RFC 4791, 5.3.2, PUT.
 */
void
method_put(struct kreq *r)
{
	struct ical	*p;
	struct state	*st = r->arg;
	char		 buf[22];
	size_t		 sz;
	int64_t		 tag;
	char		*ep, *digest;
	int		 rc;

	if (NULL == st->cfg) {
		kerrx("%s: PUT into non-calendar "
			"collection", st->prncpl->name);
		http_error(r, KHTTP_403);
		return;
	} else if (NULL == (p = req2ical(r)))
		return;

	/* Check if PUT is conditional upon existing etag. */
	digest = NULL;
	if (NULL != r->reqmap[KREQU_IF_MATCH]) {
		sz = strlcpy(buf, 
			r->reqmap[KREQU_IF_MATCH]->val,
			sizeof(buf));
		if (sz >= sizeof(buf)) {
			kerrx("%s: bad \"If-Match\" buf", 
				st->prncpl->name);
			http_error(r, KHTTP_400);
			ical_free(p);
			return;
		}
		digest = buf;
	} else if (NULL != r->reqmap[KREQU_IF]) {
		sz = strlcpy(buf, 
			r->reqmap[KREQU_IF]->val,
			sizeof(buf));
		if (sz >= sizeof(buf)) {
			kerrx("%s: bad \"If\" digest", 
				st->prncpl->name);
			http_error(r, KHTTP_400);
			ical_free(p);
			return;
		}
		if ('(' != buf[0] || '[' != buf[1] ||
			 ']' != buf[sz - 2] || ')' != buf[sz - 1]) {
			kerrx("%s: bad \"If\" digest", 
				st->prncpl->name);
			http_error(r, KHTTP_400);
			ical_free(p);
			return;
		}
		buf[sz - 2] = '\0';
		buf[sz - 1] = '\0';
		digest = &buf[2];
	}

	if (NULL != digest)  {
		tag = strtoll(digest, &ep, 10);
		if (digest == ep || '\0' != *ep)
			digest = NULL;
		else if (tag == LLONG_MIN && errno == ERANGE)
			digest = NULL;
		else if (tag == LLONG_MAX && errno == ERANGE)
			digest = NULL;
		if (NULL == digest)
			kerrx("%s: bad numeric digest", 
				st->prncpl->name);
	}

	if (NULL == digest) 
		rc = db_resource_new
			(r->fieldmap[VALID_BODY]->val, 
			 st->resource, st->cfg->id);
	else
		rc = db_resource_update
			(r->fieldmap[VALID_BODY]->val, 
			 st->resource, tag, st->cfg->id);

	if (rc < 0) {
		kerrx("%s: failed creation: %s", 
			st->prncpl->name, r->fullpath);
		http_error(r, KHTTP_505);
	} else if (0 == rc) {
		kerrx("%s: duplicate resource: %s", 
			st->prncpl->name, r->fullpath);
		http_error(r, KHTTP_403);
	} else {
		kinfo("%s: resource %s: %s",
			st->prncpl->name, 
			NULL == digest ? "created" : "updated",
			r->fullpath);
		http_error(r, KHTTP_201);
	}

	ical_free(p);
}

