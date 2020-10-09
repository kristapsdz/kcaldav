/*	$Id$ */
/*
 * Copyright (c) 2015, 2018 Kristaps Dzonsons <kristaps@bsd.lv>
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

struct	cbarg {
	struct kxmlreq		*xml;
	struct kreq		*req;
	const struct caldav	*dav;
	const struct coln	*c;
};

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
		kutil_info(r, st->prncpl->name,
			"failed CalDAV parse");
		http_error(r, KHTTP_400);
		return NULL;
	} 

	if (r->fieldmap[VALID_BODY]->ctypepos != KMIME_TEXT_XML &&
	    r->fieldmap[VALID_BODY]->ctypepos != KMIME_APP_XML) {
		kutil_info(r, st->prncpl->name,
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

static void
propfind_coln(struct kreq *r, struct kxmlreq *xml, 
	const struct caldav *dav, const struct coln *coln)
{
	struct state		*st = r->arg;
	size_t			 i;
	int			 nf;
	enum calproptype	 key;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, r->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_puts(xml, coln->url);
	if (coln->url[0] != '\0')
		kxml_putc(xml, '/');
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	for (nf = 0, i = 0; i < dav->propsz; i++) {
		if ((key = dav->props[i].key) == CALPROP__MAX) {
			nf = 1;
			continue;
		} else if (properties[key].cgetfp == NULL)
			continue;
		khttp_puts(r, "<X:");
		khttp_puts(r, dav->props[i].name);
		khttp_puts(r, " xmlns:X=\"");
		khttp_puts(r, dav->props[i].xmlns);
		khttp_puts(r, "\">");
		(*properties[key].cgetfp)(r, xml, coln);
		khttp_puts(r, "</X:");
		khttp_puts(r, dav->props[i].name);
		khttp_putc(r, '>');
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
			if (dav->props[i].key != CALPROP__MAX)
				continue;
			khttp_puts(r, "<X:");
			khttp_puts(r, dav->props[i].name);
			khttp_puts(r, " xmlns:X=\"");
			khttp_puts(r, dav->props[i].xmlns);
			khttp_puts(r, "\" />");
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
propfind_resource(struct kreq *r,
	struct kxmlreq *xml, const struct caldav *dav, 
	const struct coln *c, const struct res *res)
{
	struct state		*st = r->arg;
	size_t			 i;
	int			 nf;
	enum calproptype	 key;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, r->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_puts(xml, c->url);
	kxml_putc(xml, '/');
	kxml_puts(xml, res->url);
	kxml_pop(xml);

	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);
	for (nf = 0, i = 0; i < dav->propsz; i++) {
		if ((key = dav->props[i].key) == CALPROP__MAX) {
			nf = 1;
			continue;
		} else if (properties[key].rgetfp == NULL)
			continue;
		khttp_puts(r, "<X:");
		khttp_puts(r, dav->props[i].name);
		khttp_puts(r, " xmlns:X=\"");
		khttp_puts(r, dav->props[i].xmlns);
		khttp_puts(r, "\">");
		(*properties[key].rgetfp)(r, xml, c, res);
		khttp_puts(r, "</X:");
		khttp_puts(r, dav->props[i].name);
		khttp_putc(r, '>');
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
			if (dav->props[i].key != CALPROP__MAX)
				continue;
			khttp_puts(r, "<X:");
			khttp_puts(r, dav->props[i].name);
			khttp_puts(r, " xmlns:X=\"");
			khttp_puts(r, dav->props[i].xmlns);
			khttp_puts(r, "\" />");
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

	propfind_resource(d->req, d->xml, d->dav, d->c, r);
}

/*
 * Look up a resource within the given collection.
 * This is similar to when we look up our request from the HTTP method
 * (e.g., GET /principal/collection/resource).
 * Return <0 on fatal error, 0 on error, >0 on success.
 */
static int
propfind_resource_lookup(struct kreq *r, struct kxmlreq *xml, 
	const char *cp, struct res **p)
{
	struct state	*st = r->arg;
	char		*prncp = NULL, *comp = NULL, *res = NULL;
	int		 rc = 0;
	size_t		 i, sz;

	*p = NULL;

	sz = strlen(r->pname);
	if (strncmp(r->pname, cp, sz)) {
		kutil_info(r, st->prncpl->name,
			"bad script name");
		return 0;
	}

	/* Parse out paths. */

	cp += sz;

	if (!http_paths(cp, &prncp, &comp, &res)) {
		kutil_info(r, st->prncpl->name,
			"bad request path");
		goto err;
	} else if (strcmp(st->rprncpl->name, prncp)) {
		kutil_info(r, st->prncpl->name,
			"bad request principal: %s", prncp);
		goto err;
	}

	/* Look up the collection. */

	for (i = 0; i < st->rprncpl->colsz; i++)
		if (strcmp(st->rprncpl->cols[i].url, comp) == 0)
			break;

	if (i == st->rprncpl->colsz) {
		kutil_info(r, st->prncpl->name,
			"bad request collection: %s", comp);
		goto err;
	}

	rc = db_resource_load(p, res, st->rprncpl->cols[i].id);

	if (rc == 0)
		kutil_info(r, st->prncpl->name,
			"resource not found: %s", res);
	else if (rc < 0)
		kutil_warnx(r, st->prncpl->name,
			"cannot load resource: %s", res);
err:
	free(prncp);
	free(comp);
	free(res);
	return rc;
}

static void
propfind_prncpl(struct kreq *req, struct kxmlreq *xml, 
	const struct caldav *dav)
{
	struct state		*st = req->arg;
	size_t			 i;
	int			 nf;
	enum calproptype	 key;

	kdbg("%s: showing principal collection", st->prncpl->email);

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_puts(xml, req->pname);
	kxml_putc(xml, '/');
	kxml_puts(xml, st->rprncpl->name);
	kxml_putc(xml, '/');
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	for (nf = 0, i = 0; i < dav->propsz; i++) {
		if (CALPROP__MAX == (key = dav->props[i].key)) {
			nf = 1;
			continue;
		} else if (NULL == properties[key].pgetfp)
			continue;
		khttp_puts(req, "<X:");
		khttp_puts(req, dav->props[i].name);
		khttp_puts(req, " xmlns:X=\"");
		khttp_puts(req, dav->props[i].xmlns);
		khttp_puts(req, "\">");
		(*properties[key].pgetfp)(req, xml);
		khttp_puts(req, "</X:");
		khttp_puts(req, dav->props[i].name);
		khttp_putc(req, '>');
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
			if (CALPROP__MAX != dav->props[i].key)
				continue;
			khttp_puts(req, "<X:");
			khttp_puts(req, dav->props[i].name);
			khttp_puts(req, " xmlns:X=\"");
			khttp_puts(req, dav->props[i].xmlns);
			khttp_puts(req, "\" />");
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
propfind_proxy(struct kreq *req, struct kxmlreq *xml, 
	const struct caldav *dav, const char *proxy)
{
	struct state	*st = req->arg;
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
	kxml_puts(xml, req->pname);
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
		if (CALPROP_RESOURCETYPE == dav->props[i].key) {
			kxml_pushnull(xml, XML_DAV_PRINCIPAL);
			kxml_pushnull(xml, type);
			continue;
		} else if (CALPROP_GROUP_MEMBER_SET != dav->props[i].key) {
			nf = 1;
			continue;
		}
		/*
		 * RFC 3744, 4.3.
		 * Specifically, caldav-proxy.txt, 5.2.
		 */
		khttp_puts(req, "<X:");
		khttp_puts(req, dav->props[i].name);
		khttp_puts(req, " xmlns:X=\"");
		khttp_puts(req, dav->props[i].xmlns);
		khttp_puts(req, "\">");

		/* All of the readers or writers. */
		for (j = 0; j < st->rprncpl->proxiesz; j++) {
			if (bits != st->rprncpl->proxies[j].bits)
				continue;
			kxml_push(xml, XML_DAV_HREF);
			kxml_puts(xml, req->pname);
			kxml_putc(xml, '/');
			kxml_puts(xml, st->rprncpl->proxies[j].name);
			kxml_putc(xml, '/');
			kxml_pop(xml);
		}

		khttp_puts(req, "</X:");
		khttp_puts(req, dav->props[i].name);
		khttp_putc(req, '>');
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
			if (CALPROP_RESOURCETYPE == dav->props[i].key ||
			    CALPROP_GROUP_MEMBER_SET == dav->props[i].key)
				continue;
			khttp_puts(req, "<X:");
			khttp_puts(req, dav->props[i].name);
			khttp_puts(req, " xmlns:X=\"");
			khttp_puts(req, dav->props[i].xmlns);
			khttp_puts(req, "\" />");
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
propfind_directory(struct kreq *req, struct kxmlreq *xml, 
	const struct caldav *dav, const struct coln *c)
{
	struct state	*st = req->arg;
	size_t		 i, depth;
	struct cbarg	 carg;

	if (NULL == req->reqmap[KREQU_DEPTH])
		depth = 1;
	else if (0 == strcmp(req->reqmap[KREQU_DEPTH]->val, "0"))
		depth = 0;
	else
		depth = 1;

	kdbg("%s: responding to PROPFIND, depth: %zu", 
		st->prncpl->email, depth);

	/* Look up the root collection/principal. */

	if (NULL == c)
		propfind_prncpl(req, xml, dav);
	else
		propfind_coln(req, xml, dav, c);

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
		carg.req = req;
		db_collection_resources
			(propfind_resource_cb, c->id, &carg);
		return;
	} 

	propfind_proxy(req, xml, dav, "calendar-proxy-read");
	propfind_proxy(req, xml, dav, "calendar-proxy-write");
	for (i = 0; i < st->rprncpl->colsz; i++)
		propfind_coln(req, xml, dav, 
			&st->rprncpl->cols[i]);
}

/*
 * Given a list of collections or resources in the "href" object of the
 * XML request, get their properties.
 * This occurs within a multi-response.
 */
static void
propfind_list(struct kreq *req, struct kxmlreq *xml, 
	const struct caldav *dav)
{
	struct state	*st = req->arg;
	size_t		 i, j;
	struct res	*res;
	char		*cp;
	int		 rc;

	for (i = 0; i < dav->hrefsz; i++) {
		rc = propfind_resource_lookup
			(req, xml, dav->hrefs[i], &res);
		if (rc > 0) {
			assert(res != NULL);
			for (j = 0; j < st->rprncpl->colsz; j++)
				if (res->collection == 
				    st->rprncpl->cols[j].id)
					break;
			assert(j < st->rprncpl->colsz);
			propfind_resource(req, xml, dav, 
				&st->rprncpl->cols[j], res);
			db_resource_free(res);
			continue;
		} 

		kxml_push(xml, XML_DAV_RESPONSE);
		kxml_push(xml, XML_DAV_HREF);
		kxml_puts(xml, req->pname);
		/* Remember to URL encode! */
		cp = khttp_urlencode(dav->hrefs[i]);
		kxml_puts(xml, cp);
		free(cp);
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_puts(xml, "HTTP/1.1 ");
		kxml_puts(xml, khttps[rc < 0 ? KHTTP_505 : KHTTP_403]);
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
	struct res	*res = NULL;
	enum kmime	 mime;
	int		 rc;

	if (st->cfg == NULL) {
		kutil_info(r, st->prncpl->name, 
			"REPORT of non-calendar collection");
		http_error(r, KHTTP_403);
		return;
	} else if ((dav = req2caldav(r, &mime)) == NULL)
		return;

	if (dav->type != CALREQTYPE_CALMULTIGET &&
	    dav->type != CALREQTYPE_CALQUERY) {
		kutil_info(r, st->prncpl->name, 
			"unknown REPORT request type");
		http_error(r, KHTTP_415);
		caldav_free(dav);
		return;
	}

	if (st->resource[0] != '\0') {
		rc = db_resource_load(&res, st->resource, st->cfg->id);
		if (rc < 0) {
			kutil_warnx(r, st->prncpl->name,
				"cannot load resource: %s", 
				st->resource);
			http_error(r, KHTTP_505);
			return;
		} else if (0 == rc) {
			kutil_warnx(r, st->prncpl->name,
				"REPORT for unknown resource: %s", 
				st->resource);
			http_error(r, KHTTP_404);
			return;
		}
	} 

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);

	/* FIXME: remove this? */

	khttp_head(r, "DAV", "1, access-control, "
		"calendar-access, calendar-proxy");
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[mime]);
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_prologue(&xml);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:B", "http://calendarserver.org/ns/",
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);

	if (res == NULL) {
		if (dav->type == CALREQTYPE_CALMULTIGET)
			propfind_list(r, &xml, dav);
		else if (dav->type == CALREQTYPE_CALQUERY)
			propfind_directory(r, &xml, dav, st->cfg);
	} else
		propfind_resource(r, &xml, dav, st->cfg, res);

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
	struct res	*res = NULL;
	int		 rc;
	enum kmime	 mime;

	if ((dav = req2caldav(r, &mime)) == NULL)
		return;

	if (dav->type != CALREQTYPE_PROPFIND) {
		kutil_info(r, st->prncpl->name, 
			"unknown PROPFIND request type");
		http_error(r, KHTTP_415);
		caldav_free(dav);
		return;
	}
	
	if (st->cfg != NULL && st->resource[0] != '\0') {
		rc = db_resource_load(&res,
			st->resource, st->cfg->id);
		if (rc < 0) {
			kutil_warnx(r, st->prncpl->name,
				"cannot load resource: %s", 
				st->resource);
			http_error(r, KHTTP_505);
			return;
		} else if (rc == 0) {
			kutil_warnx(r, st->prncpl->name,
				"PROPFIND for unknown resource: %s", 
				st->resource);
			http_error(r, KHTTP_404);
			return;
		}
	} else if (st->cfg == NULL && st->resource[0] != '\0') {
		kutil_info(r, st->prncpl->name, 
			"PROPFIND from non-calendar collection");
		http_error(r, KHTTP_403);
		return;
	}

	/*
	 * If we're a resource, our resource is now loaded into "res".
	 * If we're a collection, it's now loaded into "cfg".
	 * If we're neither AND we have one of the meta-collections,
	 * then route to the relevant handler.
	 * Otherwise, we're a root (principal) request.
	 * After this point we can't change our HTTP status.
	 * All errors need to reported within the multi-status.
	 */

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

	if (st->cfg == NULL && 
	    (strcmp(st->collection, "calendar-proxy-read") == 0 ||
	     strcmp(st->collection, "calendar-proxy-write") == 0))
		/* Proxy requests. */
		propfind_proxy(r, &xml, dav, st->collection);
	else if (res == NULL && st->cfg != NULL)
		/* Collections. */
		propfind_directory(r, &xml, dav, st->cfg);
	else if (res != NULL && st->cfg != NULL)
		/* Resources. */
		propfind_resource(r, &xml, dav, st->cfg, res);
	else 
		/* Root (principal). */
		propfind_directory(r, &xml, dav, NULL);

	caldav_free(dav);
	kxml_popall(&xml);
	kxml_close(&xml);
}
