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

#include <sys/types.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "libkcaldav.h"
#include "db.h"
#include "server.h"

/*
 * Non-standard.
 * Used by Apple's clients.
 */
static void
collection_calendar_colour(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_puts(xml, c->colour);
}

/*
 * RFC 4791, 5.2.1.
 */
static void
collection_calendar_description(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_puts(xml, c->description);
}

/*
 * RFC 4791, 6.2.1.
 */
static void
principal_calendar_home_set(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_pop(xml);
}

/*
 * RFC 4791, 6.2.1.
 * Route through to principal handler.
 */
static void
collection_calendar_home_set(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	principal_calendar_home_set(req, xml);
}

/*
 * RFC 4791, 6.2.1.
 * Route through to principal handler.
 */
static void
resource_calendar_home_set(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c, 
	const struct res *p)
{

	principal_calendar_home_set(req, xml);
}

/*
 * caldav-proxy.txt, 5.3.1.
 */
static void
principal_calendar_proxy_read_for(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;
	size_t		 i;

	for (i = 0; i < st->rprncpl->rproxiesz; i++) {
		if (PROXY_WRITE != st->rprncpl->rproxies[i].bits)
			continue;
		kxml_push(xml, XML_DAV_HREF);
		kxml_puts(xml, req->pname);
		kxml_putc(xml, '/');
		kxml_puts(xml, st->rprncpl->rproxies[i].name);
		kxml_putc(xml, '/');
		kxml_pop(xml);
	}
}

/*
 * caldav-proxy.txt, 5.3.2.
 */
static void
principal_calendar_proxy_write_for(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;
	size_t		 i;

	for (i = 0; i < st->rprncpl->rproxiesz; i++) {
		if (PROXY_READ != st->rprncpl->rproxies[i].bits)
			continue;
		kxml_push(xml, XML_DAV_HREF);
		kxml_puts(xml, req->pname);
		kxml_putc(xml, '/');
		kxml_puts(xml, st->rprncpl->rproxies[i].name);
		kxml_putc(xml, '/');
		kxml_pop(xml);
	}
}

/*
 * RFC 6638, 2.4.1.
 */
static void
principal_calendar_user_address_set(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, "mailto:");
	kxml_puts(xml, st->rprncpl->email);
	kxml_pop(xml);
}

/*
 * RFC 6638, 2.4.1.
 */
static void
collection_calendar_user_address_set(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	principal_calendar_user_address_set(req, xml);
}

/*
 * RFC 6638, 2.4.1.
 */
static void
resource_calendar_user_address_set(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c, 
	const struct res *p)
{

	principal_calendar_user_address_set(req, xml);
}

/*
 * RFC 5379, 3.
 */
static void
principal_current_user_principal(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->prncpl->name);
	kxml_putc(xml, '/');
	kxml_pop(xml);
}

/*
 * RFC 5379, 3.
 */
static void
collection_current_user_principal(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	principal_current_user_principal(req, xml);
}

/*
 * RFC 5379, 3.
 */
static void
resource_current_user_principal(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c, 
	const struct res *p)
{

	principal_current_user_principal(req, xml);
}

/*
 * RFC 3744, 5.4.
 */
static void
collection_current_user_privilege_set(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{
	struct state	*st = req->arg;

	kxml_push(xml, XML_DAV_PRIVILEGE);
	kxml_pushnull(xml, XML_DAV_READ_CURRENT_USER_PRIVILEGE_SET);
	kxml_pop(xml);

	if (st->prncpl == st->rprncpl ||
	    PROXY_WRITE == st->proxy) {
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_READ);
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_WRITE);
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_BIND);
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_UNBIND);
		kxml_pop(xml);
	} else if (PROXY_READ == st->proxy) {
		kxml_push(xml, XML_DAV_PRIVILEGE);
		kxml_pushnull(xml, XML_DAV_READ);
		kxml_pop(xml);
	}
}

/*
 * RFC 3744, 5.4.
 * We define this over the whole collection for now.
 */
static void
resource_current_user_privilege_set(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c, 
	const struct res *p)
{

	collection_current_user_privilege_set(req, xml, c);
}

/*
 * RFC 4918, 15.2.
 */
static void
collection_displayname(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_puts(xml, c->displayname);
}

/*
 * caldav-ctag-02, 4.1.
 */
static void
collection_getctag(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{
	char	 buf[22];

	snprintf(buf, sizeof(buf), "%" PRId64, c->ctag);
	kxml_puts(xml, buf);
}

/*
 * RFC 4918, 15.6.
 */
static void
resource_getetag(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	kxml_puts(xml, p->etag);
}

/*
 * RFC 4918, 14.17.
 * This only refers to locks, but provide it anyway.
 */
static void
collection_owner(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{
	struct state	*st = req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_pop(xml);
}

/*
 * RFC 4918, 14.17.
 * This only refers to locks, but provide it anyway.
 */
static void
resource_owner(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	collection_owner(req, xml, c);
}

/*
 * RFC 3744, 4.2.
 */
static void
principal_principal_url(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_pop(xml);
}

/*
 * RFC 3744, 4.2.
 */
static void
collection_principal_url(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	principal_principal_url(req, xml);
}

/*
 * RFC 3744, 4.2.
 * Note that this doesn't specify that this property is on resources, so
 * assume that it is.
 */
static void
resource_principal_url(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	principal_principal_url(req, xml);
}

/*
 * RFC 3744, 4.4.
 * Also, caldav-proxy.txt, 5.2.
 */
static void
principal_group_membership(struct kreq *req, struct kxmlreq *xml)
{
	struct state	*st = req->arg;
	size_t	 	 i;

	for (i = 0; i < st->rprncpl->rproxiesz; i++) {
		if (PROXY_READ == 
		    st->rprncpl->rproxies[i].bits) {
			kxml_push(xml, XML_DAV_HREF);
			kxml_puts(xml, req->pname);
			kxml_putc(xml, '/');
			kxml_puts(xml, st->rprncpl->rproxies[i].name);
			kxml_puts(xml, "/calendar-proxy-read/");
			kxml_pop(xml);
		} else if (PROXY_WRITE == 
			   st->rprncpl->rproxies[i].bits) {
			kxml_push(xml, XML_DAV_HREF);
			kxml_puts(xml, req->pname);
			kxml_putc(xml, '/');
			kxml_puts(xml, st->rprncpl->rproxies[i].name);
			kxml_puts(xml, "/calendar-proxy-write/");
			kxml_pop(xml);
		}
	}
}

/*
 * RFC 4331, 3.
 * This is defined over the collection; the resource handler simply
 * calls through here.
 */
static void
collection_quota_available_bytes(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{
	struct state	*st = req->arg;
	char	 	 buf[32];

	snprintf(buf, sizeof(buf), "%" PRIu64, 
		st->rprncpl->quota_avail);
	kxml_puts(xml, buf);
}

/*
 * RFC 4331, 3.
 */
static void
resource_quota_available_bytes(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	collection_quota_available_bytes(req, xml, c);
}

/*
 * RFC 4331, 4.
 * This is defined over the collection; the resource handler simply
 * calls through here.
 */
static void
collection_quota_used_bytes(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{
	struct state	*st = req->arg;
	char	 	 buf[32];

	snprintf(buf, sizeof(buf), "%"
		PRIu64, st->rprncpl->quota_used);
	kxml_puts(xml, buf);
}

/*
 * RFC 4331, 4.
 */
static void
resource_quota_used_bytes(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	collection_quota_used_bytes(req, xml, c);
}

/*
 * RFC 4918, 15.9.
 * Moreover see RFC 3744, 4 and caldav-proxy.txt.
 */
static void
principal_resourcetype(struct kreq *req, struct kxmlreq *xml)
{

	kxml_pushnull(xml, XML_DAV_PRINCIPAL);
	kxml_pushnull(xml, XML_DAV_COLLECTION);
}

/*
 * RFC 4918, 15.9.
 * This is specific to collections; resource have their own.
 */
static void
collection_resourcetype(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_pushnull(xml, XML_DAV_COLLECTION);
	kxml_pushnull(xml, XML_CALDAV_CALENDAR);
}

/*
 * RFC 4918, 15.9.
 */
static void
resource_resourcetype(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	/* 
	 * Intentionally do nothing. 
	 * This is specified by the RFC (the default).
	 */
}

/* 
 * RFC 6638, 9.1.
 * This seems to be required by iOS, although the standard says the
 * default value is OPAQUE.
 */
static void
collection_schedule_calendar_transp(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_pushnull(xml, XML_CALDAV_OPAQUE);
}

/*
 * RFC 4791, 5.2.3.
 */
static void
collection_supported_calendar_component_set(struct kreq *req,
	struct kxmlreq *xml, const struct coln *c)
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
static void
collection_supported_calendar_data(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	/* Use the canonical type. */
	kxml_pushnullattrs(xml, XML_CALDAV_CALENDAR_DATA, 
		"content-type", kmimetypes[KMIME_TEXT_CALENDAR], 
		"version", "2.0", NULL);
}

/*
 * RFC 4791, 5.2.2.
 */
static void
collection_calendar_timezone(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
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
static void
collection_min_date_time(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_puts(xml, "19700101T000000Z");
}

/*
 * RFC 4791, 9.6.
 */
static void
resource_calendar_data(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	ical_print(p->ical, xml_ical_putc, xml);
}

/*
 * RFC 4918, 15.5.
 * NOTE: there's no clear consensus on what this should return, but this
 * seems reasonable enough and a topical web search indicates the same.
 * The RFC is silent on this matter.
 */
static void
principal_getcontenttype(struct kreq *req, struct kxmlreq *xml)
{

	kxml_puts(xml, "httpd/unix-directory");
}

/*
 * RFC 4918, 15.5.
 * See principal_getcontenttype()
 */
static void
collection_getcontenttype(struct kreq *req, 
	struct kxmlreq *xml, const struct coln *c)
{

	kxml_puts(xml, "httpd/unix-directory");
}

/*
 * RFC 4918, 15.5.
 */
static void
resource_getcontenttype(struct kreq *req, struct kxmlreq *xml, 
	const struct coln *c, const struct res *p)
{

	kxml_puts(xml, kmimetypes[KMIME_TEXT_CALENDAR]);
}

const struct property properties[CALPROP__MAX] = {
	{ /* CALPROP_CALENDAR_COLOR */
	  0, 
	  collection_calendar_colour, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_CALENDAR_DATA */
	  0, 
	  NULL, 
	  resource_calendar_data,
	  NULL }, 
	{ /* CALPROP_CALENDAR_DESCRIPTION */
	  0, 
	  collection_calendar_description, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_CALENDAR_HOME_SET */
	  0, 
	  collection_calendar_home_set, 
	  resource_calendar_home_set,
	  principal_calendar_home_set }, 
	{ /* CALPROP_MIN_DATE_TIME */
	  0, 
	  collection_min_date_time, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_CALENDAR_PROXY_READ_FOR */
	  0, 
	  NULL, 
	  NULL,
	  principal_calendar_proxy_read_for }, 
	{ /* CALPROP_CALENDAR_PROXY_WRITE_FOR */
	  0, 
	  NULL, 
	  NULL,
	  principal_calendar_proxy_write_for }, 
	{ /* CALPROP_CALENDAR_TIMEZONE */
	  0, 
	  collection_calendar_timezone, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_CALENDAR_USER_ADDRESS_SET */
	  0, 
	  collection_calendar_user_address_set, 
	  resource_calendar_user_address_set,
	  principal_calendar_user_address_set }, 
	{ /* CALPROP_CURRENT_USER_PRINCIPAL */
	  0, 
	  collection_current_user_principal, 
	  resource_current_user_principal,
	  principal_current_user_principal }, 
	{ /* CALPROP_CURRENT_USER_PRIVILEGE_SET */
  	  0, 
	  collection_current_user_privilege_set, 
	  resource_current_user_privilege_set,
	  NULL }, 
	{ /* CALPROP_DISPLAYNAME */
	  0, 
	  collection_displayname, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_GETCONTENTTYPE */
	  0, 
	  collection_getcontenttype, 
	  resource_getcontenttype,
	  principal_getcontenttype }, 
	{ /* CALPROP_GETCTAG */
	  0, 
	  collection_getctag, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_GETETAG */
	  0, 
	  NULL, 
	  resource_getetag,
	  NULL }, 
	{ /* CALPROP_GROUP_MEMBER_SET */
	  0, 
	  NULL, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_GROUP_MEMBERSHIP */
	  0, 
	  NULL, 
	  NULL,
	  principal_group_membership }, 
	{ /* CALPROP_OWNER */
	  0, 
	  collection_owner, 
	  resource_owner,
	  NULL }, 
	{ /* CALPROP_PRINCIPAL_URL */
	  0, 
	  collection_principal_url, 
	  resource_principal_url,
	  principal_principal_url }, 
	{ /* CALPROP_QUOTA_AVAILABLE_BYTES */
	  0, 
	  collection_quota_available_bytes, 
	  resource_quota_available_bytes,
	  NULL }, 
	{ /* CALPROP_QUOTA_USED_BYTES */
	  0, 
	  collection_quota_used_bytes, 
	  resource_quota_used_bytes,
	  NULL }, 
	{ /* CALPROP_RESOURCETYPE */
	  0, 
	  collection_resourcetype, 
	  resource_resourcetype,
	  principal_resourcetype }, 
	{ /* CALPROP_SCHEDULE_CALENDAR_TRANSP */
	  0, 
	  collection_schedule_calendar_transp, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_SUPPORTED_CALENDAR_COMPONENT_SET */
	  0, 
	  collection_supported_calendar_component_set, 
	  NULL,
	  NULL }, 
	{ /* CALPROP_SUPPORTED_CALENDAR_DATA */
	  0, 
	  collection_supported_calendar_data, 
	  NULL,
	  NULL }, 
};
