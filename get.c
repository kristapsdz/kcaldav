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
#include <inttypes.h>
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

/*
 * RFC 4791 doesn't specify any special behaviour for GET, and RFC 2518
 * just says it follows HTTP, RFC 2068.
 * So use those semantics.
 */
void
method_get(struct kreq *r)
{
	struct res	*p;
	struct state	*st = r->arg;
	const char	*cp;
	char		 etag[22];
	int		 rc;

	if ('\0' == st->resource[0]) {
		kerrx("%s: GET for collection", st->prncpl->name);
		http_error(r, KHTTP_404);
		return;
	} else if (NULL == st->cfg) {
		kerrx("%s: GET from non-calendar "
			"collection", st->prncpl->name);
		http_error(r, KHTTP_403);
		return;
	}

	rc = db_resource_load(&p, st->resource, st->cfg->id);
	if (rc < 0) {
		kerrx("%s: failed load resource", st->prncpl->name);
		http_error(r, KHTTP_505);
		return;
	} else if (0 == rc) {
		kerrx("%s: unknown resource", st->prncpl->name);
		http_error(r, KHTTP_404);
		return;
	}

	/* 
	 * If we request with the If-None-Match header (see RFC 2068,
	 * 14.26), then see if the ETag is consistent.
	 * If it is, then indicate that the remote cache is ok.
	 * If not, resend the data.
	 */
	if (NULL != r->reqmap[KREQU_IF_NONE_MATCH]) {
		snprintf(etag, sizeof(etag), "%" PRId64, p->etag);
		cp = r->reqmap[KREQU_IF_NONE_MATCH]->val;
		if (0 == strcmp(etag, cp)) {
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_304]);
			khttp_head(r, kresps[KRESP_ETAG], 
				"%" PRId64, p->etag);
			khttp_body(r);
			res_free(p);
			return;
		} 
	} 

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_CALENDAR]);
	khttp_head(r, kresps[KRESP_ETAG], "%" PRId64, p->etag);
	khttp_body(r);
	ical_print(p->ical, http_ical_putc, r);
	res_free(p);
}

