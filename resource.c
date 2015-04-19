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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

/*
 * RFC 4791, 9.6.
 */
void
resource_calendar_data(struct kxmlreq *xml, const struct ical *p)
{

	ical_print(p, xml_ical_putc, xml);
}

/*
 * RFC 5379, 3.
 * We define this over the whole collection.
 */
void
resource_current_user_principal(struct kxmlreq *xml, const struct ical *p)
{

	collection_current_user_principal(xml);
}

/*
 * RFC 3744, 5.4.
 * We define this over the whole collection for now.
 */
void
resource_current_user_privilege_set(struct kxmlreq *xml, const struct ical *p)
{

	collection_current_user_privilege_set(xml);
}

/*
 * RFC 4918, 15.5.
 */
void
resource_getcontenttype(struct kxmlreq *xml, const struct ical *p)
{

	kxml_puts(xml, kmimetypes[KMIME_TEXT_CALENDAR]);
}

/*
 * RFC 4918, 15.6.
 */
void
resource_getetag(struct kxmlreq *xml, const struct ical *p)
{

	kxml_puts(xml, p->digest);
}

/*
 * RFC 4918, 14.17.
 * This only refers to locks, but provide it anyway.
 */
void
resource_owner(struct kxmlreq *xml, const struct ical *p)
{

	collection_owner(xml);
}

/*
 * RFC 4331, 3.
 */
void
resource_quota_available_bytes(struct kxmlreq *xml, const struct ical *p)
{

	collection_quota_available_bytes(xml);
}

/*
 * RFC 4331, 4.
 */
void
resource_quota_used_bytes(struct kxmlreq *xml, const struct ical *p)
{

	collection_quota_used_bytes(xml);
}

/*
 * RFC 4918, 15.9.
 */
void
resource_resourcetype(struct kxmlreq *xml, const struct ical *p)
{

	/* 
	 * Intentionally do nothing. 
	 * This is specified by the RFC (the default).
	 */
}

/*
 * RFC 6638, 2.4.1.
 * Route through to collection handler.
 */
void
resource_calendar_user_address_set(struct kxmlreq *xml, const struct ical *p)
{

	collection_calendar_user_address_set(xml);
}

/*
 * RFC 4791, 6.2.1.
 * Route through to collection handler.
 */
void
resource_calendar_home_set(struct kxmlreq *xml, const struct ical *p)
{

	collection_calendar_home_set(xml);
}
