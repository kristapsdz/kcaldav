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

#include <sys/types.h>

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
 * RFC 4791, 6.2.1.
 */
void
collection_calendar_home_set(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, st->rpath);
	kxml_pop(xml);
}

/*
 * RFC 6638, 2.4.1.
 */
void
collection_calendar_user_address_set(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, "mailto:");
	kxml_puts(xml, st->prncpl->email);
	kxml_pop(xml);
}

/*
 * RFC 5379, 3.
 */
void
collection_current_user_principal(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_puts(xml, st->prncpl->homedir);
	kxml_pop(xml);
}

/*
 * RFC 3744, 5.4.
 */
void
collection_current_user_privilege_set(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_PRIVILEGE);
	kxml_pushnull(xml, XML_DAV_READ_CURRENT_USER_PRIVILEGE_SET);
	kxml_pop(xml);

	if (PERMS_READ & st->cfg->perms) {
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_READ);
		kxml_pop(xml);
	}
	if (PERMS_WRITE & st->cfg->perms) {
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_WRITE_CONTENT);
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_BIND);
		kxml_pop(xml);
	}
	if (PERMS_DELETE & st->cfg->perms) {
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_UNBIND);
		kxml_pop(xml);
	}
}

/*
 * RFC 4918, 15.2.
 */
void
collection_displayname(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_puts(xml, st->cfg->displayname);
}

/*
 * caldav-ctag-02, 4.1.
 */
void
collection_getctag(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;
	char		 ctag[65];

	ctag_get(st->ctagfile, ctag);
	kxml_puts(xml, ctag);
}

/*
 * RFC 4918, 14.17.
 * This only refers to locks, but provide it anyway.
 */
void
collection_owner(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, st->rpath);
	kxml_pop(xml);
}

/*
 * RFC 3744, 4.2.
 */
void
collection_principal_url(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, st->rpath);
	kxml_pop(xml);
}

/*
 * RFC 4331, 3.
 * This is defined over the collection; the resource handler simply
 * calls through here.
 */
void
collection_quota_available_bytes(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;
	char	 	 buf[32];

	snprintf(buf, sizeof(buf), "%lld", st->cfg->bytesavail);
	kxml_puts(xml, buf);
}

/*
 * RFC 4331, 4.
 * This is defined over the collection; the resource handler simply
 * calls through here.
 */
void
collection_quota_used_bytes(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;
	char	 	 buf[32];

	snprintf(buf, sizeof(buf), "%lld", st->cfg->bytesused);
	kxml_puts(xml, buf);
}

/*
 * RFC 4918, 15.9.
 * This is specific to collections; resource have their own.
 */
void
collection_resourcetype(struct kxmlreq *xml)
{

	kxml_pushnull(xml, XML_DAV_COLLECTION);
	kxml_pushnull(xml, XML_CALDAV_CALENDAR);
}

/* 
 * RFC 6638, 9.1.
 * This seems to be required by iOS, although the standard says the
 * default value is OPAQUE.
 */
void
collection_schedule_calendar_transp(struct kxmlreq *xml)
{

	kxml_pushnull(xml, XML_CALDAV_OPAQUE);
}

/*
 * RFC 4791, 5.2.3.
 */
void
collection_supported_calendar_component_set(struct kxmlreq *xml)
{
	enum icaltype	 i;

	/* Accept all of the iCalendar types we know about. */
	for (i = 0; i < ICALTYPE__MAX; i++) 
		kxml_pushnullattrs(xml, XML_CALDAV_COMP,
			"name", icaltypes[i], NULL);
}

/*
 * RFC 4791, 5.2.4.
 */
void
collection_supported_calendar_data(struct kxmlreq *xml)
{

	/* Use the canonical type. */
	kxml_pushnullattrs(xml, XML_CALDAV_CALENDAR_DATA, 
		"content-type", kmimetypes[KMIME_TEXT_CALENDAR], 
		"version", "2.0", NULL);
}

/*
 * RFC 4791, 5.2.2.
 */
void
collection_calendar_timezone(struct kxmlreq *xml)
{

	/* 
	 * Stipulate GMT.
	 * This makes free-floating time convenient in that we can just
	 * say that they're all UTC.
	 */
	kxml_puts(xml, "BEGIN:VCALENDAR\r\n");
	kxml_puts(xml, "PRODID:-//BSD.lv Project/kcaldav " 
		VERSION "//EN\r\n");
	kxml_puts(xml, "VERSION:2.0\r\n");
	kxml_puts(xml, "BEGIN:VTIMEZONE\r\n");
	kxml_puts(xml, "TZID:GMT\r\n");
	kxml_puts(xml, "BEGIN:STANDARD\r\n");
	kxml_puts(xml, "DTSTART:19700101T000000\r\n");
	kxml_puts(xml, "TZOFFSETTO:+0000\r\n");
	kxml_puts(xml, "TZOFFSETFROM:+0000\r\n");
	kxml_puts(xml, "END:STANDARD\r\n");
	kxml_puts(xml, "END:VTIMEZONE\r\n");
	kxml_puts(xml, "END:VCALENDAR\r\n");
}

/*
 * RFC 4791, 5.2.6.
 */
void
collection_calendar_min_date_time(struct kxmlreq *xml)
{

	kxml_puts(xml, "19700101T000000Z");
}
