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
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "main.h"

static	const char *const __xmls[XML__MAX] = {
	"C:calendar", /* XML_CALDAV_CALENDAR */
	"C:comp", /* XML_CALDAV_COMP */
	"C:opaque", /* XML_CALDAV_OPAQUE */
	"D:bind", /* XML_DAV_BIND */
	"D:collection", /* XML_DAV_COLLECTION */
	"D:href", /* XML_DAV_HREF */
	"D:multistatus", /* XML_DAV_MULTISTATUS */
	"D:privilege", /* XML_DAV_PRIVILEGE */
	"D:prop", /* XML_DAV_PROP */
        "D:propstat", /* XML_DAV_PROPSTAT */
	"D:read", /* XML_DAV_READ */
	"D:read-current-user-privilege-set", /* XML_DAV_READ_CUR... */
	"D:response", /* XML_DAV_RESPONSE */
	"D:status", /* XML_DAV_STATUS */
	"D:unbind", /* XML_DAV_UNBIND */
	"D:write-content", /* XML_DAV_WRITE_CONTENT */
};

const char *const *xmls = __xmls;

int
xml_ical_putc(int c, void *arg)
{
	struct kxmlreq	*r = arg;

	kxml_putc(r, c);
	return(1);
}

int
http_ical_putc(int c, void *arg)
{
	struct kreq	*r = arg;

	khttp_putc(r, c);
	return(1);
}

void
http_error(struct kreq *r, enum khttp c)
{
	char	 nonce[33];
	size_t	 i;

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[c]);
	if (KHTTP_401 == c) {
		for (i = 0; i < sizeof(nonce); i++)
			snprintf(nonce + i, 2, "%01X", 
				arc4random_uniform(128));
		khttp_head(r, kresps[KRESP_WWW_AUTHENTICATE],
			"Digest realm=\"kcaldav\" "
			"quop=\"\" nonce=\"%s\"", nonce);
	}

	khttp_body(r);
}

