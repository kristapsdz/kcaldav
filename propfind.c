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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "main.h"

/*
 * This converts the request into a CalDav object.
 * We know that the request is a well-formed CalDav object because it
 * appears in the fieldmap and we parsed it during HTTP unpacking.
 * So we really only check its media type.
 */
static struct caldav *
req2caldav(struct kreq *r)
{
	struct caldav	*p;

	if (NULL == r->fieldmap[0]) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	} 

	p = caldav_parse
		(r->fieldmap[0]->val, 
		 r->fieldmap[0]->valsz);
	assert(NULL != p);

	/*fprintf(stderr, "%s", r->fieldmap[0]->val);*/
	return(p);
}

/*
 * Defines the PROPFIND method for a collection (directory).
 * The directory we search is always the root of the request, i.e.,
 * st->path.
 */
static void
propfind_collection(struct kxmlreq *xml, const struct caldav *dav)
{
	size_t	 	 i, nf;
	struct state	*st = xml->req->arg;
	const char	*tag;
	collectionfp	 accepted[PROP__MAX + 1];
	int		 ignore[PROP__MAX + 1];

	fprintf(stderr, "%s: properties: %s\n", 
		st->path, st->prncpl->name);

	memset(accepted, 0, sizeof(accepted));
	memset(ignore, 0, sizeof(ignore));

	accepted[PROP_CALENDAR_HOME_SET] = 
		collection_calendar_home_set;
	accepted[PROP_CALENDAR_USER_ADDRESS_SET] = 
		collection_calendar_user_address_set;
	accepted[PROP_CURRENT_USER_PRINCIPAL] = 
		collection_current_user_principal;
	accepted[PROP_CURRENT_USER_PRIVILEGE_SET] = 
		collection_current_user_privilege_set;
	accepted[PROP_DISPLAYNAME] = 
		collection_displayname;
	accepted[PROP_GETCTAG] = 
		collection_getctag;
	accepted[PROP_OWNER] = 
		collection_owner;
	accepted[PROP_PRINCIPAL_URL] = 
		collection_principal_url;
	accepted[PROP_QUOTA_AVAILABLE_BYTES] = 
		collection_quota_available_bytes;
	accepted[PROP_QUOTA_USED_BYTES] = 
		collection_quota_used_bytes;
	accepted[PROP_RESOURCETYPE] = 
		collection_resourcetype;
	accepted[PROP_SCHEDULE_CALENDAR_TRANSP] = 
		collection_schedule_calendar_transp;
	accepted[PROP_SUPPORTED_CALENDAR_COMPONENT_SET] = 
		collection_supported_calendar_component_set;

	/*
	 * As defined by RFC 4918, we can ignore these.
	 */
	ignore[PROP_GETETAG] = 1;
	ignore[PROP_GETCONTENTTYPE] = 1;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_puts(xml, xml->req->fullpath);
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	for (nf = i = 0; i < dav->propsz; i++) {
		if (NULL == accepted[dav->props[i].key]) {
			if (ignore[dav->props[i].key])
				continue;
			nf++;
			fprintf(stderr, "Unknown collection "
				"property: %s (%s)\n", 
				dav->props[i].name, 
				dav->props[i].xmlns);
			continue;
		}
		tag = calelems[calpropelems[dav->props[i].key]];
		khttp_puts(xml->req, "<X:");
		khttp_puts(xml->req, tag);
		khttp_puts(xml->req, " xmlns:X=\"");
		khttp_puts(xml->req, dav->props[i].xmlns);
		khttp_puts(xml->req, "\">");
		(*accepted[dav->props[i].key])(xml);
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, tag);
		khttp_putc(xml->req, '>');
	}
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_STATUS);
	kxml_puts(xml, "HTTP/1.1 ");
	kxml_puts(xml, khttps[KHTTP_200]);
	kxml_pop(xml);
	kxml_pop(xml);

	if (nf > 0) {
		kxml_push(xml, XML_DAV_PROPSTAT);
		kxml_push(xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (NULL != accepted[dav->props[i].key])
				continue;
			if (ignore[dav->props[i].key])
				continue;
			khttp_puts(xml->req, "<X:");
			khttp_puts(xml->req, dav->props[i].name);
			khttp_puts(xml->req, " xmlns:X=\"");
			khttp_puts(xml->req, dav->props[i].xmlns);
			khttp_puts(xml->req, "\" />");
		}
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_puts(xml, "HTTP/1.1 ");
		kxml_puts(xml, khttps[KHTTP_404]);
		kxml_pop(xml);
		kxml_pop(xml);
	}

	kxml_pop(xml);
}

/*
 * A PROPFIND request for a specified calendar resource (file).
 */
static void
propfind_resource(struct kxmlreq *xml, 
	const struct caldav *dav, const char *name)
{
	struct state	*st = xml->req->arg;
	struct ical	*ical;
	size_t		 i, nf, sz;
	const char	*tag, *pathp;
	char		 buf[PATH_MAX];
	resourcefp	 accepted[PROP__MAX + 1];
	int		 ignore[PROP__MAX + 1];

	memset(accepted, 0, sizeof(accepted));
	memset(ignore, 0, sizeof(ignore));

	accepted[PROP_CALENDAR_DATA] = 
		resource_calendar_data;
	accepted[PROP_CURRENT_USER_PRINCIPAL] = 
		resource_current_user_principal;
	accepted[PROP_CURRENT_USER_PRIVILEGE_SET] = 
		resource_current_user_privilege_set;
	accepted[PROP_GETCONTENTTYPE] = 
		resource_getcontenttype;
	accepted[PROP_GETETAG] = 
		resource_getetag;
	accepted[PROP_OWNER] = 
		resource_owner;
	accepted[PROP_QUOTA_AVAILABLE_BYTES] = 
		resource_quota_available_bytes;
	accepted[PROP_QUOTA_USED_BYTES] = 
		resource_quota_used_bytes;
	accepted[PROP_RESOURCETYPE] = 
		resource_resourcetype;

	ignore[PROP_GETCTAG] = 1;
	ignore[PROP_SCHEDULE_CALENDAR_TRANSP] = 1;
	ignore[PROP_SUPPORTED_CALENDAR_COMPONENT_SET] = 1;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_puts(xml, xml->req->fullpath);
	if (NULL != name)
		kxml_puts(xml, name);
	kxml_pop(xml);

	/* See if we must reconstitute the file to open. */
	if (NULL != name) {
		if (strchr(name, '/')) {
			fprintf(stderr, "%s: insecure path\n", name);
			kxml_push(xml, XML_DAV_STATUS);
			kxml_puts(xml, "HTTP/1.1 ");
			kxml_puts(xml, khttps[KHTTP_403]);
			kxml_pop(xml);
			kxml_pop(xml);
			return;
		}
		pathp = buf;
		strlcpy(buf, st->path, sizeof(buf));
		sz = strlcat(buf, name, sizeof(buf));
		if (sz >= sizeof(buf)) {
			fprintf(stderr, "%s: too long\n", buf);
			kxml_push(xml, XML_DAV_STATUS);
			kxml_puts(xml, "HTTP/1.1 ");
			kxml_puts(xml, khttps[KHTTP_403]);
			kxml_pop(xml);
			kxml_pop(xml);
			return;
		}
	} else 
		pathp = name;

	fprintf(stderr, "%s: properties: %s\n", 
		pathp, st->prncpl->name);

	/* We can only request iCal object, so parse now. */
	if (NULL == (ical = ical_parsefile(pathp))) {
		fprintf(stderr, "%s: parse error\n", pathp);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_puts(xml, "HTTP/1.1 ");
		kxml_puts(xml, khttps[KHTTP_403]);
		kxml_pop(xml);
		kxml_pop(xml);
		return;
	} 

	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);
	for (nf = i = 0; i < dav->propsz; i++) {
		if (NULL == accepted[dav->props[i].key]) {
			if (ignore[dav->props[i].key])
				continue;
			nf++;
			fprintf(stderr, "Unknown resource "
				"property: %s (%s)\n", 
				dav->props[i].name, 
				dav->props[i].xmlns);
			continue;
		}
		tag = calelems[calpropelems[dav->props[i].key]];
		khttp_puts(xml->req, "<X:");
		khttp_puts(xml->req, tag);
		khttp_puts(xml->req, " xmlns:X=\"");
		khttp_puts(xml->req, dav->props[i].xmlns);
		khttp_puts(xml->req, "\">");
		(*accepted[dav->props[i].key])(xml, ical);
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, tag);
		khttp_putc(xml->req, '>');
	}
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_STATUS);
	kxml_puts(xml, "HTTP/1.1 ");
	kxml_puts(xml, khttps[KHTTP_200]);
	kxml_pop(xml);
	kxml_pop(xml);

	/*
	 * If we had any properties for which we're not going to report,
	 * then indicate that now with a 404.
	 */
	if (nf > 0) {
		kxml_push(xml, XML_DAV_PROPSTAT);
		kxml_push(xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (NULL != accepted[dav->props[i].key])
				continue;
			if (ignore[dav->props[i].key])
				continue;
			khttp_puts(xml->req, "<X:");
			khttp_puts(xml->req, dav->props[i].name);
			khttp_puts(xml->req, " xmlns:X=\"");
			khttp_puts(xml->req, dav->props[i].xmlns);
			khttp_puts(xml->req, "\" />");
		}
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_puts(xml, "HTTP/1.1 ");
		kxml_puts(xml, khttps[KHTTP_404]);
		kxml_pop(xml);
		kxml_pop(xml);
	}

	kxml_pop(xml);
	ical_free(ical);
}

void
method_report(struct kreq *r)
{
	struct caldav	*dav;
	struct state	*st = r->arg;
	struct kxmlreq	 xml;
	size_t		 i, sz;
	char		 buf[PATH_MAX];

	if ( ! (PERMS_READ & st->cfg->perms)) {
		fprintf(stderr, "%s: principal does not "
			"have read acccess\n", st->path);
		http_error(r, KHTTP_403);
		return;
	}

	if (NULL == (dav = req2caldav(r)))
		return;

	if (TYPE_CALMULTIGET != dav->type) {
		fprintf(stderr, "%s: unknown request "
			"type\n", st->path);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_415]);
		khttp_body(r);
		caldav_free(dav);
		return;
	}

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, "DAV", "1, 2, "
		"access-control, calendar-access");
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:B", "http://calendarserver.org/ns/",
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);

	if (st->isdir) {
		strlcpy(buf, r->pname, sizeof(buf));
		sz = strlcat(buf, r->fullpath, sizeof(buf));
		/* XXX */
		assert(sz < sizeof(buf));
		assert(sz > 0);
		for (i = 0; i < dav->hrefsz; i++) {
			if (strncmp(dav->hrefs[i], buf, sz)) {
				fprintf(stderr, "%s: does not reside "
					"in root: %s\n", st->path,
					dav->hrefs[i]);
				kxml_push(&xml, XML_DAV_RESPONSE);
				kxml_push(&xml, XML_DAV_HREF);
				kxml_puts(&xml, r->pname);
				kxml_puts(&xml, dav->hrefs[i]);
				kxml_pop(&xml);
				kxml_push(&xml, XML_DAV_STATUS);
				kxml_puts(&xml, "HTTP/1.1 ");
				kxml_puts(&xml, khttps[KHTTP_403]);
				kxml_pop(&xml);
				kxml_pop(&xml);
				continue;
			}
			propfind_resource(&xml, dav, 
				dav->hrefs[i] + sz);
		}
	} else
		propfind_resource(&xml, dav, NULL);

	caldav_free(dav);
	kxml_popall(&xml);
	kxml_close(&xml);
}

/*
 * PROPFIND is used to define properties for calendar collections (i.e.,
 * directories consisting of calendar resources) or resources
 * themselves.
 * Switch on that behaviour here.
 */
void
method_propfind(struct kreq *r)
{
	struct caldav	*dav;
	struct state	*st = r->arg;
	struct kxmlreq	 xml;
	int		 depth;
	DIR		*dirp;
	struct dirent	*dp;

	if ( ! (PERMS_READ & st->cfg->perms)) {
		fprintf(stderr, "%s: principal does not "
			"have read acccess\n", st->path);
		http_error(r, KHTTP_403);
		return;
	}

	if (NULL == (dav = req2caldav(r)))
		return;

	if (TYPE_PROPFIND != dav->type) {
		fprintf(stderr, "%s: bad media\n", st->path);
		http_error(r, KHTTP_415);
		caldav_free(dav);
		return;
	}

	/* Only accept depth of 0 or 1 (not infinity). */
	if (NULL == r->reqmap[KREQU_DEPTH])
		depth = 1;
	else if (0 == strcmp(r->reqmap[KREQU_DEPTH]->val, "0"))
		depth = 0;
	else
		depth = 1;

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_head(r, "DAV", "1, 2, "
		"access-control, calendar-access");
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:B", "http://calendarserver.org/ns/",
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);

	/* Root of request. */
	if (st->isdir) 
		propfind_collection(&xml, dav);
	else
		propfind_resource(&xml, dav, NULL);

	/* Scan directory contents. */
	if (st->isdir && depth) {
		if (NULL == (dirp = opendir(st->path)))
			perror(st->path);
		while (NULL != dirp && NULL != (dp = readdir(dirp))) {
			if ('.' == dp->d_name[0])
				continue;
			if (0 == strcmp(dp->d_name, "kcaldav.conf"))
				continue;
			if (0 == strcmp(dp->d_name, "kcaldav.ctag"))
				continue;
			propfind_resource(&xml, dav, dp->d_name);
		}
		if (NULL != dirp)
			closedir(dirp);
	}

	caldav_free(dav);
	kxml_popall(&xml);
	kxml_close(&xml);
}

