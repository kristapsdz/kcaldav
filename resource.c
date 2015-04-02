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
#include "main.h"

void
resource_calendar_data(struct kxmlreq *xml, const struct ical *p)
{

	ical_print(p, xml_ical_putc, xml);
}

void
resource_current_user_principal(struct kxmlreq *xml, const struct ical *p)
{

	collection_current_user_principal(xml);
}

void
resource_current_user_privilege_set(struct kxmlreq *xml, const struct ical *p)
{

	collection_current_user_privilege_set(xml);
}

void
resource_getcontenttype(struct kxmlreq *xml, const struct ical *p)
{

	kxml_puts(xml, kmimetypes[KMIME_TEXT_CALENDAR]);
}

void
resource_getetag(struct kxmlreq *xml, const struct ical *p)
{

	kxml_puts(xml, p->digest);
}

void
resource_owner(struct kxmlreq *xml, const struct ical *p)
{

	collection_owner(xml);
}

void
resource_quota_available_bytes(struct kxmlreq *xml, const struct ical *p)
{

	collection_quota_available_bytes(xml);
}

void
resource_quota_used_bytes(struct kxmlreq *xml, const struct ical *p)
{

	collection_quota_used_bytes(xml);
}

void
resource_resourcetype(struct kxmlreq *xml, const struct ical *p)
{

	/* Intentionally do nothing. */
}
