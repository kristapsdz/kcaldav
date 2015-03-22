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
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"

static	const char *const kmethods[KMETHOD__MAX + 1] = {
	"acl", /* KMETHOD_ACL */
	"connect", /* KMETHOD_CONNECT */
	"copy", /* KMETHOD_COPY */
	"delete", /* KMETHOD_DELETE */
	"get", /* KMETHOD_GET */
	"head", /* KMETHOD_HEAD */
	"lock", /* KMETHOD_LOCK */
	"mkcalendar", /* KMETHOD_MKCALENDAR */
	"mkcol", /* KMETHOD_MKCOL */
	"move", /* KMETHOD_MOVE */
	"options", /* KMETHOD_OPTIONS */
	"post", /* KMETHOD_POST */
	"propfind", /* KMETHOD_PROPFIND */
	"proppatch", /* KMETHOD_PROPPATCH */
	"put", /* KMETHOD_PUT */
	"report", /* KMETHOD_REPORT */
	"trace", /* KMETHOD_TRACE */
	"unlock", /* KMETHOD_UNLOCK */
	"unknown", /* KMETHOD__MAX */
};

enum	xml {
	XML_CALDAV_CALENDAR,
	XML_CALDAV_CALENDAR_HOME_SET,
	XML_DAV_COLLECTION,
	XML_DAV_DISPLAYNAME,
	XML_DAV_GETETAG,
	XML_DAV_HREF,
	XML_DAV_MULTISTATUS,
	XML_DAV_PRINCIPAL_URL,
	XML_DAV_PROP,
	XML_DAV_PROPSTAT,
	XML_DAV_RESOURCETYPE,
	XML_DAV_RESPONSE,
	XML_DAV_STATUS,
	XML_DAV_UNAUTHENTICATED,
	XML__MAX
};

static	const enum xml xmlprops[PROP__MAX] = {
	XML_CALDAV_CALENDAR_HOME_SET, /* PROP_CALENDAR_HOME_SET */
	XML_DAV_DISPLAYNAME, /* PROP_DISPLAYNAME */
	XML_DAV_GETETAG, /* PROP_GETETAG */
	XML_DAV_PRINCIPAL_URL, /* PROP_PRINCIPAL_URL */
	XML_DAV_RESOURCETYPE, /* PROP_RESOURCETYPE */
};

static	const char *const xmls[XML__MAX] = {
	"C:calendar", /* XML_CALDAV_CALENDAR */
	"C:calendar-home-set", /* XML_CALDAV_CALENDAR_HOME_SET */
	"D:collection", /* XML_DAV_COLLECTION */
	"D:displayname", /* XML_DAV_DISPLAYNAME */
	"D:getetag", /* XML_DAV_GETETAG */
	"D:href", /* XML_DAV_HREF */
	"D:multistatus", /* XML_DAV_MULTISTATUS */
	"D:principal-URL", /* XML_DAV_PRINCIPAL_URL */
	"D:prop", /* XML_DAV_PROP */
        "D:propstat", /* XML_DAV_PROPSTAT */
	"D:resourcetype", /* XML_DAV_RESOURCETYPE */
	"D:response", /* XML_DAV_RESPONSE */
	"D:status", /* XML_DAV_STATUS */
	"D:unauthenticated", /* XML_DAV_UNAUTHENTICATED */
};

static int
pathnorm(const char *p, char *path)
{
	int	 sz;

	if (NULL == p || '\0' == p[0])
		return(0);
	if (strstr(p, "../") || strstr(p, "/.."))
		return(0);
	sz = snprintf(path, PATH_MAX, "%s%s", CALDIR, p);
	return(sz < PATH_MAX);
}

static void
logmsg(const struct kreq *r, const char *fmt, ...)
{
	va_list	 	 ap;

	fprintf(stderr, "%s: debug(%s): ", 
		r->pname, kmethods[r->method]);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void
logerr(const struct kreq *r, const char *fmt, ...)
{
	va_list	 	 ap;

	fprintf(stderr, "%s: error(%s): ", 
		r->pname, kmethods[r->method]);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* 
 * Validator for iCalendar OR CalDav object.
 * This checks the initial string of the object: if it equals the
 * prologue to an iCalendar, we attempt an iCalendar parse.
 * Otherwise, we try a CalDav parse.
 */
static int
kvalid(struct kpair *kp)
{
	size_t	 	 sz;
	const char	*tok = "BEGIN:VCALENDAR";
	struct ical	*ical;
	struct caldav	*dav;

	if ( ! kvalid_stringne(kp))
		return(0);
	sz = strlen(tok);
	if (0 == strncmp(kp->val, tok, sz)) {
		ical = ical_parse(kp->val);
		ical_free(ical);
		return(NULL != ical);
	}
	dav = caldav_parse(kp->val, kp->valsz);
	caldav_free(dav);
	return(NULL != dav);
}

/*
 * Put a character into a request stream.
 * We use this instead of just fputc because the stream might be
 * compressed, so we need kcgi(3)'s function.
 */
static void
ical_putc(int c, void *arg)
{
	struct kreq	*r = arg;

	khttp_putc(r, c);
}

/*
 * If found, convert the (already-validated) iCalendar.
 */
static struct ical *
req2ical(struct kreq *r)
{
	struct ical	*p;

	if (NULL == r->fieldmap[0]) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	}
	
	/* We already know that this works! */
	p = ical_parse(r->fieldmap[0]->val);
	assert(NULL != p);
	return(p);
}

/*
 * This converts the request into a CalDav object.
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

	p = caldav_parse(r->fieldmap[0]->val, r->fieldmap[0]->valsz);
	fprintf(stderr, "%.*s\n", (int)r->fieldmap[0]->valsz, r->fieldmap[0]->val);
	assert(NULL != p);
	return(p);
}

/*
 * Satisfy RFC 4791, 5.3.2, PUT.
 * This has fairly complicated semantics.
 */
static void
put(struct kreq *r)
{
	struct ical	*p, *cur;
	int		 fd;
	size_t		 sz;
	const char	*d;

	if (NULL == (p = req2ical(r))) {
		logerr(r, "failed message body parse");
		return;
	}

	if (NULL != r->reqmap[KREQU_IF] &&
		(sz = strlen(r->reqmap[KREQU_IF]->val)) == 36 &&
		'(' == r->reqmap[KREQU_IF]->val[0] &&
		'[' == r->reqmap[KREQU_IF]->val[1] &&
		']' == r->reqmap[KREQU_IF]->val[sz - 2] &&
		')' == r->reqmap[KREQU_IF]->val[sz - 1]) {
		/*
		 * If we have an If header, then we see if the existing
		 * object matches the requested Etag (MD5).  If it does,
		 * then we replace it with the PUT.  If it doesn't, then
		 * we don't do the replacement.
		 */
		cur = ical_parsefile_open(r->arg, &fd);
		d = r->reqmap[KREQU_IF]->val + 2;
		logmsg(r, "%s: if [%.32s] (%s)", r->arg, d, p->digest);
		if (NULL == cur) {
			logmsg(r, "%s: does not exist", r->arg);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_404]);
		} else if (strncmp(cur->digest, d, 32)) {
			logmsg(r, "%s: no replace file", r->arg);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_412]);
		} else if (-1 == lseek(fd, 0, SEEK_SET) ||
				-1 == ftruncate(fd, 0)) {
			perror(r->arg);
			logerr(r, "%s: can't truncate!?", r->arg);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_505]);
		} else {
			logmsg(r, "%s: replace file", r->arg);
			ical_printfile(fd, p);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_201]);
			khttp_head(r, kresps[KRESP_ETAG], 
				"%s", p->digest);
		}
		ical_parsefile_close(r->arg, fd);
		ical_free(cur);
	} else if (ical_putfile(r->arg, p)) {
		logmsg(r, "%s: install new", r->arg);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_201]);
		khttp_head(r, kresps[KRESP_ETAG], 
			"%s", p->digest);
	} else if (NULL == (cur = ical_parsefile(r->arg))) {
		logerr(r, "%s: can't parse!?", r->arg);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_505]);
	} else {
		logmsg(r, "%s: no replace", r->arg);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_412]);
		khttp_head(r, kresps[KRESP_ETAG], 
			"%s", cur->digest);
		ical_free(cur);
	}
	khttp_body(r);
	ical_free(p);
}

/*
 * RFC 4791 doesn't specify any special behaviour for GET, and RFC 2518
 * just says it follows HTTP, RFC 2068.
 * So use those semantics.
 */
static void
get(struct kreq *r)
{
	struct ical	*p;
	const char	*cp;

	if (NULL == (p = ical_parsefile(r->arg))) {
		logmsg(r, "%s: does not exist", r->arg);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_404]);
		khttp_body(r);
		return;
	}

	/* 
	 * If we request with the If-None-Match header (see RFC 2068,
	 * 14.26), then see if the ETag (MD5 hash) is consistent.
	 * If it is, then indicate that the remote cache is ok.
	 * If not, resend the data.
	 */
	if (NULL != r->reqmap[KREQU_IF_NONE_MATCH]) {
		cp = r->reqmap[KREQU_IF_NONE_MATCH]->val;
		if (0 == strcmp(p->digest, cp)) {
			logmsg(r, "%s: cached (%s = %s)", 
				r->arg, cp, p->digest);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_304]);
			khttp_head(r, kresps[KRESP_ETAG], 
				"%s", p->digest);
			khttp_body(r);
			ical_free(p);
			return;
		} 
		logmsg(r, "%s: not cached (%s != %s)", 
			r->arg, cp, p->digest);
	} else 
		logmsg(r, "%s: no cache request", r->arg);

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_CALENDAR]);
	khttp_head(r, kresps[KRESP_ETAG], "%s", p->digest);
	khttp_body(r);
	ical_print(p, ical_putc, r);
	ical_free(p);
}

/*
 * Defines the PROPFIND method for a collection (directory).
 */
static void
propfind_collection(const struct caldav *dav, struct kreq *r)
{
	size_t	 	 i;
	struct kxmlreq	 xml;
	struct config	*cfg;
	enum xml	 key;

	cfg = config_parse(r->arg);

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_body(r);
	kxml_open(&xml, r, xmls, XML__MAX);
	kxml_pushattrs(&xml, XML_DAV_MULTISTATUS, 
		"xmlns:C", "urn:ietf:params:xml:ns:caldav",
		"xmlns:D", "DAV:", NULL);
	kxml_push(&xml, XML_DAV_RESPONSE);
	kxml_push(&xml, XML_DAV_HREF);
	kxml_text(&xml, r->pname);
	kxml_text(&xml, r->fullpath);
	kxml_pop(&xml);
	kxml_push(&xml, XML_DAV_PROPSTAT);
	kxml_push(&xml, XML_DAV_PROP);
	for (i = 0; i < dav->propsz; i++) {
		key = xmlprops[dav->props[i].key];
		if (NULL == cfg) {
			kxml_pushnull(&xml, key);
			continue;
		}
		kxml_push(&xml, key);
		switch (dav->props[i].key) {
		case (PROP_RESOURCETYPE):
			/* This is necessary as per RFC 4791, 4.2. */
			kxml_pushnull(&xml, XML_DAV_COLLECTION);
			kxml_pushnull(&xml, XML_CALDAV_CALENDAR);
			break;
		case (PROP_PRINCIPAL_URL):
			kxml_push(&xml, XML_DAV_HREF);
			kxml_text(&xml, r->pname);
			kxml_text(&xml, r->fullpath);
			kxml_pop(&xml);
		case (PROP_DISPLAYNAME):
			kxml_text(&xml, cfg->displayname);
			break;
		case (PROP_CALENDAR_HOME_SET):
			kxml_text(&xml, cfg->calendarhomeset);
			break;
		default:
			break;
		} 
		kxml_pop(&xml);
	}
	kxml_pop(&xml);
	kxml_push(&xml, XML_DAV_STATUS);
	kxml_text(&xml, "HTTP/1.1 ");
	kxml_text(&xml, khttps[NULL == cfg ? KHTTP_404 : KHTTP_200]);
	kxml_popall(&xml);
	kxml_close(&xml);
	config_free(cfg);
}

/*
 * A PROPFIND request for a specified calendar resource (file).
 */
static void
propfind_resource(const struct caldav *dav, struct kreq *r)
{
	struct ical	*ical;
	size_t		 i;
	const char	*tag;

	for (i = 0; i < dav->propsz; i++) 
		logmsg(r, "%s: resource: %s", r->arg, 
			calelems[calpropelems[dav->props[i].key]]);

	if (NULL == (ical = ical_parsefile(r->arg))) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_404]);
		khttp_body(r);
		return;
	} 

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_body(r);
	khttp_puts(r, "<?xml version=\"1.0\" encoding=\"utf-8\" ?>");
	khttp_puts(r, "<DAV:multistatus>");
	khttp_puts(r, "<DAV:response>");
	khttp_puts(r, "<DAV:href>http://");
	khttp_puts(r, r->host);
	khttp_puts(r, r->pname);
	khttp_puts(r, r->fullpath);
	khttp_puts(r, "</DAV:href>");
	khttp_puts(r, "<DAV:propstat>");
	khttp_puts(r, "<DAV:prop>");
	for (i = 0; i < dav->propsz; i++) {
		tag = calelems[calpropelems[dav->props[i].key]];
		if (PROP_GETETAG == dav->props[i].key) {
			khttp_puts(r, "<DAV:getetag>");
			khttp_puts(r, ical->digest);
			khttp_puts(r, "</DAV:getetag>");
		}
	}
	khttp_puts(r, "</DAV:prop>");
	khttp_puts(r, "<DAV:status>HTTP/1.1 200 OK</DAV:status>");
	khttp_puts(r, "</DAV:propstat>");
	khttp_puts(r, "</DAV:response>");
	khttp_puts(r, "</DAV:multistatus>");
	ical_free(ical);
}

static void
propfind(struct kreq *r)
{
	struct caldav	*dav;
	struct stat	 st;

	if (-1 == stat(r->arg, &st)) {
		logmsg(r, "%s: not found", r->arg);
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_403]);
		khttp_body(r);
		return;
	} else if (NULL == (dav = req2caldav(r))) {
		logmsg(r, "%s: failed parse", r->arg);
		return;
	} else if (TYPE_PROPFIND != dav->type) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		caldav_free(dav);
		return;
	}

	if (S_IFDIR & st.st_mode)
		propfind_collection(dav, r);
	else
		propfind_resource(dav, r);

	caldav_free(dav);
}

static void
options(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_ALLOW], "%s", 
		"OPTIONS, GET, PUT, PROPFIND");
	khttp_body(r);
}

int
main(void)
{
	struct kreq	 r;
	struct kvalid	 valid = { kvalid, "" };
	char		 path[PATH_MAX];

	if (KCGI_OK != khttp_parse(&r, &valid, 1, NULL, 0, 0))
		return(EXIT_FAILURE);

	if ( ! pathnorm(r.fullpath, path)) {
		fprintf(stderr, "%s: insecure path\n", r.fullpath);
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_404]);
		khttp_body(&r);
		khttp_free(&r);
		return(EXIT_FAILURE);
	} else
		r.arg = path;

	switch (r.method) {
	case (KMETHOD_OPTIONS):
		options(&r);
		break;
	case (KMETHOD_PUT):
		put(&r);
		break;
	case (KMETHOD_PROPFIND):
		propfind(&r);
		break;
	case (KMETHOD_GET):
		get(&r);
		break;
	default:
		fprintf(stderr, "Unknown method\n");
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_405]);
		khttp_body(&r);
		break;
	}

	khttp_free(&r);
	return(EXIT_SUCCESS);
}
