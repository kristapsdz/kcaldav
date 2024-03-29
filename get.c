/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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
	int		 rc;
	char		*buf = NULL;
	const char	*digest = NULL;

	if (st->resource[0] == '\0') {
		kutil_warnx(r, st->prncpl->name, 
			"GET for non-resource (collection?)");
		http_error(r, KHTTP_404);
		return;
	} else if (st->cfg == NULL) {
		kutil_warnx(r, st->prncpl->name, 
			"GET from non-calendar collection");
		http_error(r, KHTTP_403);
		return;
	}

	rc = db_resource_load(&p, st->resource, st->cfg->id);

	if (rc < 0) {
		kutil_errx_noexit(r, st->prncpl->name,
			"cannot load resource: %s", st->resource);
		http_error(r, KHTTP_505);
		return;
	} else if (rc == 0) {
		kutil_warnx(r, st->prncpl->name, 
			"GET for unknown resource: %s", st->resource);
		http_error(r, KHTTP_404);
		return;
	}

	/* 
	 * If we request with the If-None-Match header (see RFC 2068,
	 * 14.26), then see if the ETag is consistent.
	 * If it is, then indicate that the remote cache is ok.
	 * If not, resend the data.
	 */

	if (r->reqmap[KREQU_IF_NONE_MATCH] != NULL) {
		digest = http_etag_if_match
			(r->reqmap[KREQU_IF_NONE_MATCH]->val, &buf);

		/*
		 * If the etag is not quoted and set to "*", this means
		 * (according to RFC 7232 section 3.1) that the
		 * condition is FALSE if the "listed representation"
		 * (i.e., the resource name) is not matched.
		 * We know the listed resource matches, so require a
		 * full resend.
		 */

		if (digest != NULL &&
		    buf == NULL && strcmp(digest, "*") == 0)
			digest = NULL;
	}

	if (digest != NULL && strcmp(p->etag, digest) == 0) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_304]);
		khttp_head(r, kresps[KRESP_ETAG], "%s", p->etag);
		khttp_body(r);
	} else {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_200]);
		khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
			"%s", kmimetypes[KMIME_TEXT_CALENDAR]);
		khttp_head(r, kresps[KRESP_ETAG], "%s", p->etag);
		khttp_body(r);
		ical_print(p->ical, http_ical_putc, r);
	}

	db_resource_free(p);
	free(buf);
}

