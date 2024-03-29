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
 * This converts the request into a CalDav object.
 * We know that the request is a well-formed CalDav object because it
 * appears in the fieldmap and we parsed it during HTTP unpacking.
 * So we really only check its media type.
 */
static struct caldav *
req2caldav(struct kreq *r, enum kmime *mime)
{
	struct state	*st = r->arg;

	if (r->fieldmap[VALID_BODY] == NULL) {
		kutil_warnx(r, st->prncpl->name,
			"failed CalDAV parse");
		http_error(r, KHTTP_400);
		return NULL;
	} 

	if (r->fieldmap[VALID_BODY]->ctypepos != KMIME_TEXT_XML &&
	    r->fieldmap[VALID_BODY]->ctypepos != KMIME_APP_XML) {
		kutil_warnx(r, st->prncpl->name,
			"bad CalDAV MIME type");
		http_error(r, KHTTP_415);
		return NULL;
	}

	*mime = r->fieldmap[VALID_BODY]->ctypepos;

	/* This shouldn't fail now. */

	return caldav_parse
		(r->fieldmap[VALID_BODY]->val, 
		 r->fieldmap[VALID_BODY]->valsz, NULL);
}

/*
 * Handle the PROPPATCH method, which is in its most general way
 * described in RFC 4918, section 9.2, accepting the "propertyupdate"
 * CalDAV XML (ibid., 14.9).
 */
void
method_proppatch(struct kreq *r)
{
	struct caldav	*dav;
	struct state	*st = r->arg;
	struct kxmlreq	 xml;
	size_t		 nf, df, bf, i;
	int		 accepted[CALPROP__MAX + 1];
	struct coln	 cfg;
	enum kmime	 mime;

	if (st->cfg == NULL) {
		kutil_warnx(r, st->prncpl->name, 
			"PROPPATCH of non-calendar collection");
		http_error(r, KHTTP_403);
		return;
	} else if ((dav = req2caldav(r, &mime)) == NULL)
		return;

	cfg = *st->cfg;

	memset(accepted, 0, sizeof(accepted));
	accepted[CALPROP_CALENDAR_COLOR] = 1;
	accepted[CALPROP_CALENDAR_DESCRIPTION] = 1;
	accepted[CALPROP_DISPLAYNAME] = 1;

	if (CALREQTYPE_PROPERTYUPDATE != dav->type) {
		kutil_warnx(r, st->prncpl->name, 
			"unknown PROPPATCH request type");
		http_error(r, KHTTP_415);
		caldav_free(dav);
		return;
	}

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[mime]);
	khttp_head(r, "DAV", "1, access-control, "
		"calendar-access, calendar-proxy");
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_prologue(&xml);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:B", "http://calendarserver.org/ns/",
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);

	/*
	 * Begin by looking over all of our properties and setting.
	 * If we don't understand a property, skip it.
	 * If we understand it but the value wasn't valid, skip it.
	 * Otherwise, set into a temporary "struct config".
	 */

	kxml_push(&xml, XML_DAV_PROPSTAT);
	kxml_push(&xml, XML_DAV_PROP);

	for (nf = df = bf = i = 0; i < dav->propsz; i++) {
		if (!accepted[dav->props[i].key]) {
			nf++;
			bf += dav->props[i].valid < 0;
			continue;
		} else if (dav->props[i].valid < 0) {
			bf++;
			continue;
		}

		df++;
		switch (dav->props[i].key) {
		case CALPROP_DISPLAYNAME:
			cfg.displayname = dav->props[i].val;
			kutil_dbg(r, st->prncpl->name,
				"display name modified");
			break;
		case CALPROP_CALENDAR_COLOR:
			cfg.colour = dav->props[i].val;
			kutil_dbg(r, st->prncpl->name,
				"calendar colour modified");
			break;
		case CALPROP_CALENDAR_DESCRIPTION:
			cfg.description = dav->props[i].val;
			kutil_dbg(r, st->prncpl->name,
				"calendar description modified");
			break;
		default:
			abort();
		}
	}

	kxml_pop(&xml);
	kxml_push(&xml, XML_DAV_STATUS);
	kxml_puts(&xml, "HTTP/1.1 ");
	kxml_puts(&xml, khttps[KHTTP_200]);
	kxml_pop(&xml);
	kxml_pop(&xml);

	/*
	 * In this event, we didn't understand one or more properties.
	 * Serialise these as error 404 according to the spec.
	 */

	if (nf > 0) {
		kxml_push(&xml, XML_DAV_PROPSTAT);
		kxml_push(&xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (accepted[dav->props[i].key])
				continue;
			khttp_puts(r, "<X:");
			khttp_puts(r, dav->props[i].name);
			khttp_puts(r, " xmlns:X=\"");
			khttp_puts(r, dav->props[i].xmlns);
			khttp_puts(r, "\" />");
		}
		kxml_pop(&xml);
		kxml_push(&xml, XML_DAV_STATUS);
		kxml_puts(&xml, "HTTP/1.1 ");
		kxml_puts(&xml, khttps[KHTTP_404]);
		kxml_pop(&xml);
		kxml_pop(&xml);
	}

	/*
	 * Bad things: we're asked to set invalid data.
	 * Use code 409 as specified by RFC 4918, 9.2.1.
	 */

	if (bf > 0) {
		kxml_push(&xml, XML_DAV_PROPSTAT);
		kxml_push(&xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (dav->props[i].valid >= 0)
				continue;
			khttp_puts(r, "<X:");
			khttp_puts(r, dav->props[i].name);
			khttp_puts(r, " xmlns:X=\"");
			khttp_puts(r, dav->props[i].xmlns);
			khttp_puts(r, "\" />");
		}
		kxml_pop(&xml);
		kxml_push(&xml, XML_DAV_STATUS);
		kxml_puts(&xml, "HTTP/1.1 ");
		kxml_puts(&xml, khttps[KHTTP_409]);
		kxml_pop(&xml);
		kxml_pop(&xml);
	}

	kxml_popall(&xml);
	kxml_close(&xml);

	/* FIXME: do this first to catch any HTTP 505 errors. */

	if (df != 0 && !db_collection_update(&cfg, st->rprncpl))
		kutil_errx_noexit(r, st->prncpl->name, 
			"cannot update collection");

	caldav_free(dav);
}

