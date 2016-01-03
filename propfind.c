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
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

struct	cbarg {
	struct kxmlreq	*xml;
	const struct caldav *dav;
	const struct coln *c;
};

/*
 * This converts the request into a CalDav object.
 * We know that the request is a well-formed CalDav object because it
 * appears in the fieldmap and we parsed it during HTTP unpacking.
 * So we really only check its media type.
 */
static struct caldav *
req2caldav(struct kreq *r)
{
	struct state	*st = r->arg;

	if (NULL == r->fieldmap[VALID_BODY]) {
		kerrx("%s: failed parse CalDAV XML "
			"in client request", st->prncpl->email);
		http_error(r, KHTTP_400);
		return(NULL);
	} 

	if (KMIME_TEXT_XML != r->fieldmap[VALID_BODY]->ctypepos) {
		kerrx("%s: not CalDAV MIME", st->prncpl->email);
		http_error(r, KHTTP_415);
		return(NULL);
	}

	return(caldav_parse
		(r->fieldmap[VALID_BODY]->val, 
		 r->fieldmap[VALID_BODY]->valsz));
}

static void
propfind_coln(struct kxmlreq *xml, 
	const struct caldav *dav, const struct coln *coln)
{
	struct state	*st = xml->req->arg;
	size_t	 	 i;
	int		 nf;
	enum proptype	 key;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_puts(xml, coln->url);
	if ('\0' != coln->url[0])
		kxml_putc(xml, '/');
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	for (nf = 0, i = 0; i < dav->propsz; i++) {
		if (PROP__MAX == (key = dav->props[i].key)) {
			nf = 1;
			continue;
		} else if (NULL == properties[key].cgetfp)
			continue;
		khttp_puts(xml->req, "<X:");
		khttp_puts(xml->req, dav->props[i].name);
		khttp_puts(xml->req, " xmlns:X=\"");
		khttp_puts(xml->req, dav->props[i].xmlns);
		khttp_puts(xml->req, "\">");
		(*properties[key].cgetfp)(xml, coln);
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, dav->props[i].name);
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
	if (nf) {
		kxml_push(xml, XML_DAV_PROPSTAT);
		kxml_push(xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (PROP__MAX != dav->props[i].key)
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

static void
propfind_resource(struct kxmlreq *xml, 
	const struct caldav *dav, 
	const struct coln *c, const struct res *r)
{
	struct state	*st = xml->req->arg;
	size_t		 i;
	int		 nf;
	enum proptype	 key;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_puts(xml, c->url);
	kxml_putc(xml, '/');
	kxml_puts(xml, r->url);
	kxml_pop(xml);

	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);
	for (nf = 0, i = 0; i < dav->propsz; i++) {
		if (PROP__MAX == (key = dav->props[i].key)) {
			nf = 1;
			continue;
		} else if (NULL == properties[key].rgetfp) 
			continue;
		khttp_puts(xml->req, "<X:");
		khttp_puts(xml->req, dav->props[i].name);
		khttp_puts(xml->req, " xmlns:X=\"");
		khttp_puts(xml->req, dav->props[i].xmlns);
		khttp_puts(xml->req, "\">");
		(*properties[key].rgetfp)(xml, c, r);
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, dav->props[i].name);
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
	if (nf) {
		kxml_push(xml, XML_DAV_PROPSTAT);
		kxml_push(xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (PROP__MAX != dav->props[i].key)
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

static void
propfind_resource_cb(const struct res *r, void *arg)
{
	struct cbarg	*d = arg;

	propfind_resource(d->xml, d->dav, d->c, r);
}

/*
 * Look up a resource within the given collection.
 * This is similar to when we look up our request from the HTTP method
 * (e.g., GET /principal/collection/resource).
 */
static struct res *
propfind_resource_lookup(struct kxmlreq *xml, const char *cp)
{
	struct state	*st = xml->req->arg;
	char		*p, *c, *r;
	int		 rc;
	size_t		 i, sz;
	struct res	*res;

	sz = strlen(xml->req->pname);
	if (strncmp(xml->req->pname, cp, sz)) {
		kerrx("%s: bad script name: %s",
			st->prncpl->email, cp);
		return(NULL);
	}

	/* Parse out paths. */
	cp += sz;
	if ( ! http_paths(cp, &p, &c, &r)) {
		kerrx("%s: path doesn't parse: %s",
			st->prncpl->email, cp);
		return(NULL);
	} else if (strcmp(st->rprncpl->name, p)) {
		kerrx("%s: bad request principal: %s", 
			st->prncpl->email, p);
		goto err;
	}

	/* Look up the collection. */
	for (i = 0; i < st->rprncpl->colsz; i++)
		if (0 == strcmp(st->rprncpl->cols[i].url, c))
			break;

	if (i == st->rprncpl->colsz) {
		kerrx("%s: bad request collection: %s", 
			st->prncpl->email, cp);
		goto err;
	}

	/* Look up the resource. */
	rc = db_resource_load(&res, r, st->rprncpl->cols[i].id);
	if (0 == rc) {
		kerrx("%s: bad request resource: %s", 
			st->prncpl->email, cp);
		goto err;
	} else if (rc < 0)
		goto err;
  
	free(p);
	free(c);
	free(r);
	return(res);
err:
	free(p);
	free(c);
	free(r);
	return(NULL);
}

static void
propfind_prncpl(struct kxmlreq *xml, const struct caldav *dav)
{
	struct state	*st = xml->req->arg;
	size_t	 	 i;
	int		 nf;
	enum proptype	 key;

	kdbg("%s: showing principal collection", st->prncpl->email);

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	for (nf = 0, i = 0; i < dav->propsz; i++) {
		if (PROP__MAX == (key = dav->props[i].key)) {
			nf = 1;
			continue;
		} else if (NULL == properties[key].pgetfp)
			continue;
		khttp_puts(xml->req, "<X:");
		khttp_puts(xml->req, dav->props[i].name);
		khttp_puts(xml->req, " xmlns:X=\"");
		khttp_puts(xml->req, dav->props[i].xmlns);
		khttp_puts(xml->req, "\">");
		(*properties[key].pgetfp)(xml);
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, dav->props[i].name);
		khttp_putc(xml->req, '>');
	}
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_STATUS);
	kxml_puts(xml, "HTTP/1.1 ");
	kxml_puts(xml, khttps[KHTTP_200]);
	kxml_pop(xml);
	kxml_pop(xml);

	if (nf) {
		kxml_push(xml, XML_DAV_PROPSTAT);
		kxml_push(xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (PROP__MAX != dav->props[i].key)
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
 * This handler is invoked for special collections defined for proxy
 * behaviour, i.e., calendar-proxy-read or write.
 * This functionality is documented in caldav-proxy.txt.
 */
static void
propfind_proxy(struct kxmlreq *xml, 
	const struct caldav *dav, const char *proxy)
{
	struct state	*st = xml->req->arg;
	size_t	 	 i, j, nf;
	int		 bits;
	enum xml	 type;

	kdbg("%s: showing proxy collection: %s", 
		st->prncpl->email, proxy);

	/* Are we asking for writers or readers? */
	type = 0 == strcmp(proxy, "calendar-proxy-write") ?
	       XML_CALDAVSERV_PROXY_WRITE : XML_CALDAVSERV_PROXY_READ;
	bits = 0 == strcmp(proxy, "calendar-proxy-write") ?
	       PROXY_WRITE : PROXY_READ;

	/* Response is for the requested resource. */
	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, xml->req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_puts(xml, proxy);
	kxml_putc(xml, '/');
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	for (nf = 0, i = 0; i < dav->propsz; i++) {
		/* 
		 * We honour the resource type and group member set
		 * property requests.
		 */
		if (PROP_RESOURCETYPE == dav->props[i].key) {
			kxml_pushnull(xml, XML_DAV_PRINCIPAL);
			kxml_pushnull(xml, type);
			continue;
		} else if (PROP_GROUP_MEMBER_SET != dav->props[i].key) {
			nf = 1;
			continue;
		}
		/*
		 * RFC 3744, 4.3.
		 * Specifically, caldav-proxy.txt, 5.2.
		 */
		khttp_puts(xml->req, "<X:");
		khttp_puts(xml->req, dav->props[i].name);
		khttp_puts(xml->req, " xmlns:X=\"");
		khttp_puts(xml->req, dav->props[i].xmlns);
		khttp_puts(xml->req, "\">");

		/* All of the readers or writers. */
		for (j = 0; j < st->rprncpl->proxiesz; j++) {
			if ( ! (bits & st->rprncpl->proxies[j].bits))
				continue;
			kxml_push(xml, XML_DAV_HREF);
			kxml_puts(xml, xml->req->pname);
			kxml_putc(xml, '/');
			kxml_puts(xml, st->rprncpl->proxies[j].name);
			kxml_putc(xml, '/');
			kxml_pop(xml);
		}

		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, dav->props[i].name);
		khttp_putc(xml->req, '>');
	}
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_STATUS);
	kxml_puts(xml, "HTTP/1.1 ");
	kxml_puts(xml, khttps[KHTTP_200]);
	kxml_pop(xml);
	kxml_pop(xml);

	/* Now unsupported elements. */
	if (nf) {
		kxml_push(xml, XML_DAV_PROPSTAT);
		kxml_push(xml, XML_DAV_PROP);
		for (i = 0; i < dav->propsz; i++) {
			if (PROP_RESOURCETYPE == dav->props[i].key ||
			    PROP_GROUP_MEMBER_SET == dav->props[i].key)
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
 * Given a "directory" (i.e., a collection), get all the properties for
 * the collection and, if a "depth" is specified, the properties of all
 * contained resources.
 * This can handle both calendar collections, which contain calendar
 * resources, and principal collections that contain calendars.
 */
static void
propfind_directory(struct kxmlreq *xml, 
	const struct caldav *dav, const struct coln *c)
{
	size_t		 i, depth;
	struct state	*st = xml->req->arg;
	struct cbarg	 carg;

	if (NULL == xml->req->reqmap[KREQU_DEPTH])
		depth = 1;
	else if (0 == strcmp(xml->req->reqmap[KREQU_DEPTH]->val, "0"))
		depth = 0;
	else
		depth = 1;

	kdbg("%s: responding to PROPFIND, depth: %zu", 
		st->prncpl->email, depth);

	/* Look up the root collection/principal. */
	if (NULL == c)
		propfind_prncpl(xml, dav);
	else
		propfind_coln(xml, dav, c);

	if (0 == depth)
		return;

	/* 
	 * Now we have infinite depth: look up children: resources for a
	 * collection, collections for a principal. 
	 */
	if (NULL != c) {
		carg.xml = xml;
		carg.c = c;
		carg.dav = dav;
		db_collection_resources
			(propfind_resource_cb, c->id, &carg);
		return;
	} 

	propfind_proxy(xml, dav, "calendar-proxy-read");
	propfind_proxy(xml, dav, "calendar-proxy-write");
	for (i = 0; i < st->rprncpl->colsz; i++)
		propfind_coln(xml, dav, &st->rprncpl->cols[i]);
}

/*
 * Given a list of collections or resources in the "href" object of the
 * XML request, get their properties.
 * This occurs within a multi-response.
 */
static void
propfind_list(struct kxmlreq *xml, const struct caldav *dav)
{
	struct state	*st = xml->req->arg;
	size_t		 i, j;
	struct res	*res;
	char		*cp;

	for (i = 0; i < dav->hrefsz; i++) {
		res = propfind_resource_lookup(xml, dav->hrefs[i]);
		if (NULL != res) {
			for (j = 0; j < st->rprncpl->colsz; j++) {
				if (res->collection == st->rprncpl->cols[j].id)
					break;
			}
			assert(j < st->rprncpl->colsz);
			propfind_resource(xml, dav, &st->rprncpl->cols[j], res);
			res_free(res);
			continue;
		}
		kerrx("%s: bad resource request: %s",
			st->prncpl->email, dav->hrefs[i]);
		kxml_push(xml, XML_DAV_RESPONSE);
		kxml_push(xml, XML_DAV_HREF);
		kxml_puts(xml, xml->req->pname);
		/* Remember to URL encode! */
		cp = kutil_urlencode(dav->hrefs[i]);
		kxml_puts(xml, cp);
		free(cp);
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_puts(xml, "HTTP/1.1 ");
		kxml_puts(xml, khttps[KHTTP_403]);
		kxml_pop(xml);
		kxml_pop(xml);
	}
}

/*
 * The REPORT method is for calendar collections and reosurces.
 * It's defined by RFC 4791, section 7.1.
 */
void
method_report(struct kreq *r)
{
	struct caldav	*dav;
	struct state	*st = r->arg;
	struct kxmlreq	 xml;
	struct res	*res;
	int		 rc;

	if (NULL == st->cfg) {
		/* Disallow non-calendar collection. */
		kerrx("%s: REPORT of non-calendar "
			"collection", st->prncpl->email);
		http_error(r, KHTTP_403);
		return;
	} else if (NULL == (dav = req2caldav(r)))
		return;

	if (TYPE_CALMULTIGET != dav->type &&
		 TYPE_CALQUERY != dav->type) {
		kerrx("%s: unknown request type", st->prncpl->email);
		http_error(r, KHTTP_415);
		caldav_free(dav);
		return;
	}

	if ('\0' != st->resource[0]) {
		rc = db_resource_load(&res, st->resource, st->cfg->id);
		if (rc < 0) {
			kerrx("%s: failed resource", st->prncpl->email);
			http_error(r, KHTTP_505);
			return;
		} else if (0 == rc) {
			kerrx("%s: bad resource", st->prncpl->email);
			http_error(r, KHTTP_404);
			return;
		}
	} else
		res = NULL;

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	/* FIXME: remove this? */
	khttp_head(r, "DAV", "1, access-control, "
		"calendar-access, calendar-proxy");
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:B", "http://calendarserver.org/ns/",
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);

	if (NULL == res) {
		if (TYPE_CALMULTIGET == dav->type)
			propfind_list(&xml, dav);
		else if (TYPE_CALQUERY == dav->type)
			propfind_directory(&xml, dav, st->cfg);
	} else
		propfind_resource(&xml, dav, st->cfg, res);

	caldav_free(dav);
	kxml_popall(&xml);
	kxml_close(&xml);
}

/*
 * PROPFIND is defined by RFC 4918, section 9.1.
 * We accept PROPFIND for resources (calendar resources), collections
 * (calendar collections), and the base principal URL (non-calendar
 * collection).
 * We also accept for meta-collections, e.g., proxy paths.
 */
void
method_propfind(struct kreq *r)
{
	struct caldav	*dav;
	struct state	*st = r->arg;
	struct kxmlreq	 xml;
	struct res	*res;
	int		 rc;

	if (NULL == (dav = req2caldav(r)))
		return;

	if (TYPE_PROPFIND != dav->type) {
		kerrx("%s: unknown request type", st->prncpl->email);
		http_error(r, KHTTP_415);
		caldav_free(dav);
		return;
	} else if (st->cfg && st->resource[0]) {
		/* Try to load the resource from the database. */
		rc = db_resource_load
			(&res, st->resource, st->cfg->id);
		if (rc < 0) {
			http_error(r, KHTTP_403);
			return;
		} else if (0 == rc) {
			http_error(r, KHTTP_404);
			return;
		}
	} else if (NULL == st->cfg && st->resource[0]) {
		/* Resource in non-calendar collection. */
		http_error(r, KHTTP_403);
		return;
	} else 
		res = NULL;

	/*
	 * If we're a resource, our resource is now loaded into "res".
	 * If we're a collection, it's now loaded into "cfg".
	 * If we're neither AND we have one of the meta-collections,
	 * then route to the relevant handler.
	 * Otherwise, we're a root (principal) request.
	 */
	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_head(r, "DAV", "1, access-control, "
		"calendar-access, calendar-proxy");
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:B", "http://calendarserver.org/ns/",
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);

	if (NULL == st->cfg && 
	    (0 == strcmp(st->collection, "calendar-proxy-read") ||
	     0 == strcmp(st->collection, "calendar-proxy-write")))
		/* Proxy requests. */
		propfind_proxy(&xml, dav, st->collection);
	else if (NULL == res && NULL != st->cfg)
		/* Collections. */
		propfind_directory(&xml, dav, st->cfg);
	else if (NULL != res && NULL != st->cfg)
		/* Resources. */
		propfind_resource(&xml, dav, st->cfg, res);
	else 
		/* Root (principal). */
		propfind_directory(&xml, dav, NULL);

	caldav_free(dav);
	kxml_popall(&xml);
	kxml_close(&xml);
}

