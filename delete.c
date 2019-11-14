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

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

void
method_delete(struct kreq *r)
{
	struct state	*st = r->arg;
	int		 rc;
	const char	*digest = NULL;

	if (st->cfg == NULL) {
		kerrx("%s: DELETE from non-calendar "
			"collection", st->prncpl->name);
		http_error(r, KHTTP_403);
		return;
	}

	if (r->reqmap[KREQU_IF_MATCH] != NULL)
		digest = r->reqmap[KREQU_IF_MATCH]->val;

	if (digest != NULL && st->resource[0] != '\0') {
		rc = db_resource_delete
			(st->resource, digest, st->cfg->id);
		if (rc == 0) {
			kerrx("%s: cannot delete: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_403);
		} else {
			kinfo("%s: resource deleted: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_204);
		}
		return;
	} else if (digest == NULL && st->resource[0] != '\0') {
		rc = db_resource_remove
			(st->resource, st->cfg->id);
		if (rc == 0) {
			kerrx("%s: cannot delete: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_403);
		} else {
			kinfo("%s: resource (unsafe) deleted: %s",
				st->prncpl->name, r->fullpath);
			http_error(r, KHTTP_204);
		}
		return;
	} 

	if (db_collection_remove(st->cfg->id, st->prncpl) == 0) {
		kinfo("%s: cannot delete: %s", 
			st->prncpl->name, r->fullpath);
		http_error(r, KHTTP_505);
	} else { 
		kinfo("%s: collection unlinked: %s", 
			st->prncpl->name, r->fullpath);
		http_error(r, KHTTP_204);
	}
}
