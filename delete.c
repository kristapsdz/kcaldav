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

void
method_delete(struct kreq *r)
{
	struct state	*st = r->arg;
	int		 rc;
	const char	*digest;
	int64_t		 tag;
	char		*ep;

	if (NULL == st->cfg) {
		kerrx("%s: DELETE from non-calendar "
			"collection", st->prncpl->name);
		http_error(r, KHTTP_403);
		return;
	}

	digest = NULL;
	if (NULL != r->reqmap[KREQU_IF_MATCH]) {
		digest = r->reqmap[KREQU_IF_MATCH]->val;
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

	if (NULL != digest && '\0' != st->resource[0]) {
		rc = db_resource_delete
			(st->resource, 
			tag, st->cfg->id);
		if (0 == rc) {
			kerrx("%s: cannot deleted: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_403);
		} else {
			kinfo("%s: resource deleted: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_204);
		}
		return;
	} else if (NULL == digest && '\0' != st->resource[0]) {
		rc = db_resource_remove
			(st->resource, st->cfg->id);
		if (0 == rc) {
			kerrx("%s: cannot deleted: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_403);
		} else {
			kinfo("%s: resource (unsafe) deleted: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_204);
		}
		return;
	} 

	if (0 == db_collection_remove(st->cfg->id, st->prncpl)) {
		kinfo("%s: cannot delete: %s", 
			st->prncpl->name, r->fullpath);
		http_error(r, KHTTP_505);
	} else { 
		kinfo("%s: collection unlinked: %s", 
			st->prncpl->name, r->fullpath);
		http_error(r, KHTTP_204);
	}
}
