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
	"D:collection", /* XML_DAV_COLLECTION */
	"D:href", /* XML_DAV_HREF */
	"D:multistatus", /* XML_DAV_MULTISTATUS */
	"D:prop", /* XML_DAV_PROP */
        "D:propstat", /* XML_DAV_PROPSTAT */
	"D:response", /* XML_DAV_RESPONSE */
	"D:status", /* XML_DAV_STATUS */
	"D:unauthenticated", /* XML_DAV_UNAUTHENTICATED */
};

const char *const *xmls = __xmls;

void
xml_ical_putc(int c, void *arg)
{
	struct kxmlreq	*r = arg;

	kxml_putc(r, c);
}

void
http_ical_putc(int c, void *arg)
{
	struct kreq	*r = arg;

	khttp_putc(r, c);
}
