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
#include <sys/quota.h>

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
collection_calendar_home_set(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, st->rpath);
	kxml_pop(xml);
}

void
collection_current_user_principal(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_puts(xml, st->prncpl->homedir);
	kxml_pop(xml);
}

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

void
collection_displayname(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_puts(xml, st->cfg->displayname);
}

void
collection_getctag(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;
	char		 ctag[65];

	ctagcache_get(st->ctagfile, ctag);
	kxml_puts(xml, ctag);
}

void
collection_owner(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, st->rpath);
	kxml_pop(xml);
}

void
collection_principal_url(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, st->rpath);
	kxml_pop(xml);
}

void
collection_quota_available_bytes(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;
	char	 	 buf[32];

	snprintf(buf, sizeof(buf), "%lld", st->cfg->bytesavail);
	kxml_puts(xml, buf);
}

void
collection_quota_used_bytes(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;
	char	 	 buf[32];

	snprintf(buf, sizeof(buf), "%lld", st->cfg->bytesused);
	kxml_puts(xml, buf);
}

void
collection_resourcetype(struct kxmlreq *xml)
{

	kxml_pushnull(xml, XML_DAV_COLLECTION);
	kxml_pushnull(xml, XML_CALDAV_CALENDAR);
}

void
collection_user_address_set(struct kxmlreq *xml)
{
	struct state	*st = xml->req->arg;

	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, "mailto:");
	kxml_puts(xml, st->cfg->emailaddress);
	kxml_pop(xml);
}
