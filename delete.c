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
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "libkcaldav.h"
#include "db.h"
#include "server.h"

void
method_delete(struct kreq *r)
{
	struct state	*st = r->arg;
	int		 rc;
	const char	*digest = NULL;
	char		*buf = NULL;

	if (st->cfg == NULL) {
		kutil_warnx(r, st->prncpl->name, 
			"DELETE of non-calendar collection");
		http_error(r, KHTTP_403);
		return;
	}

	if (r->reqmap[KREQU_IF_MATCH] != NULL) {
		digest = http_etag_if_match
			(r->reqmap[KREQU_IF_MATCH]->val, &buf);

		/*
		 * If the etag is not quoted and set to "*", this means
		 * (according to RFC 7232 section 3.1) that the
		 * condition is FALSE if the "listed representation"
		 * (i.e., the resource name) is not matched.
		 * Since we lookup based on the resource name, this
		 * simply means that it's the unsafe behaviour, so set
		 * the digest as NULL.
		 */

		if (digest != NULL &&
		    buf == NULL && strcmp(digest, "*") == 0)
			digest = NULL;
	}

	if (digest != NULL && st->resource[0] != '\0') {
		rc = db_resource_delete
			(st->resource, digest, st->cfg->id);
		if (rc == 0) {
			kutil_errx_noexit(r, st->prncpl->name,
				"cannot delete resource: %s", 
				st->resource);
			http_error(r, KHTTP_505);
		} else {
			kutil_dbg(r, st->prncpl->name,
				"resource deleted: %s", 
				st->resource);
			http_error(r, KHTTP_204);
		}
	} else if (digest == NULL && st->resource[0] != '\0') {
		rc = db_resource_remove
			(st->resource, st->cfg->id);
		if (rc == 0) {
			kutil_errx_noexit(r, st->prncpl->name,
				"cannot delete resource: %s", 
				st->resource);
			http_error(r, KHTTP_505);
		} else {
			kutil_dbg(r, st->prncpl->name,
				"resource (unsafe) deleted: %s", 
				st->resource);
			http_error(r, KHTTP_204);
		}
	} else {
		/* 
		 * FIXME: deleting the collection should do the same check with
		 * the etag (using the ctag, in this case).
		 */

		if (db_collection_remove(st->cfg->id, st->prncpl) == 0) {
			kutil_errx_noexit(r, st->prncpl->name,
				"cannot delete collection");
			http_error(r, KHTTP_505);
		} else { 
			kutil_dbg(r, st->prncpl->name,
				"collection deleted");
			http_error(r, KHTTP_204);
		}
	}

	free(buf);
}
