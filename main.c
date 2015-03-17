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
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>

#include "extern.h"

/* 
 * Validator for iCalendar OR CalDav object.
 * This checks the initial string of the object: if it equals the
 * prologue to an iCalendar, we attempt an iCalendar parse.
 * Otherwise, we try a CalDav parse.
 */
static int
kvalid(struct kpair *kp)
{
	size_t	 	 sz;
	const char	*tok = "BEGIN:VCALENDAR";
	struct ical	*ical;
	struct caldav	*dav;

	if ( ! kvalid_stringne(kp))
		return(0);
	sz = strlen(tok);
	if (0 == strncmp(kp->val, tok, sz)) {
		ical = ical_parse(kp->val);
		ical_free(ical);
		return(NULL != ical);
	}
	dav = caldav_parse(kp->val, kp->valsz);
	caldav_free(dav);
	return(NULL != dav);
}

/*
 * Put a character into a request stream.
 * We use this instead of just fputc because the stream might be
 * compressed, so we need kcgi(3)'s function.
 */
static void
ical_putc(int c, void *arg)
{
	struct kreq	*r = arg;

	khttp_putc(r, c);
}

/*
 * If found, convert the (already-validated) iCalendar.
 */
static struct ical *
req2ical(struct kreq *r)
{
	struct ical	*p;

	if (NULL == r->fieldmap[0]) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	}
	
	/* We already know that this works! */
	p = ical_parse(r->fieldmap[0]->val);
	assert(NULL != p);
	return(p);
}

/*
 * This converts the request into a CalDav object with one extra check
 * (we know that it will validate): we make sure that the CalDav request
 * is the correct type.
 */
static struct caldav *
req2caldav(struct kreq *r, enum type type)
{
	struct caldav	*p;

	if (NULL == r->fieldmap[0]) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	} 

	p = caldav_parse(r->fieldmap[0]->val, r->fieldmap[0]->valsz);
	assert(NULL != p);
	if (type == p->type)
		return(p);

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_400]);
	khttp_body(r);
	caldav_free(p);
	return(NULL);
}

/*
 * Satisfy 5.3.2, PUT.
 */
static void
put(struct kreq *r)
{
	struct ical	*p;
	enum khttp	 code;

	if (NULL == (p = req2ical(r)))
		return;

	fprintf(stderr, "%s: PUT: %s\n", r->arg, p->digest);
	code = ical_putfile(r->arg, p) ? KHTTP_201 : KHTTP_403;
	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[code]);
	khttp_head(r, kresps[KRESP_ETAG], "%s", p->digest);
	khttp_body(r);
	ical_free(p);
}

static void
get(struct kreq *r)
{
	struct ical	*p;

	if (NULL == (p = ical_parsefile(r->arg))) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_404]);
		khttp_body(r);
		return;
	}

	fprintf(stderr, "%s: GET: %s\n", r->arg, p->digest);
	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_CALENDAR]);
	khttp_head(r, kresps[KRESP_ETAG], "%s", p->digest);
	khttp_body(r);
	ical_print(p, ical_putc, r);
	ical_free(p);
}

static void
propfind(struct kreq *r)
{
	struct caldav	*dav;
	struct ical	*ical;
	size_t		 i;
	const char	*tag;

	if (NULL == (ical = ical_parsefile(r->arg))) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_404]);
		khttp_body(r);
		return;
	} else if (NULL == (dav = req2caldav(r, TYPE_PROPFIND))) {
		ical_free(ical);
		return;
	}

	/*
	 * The only property we respond to is the etag property, which
	 * we've set above.
	 */
	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_body(r);
	puts("<?xml version=\"1.0\" encoding=\"utf-8\" ?>");
	puts("<D:multistatus xmlns:D=\"DAV:\">");
	puts("<D:response>");
	printf("<D:href>http://%s%s%s</D:href>\n", 
		r->host, r->pname, r->fullpath);
	puts("<D:propstat>");
	puts("<D:prop>");
	for (i = 0; i < dav->propsz; i++) {
		tag = calelems[calpropelems[dav->props[i].key]];
		if (PROP_GETETAG == dav->props[i].key)
			printf("<D:%s>%s</D:%s>\n", tag, ical->digest, tag);
		else
			printf("<D:%s />\n", tag);
	}
	puts("</D:prop>");
	puts("<D:status>HTTP/1.1 200 OK</D:status>");
	puts("</D:propstat>");
	puts("</D:response>");
	puts("</D:multistatus>");
	caldav_free(dav);
	ical_free(ical);
}

static void
options(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_ALLOW], "%s", 
		"OPTIONS, GET, PUT, PROPFIND");
	khttp_body(r);
}

int
main(void)
{
	struct kreq	 r;
	char		*path;
	struct kvalid	 valid = { kvalid, "" };

	if (KCGI_OK != khttp_parse(&r, &valid, 1, NULL, 0, 0))
		return(EXIT_FAILURE);

	if (strstr(r.fullpath, "../") || 
			strstr(r.fullpath, "/..") ||
			'/' != r.fullpath[0]) {
		fprintf(stderr, "%s: insecure path\n", r.fullpath);
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(&r);
		khttp_free(&r);
		return(EXIT_FAILURE);
	}

	kasprintf(&path, "%s%s", CALDIR, r.fullpath);
	r.arg = path;

	switch (r.method) {
	case (KMETHOD_OPTIONS):
		options(&r);
		break;
	case (KMETHOD_PUT):
		put(&r);
		break;
	case (KMETHOD_PROPFIND):
		propfind(&r);
		break;
	case (KMETHOD_GET):
		get(&r);
		break;
	default:
		fprintf(stderr, "Unknown method\n");
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_405]);
		khttp_body(&r);
		break;
	}

	free(path);
	khttp_free(&r);
	return(EXIT_SUCCESS);
}
