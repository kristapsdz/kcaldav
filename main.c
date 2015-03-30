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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
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

#ifndef CALDIR
#error "CALDIR token not defined!"
#endif

/*
 * We carry this state around with us after successfully setting up our
 * environment: HTTP authorised (auth), HTTP authorisation matched with
 * a local principal (prncpl), and configuration file read for the
 * relevant directory (cfg).
 * The "path" component is the subject of the HTTP query mapped to the
 * local CALDIR.
 */
struct	state {
	struct prncpl	*prncpl;
	struct config	*cfg;
	struct httpauth	*auth;
	char		 path[PATH_MAX];
	int		 isdir;
};

/* Canned XML responses. */
enum	xml {
	XML_CALDAV_CALENDAR,
	XML_DAV_COLLECTION,
	XML_DAV_HREF,
	XML_DAV_MULTISTATUS,
	XML_DAV_PROP,
	XML_DAV_PROPSTAT,
	XML_DAV_RESPONSE,
	XML_DAV_STATUS,
	XML_DAV_UNAUTHENTICATED,
	XML__MAX
};

static	const char *const xmls[XML__MAX] = {
	"C:calendar", /* XML_CALDAV_CALENDAR */
	"D:collection", /* XML_DAV_COLLECTION */
	"D:href", /* XML_DAV_HREF */
	"D:multistatus", /* XML_DAV_MULTISTATUS */
	"D:prop", /* XML_DAV_PROP */
        "D:propstat", /* XML_DAV_PROPSTAT */
	"D:response", /* XML_DAV_RESPONSE */
	"D:status", /* XML_DAV_STATUS */
	"D:unauthenticated", /* XML_DAV_UNAUTHENTICATED */
};

static void
send403(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_403]);
	khttp_body(r);
}

static void
send405(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_405]);
	khttp_body(r);
}

static void
send505(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_505]);
	khttp_body(r);
}

static void
send401(struct kreq *r)
{
	char	 nonce[33];
	size_t	 i;

	for (i = 0; i < 16; i++)
		snprintf(nonce + i * 2, sizeof(nonce) - i * 2,
			"%.2X", arc4random_uniform(256));

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_401]);
	khttp_head(r, kresps[KRESP_WWW_AUTHENTICATE],
		"Digest realm=\"foo\" nonce=\"%s\"", nonce);
	khttp_body(r);
}

/*
 * Make sure that requrested path is sane, then append it to our local
 * calendar root (CALDIR).
 * Returns zero on failure or non-zero on success.
 */
static int
req2path(struct kreq *r)
{
	struct state	*st = r->arg;
	int	 	 sz;

	if (NULL == r->fullpath || '\0' == r->fullpath[0]) {
		fprintf(stderr, "empty path!?\n");
		return(0);
	} 

	if (strstr(r->fullpath, "../") || strstr(r->fullpath, "/..")) {
		fprintf(stderr, "%s: insecure path\n", r->fullpath);
		return(0);
	}

	sz = snprintf(st->path, sizeof(st->path), 
		"%s%s", CALDIR, r->fullpath);

	if ((size_t)sz >= sizeof(st->path)) {
		fprintf(stderr, "%s%s: too long\n", CALDIR, r->fullpath);
		return(0);
	}

	st->isdir = '/' == st->path[sz - 1];
	return(1);
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
 * See req2config().
 */
static int
req2config_file(struct kreq *r, char *buf)
{
	char		*dir;
	char		 np[PATH_MAX];
	struct state	*st = r->arg;

	dir = dirname(buf);
	if (strlcpy(np, dir, sizeof(np)) >= sizeof(np)) {
		fprintf(stderr, "%s: too long\n", np);
		return(0);
	}
	if (strlcat(np, "/kcaldav.conf", sizeof(np)) >= sizeof(np)) {
		fprintf(stderr, "%s: too long\n", buf);
		return(0);
	}
	return(config_parse(np, &st->cfg));
}

/*
 * See req2config().
 */
static int
req2config_dir(struct kreq *r, char *buf, size_t bufsz)
{
	struct state	*st = r->arg;

	if (strlcat(buf, "/kcaldav.conf", bufsz) >= bufsz) {
		fprintf(stderr, "%s: too long\n", buf);
		return(0);
	}
	return(config_parse(buf, &st->cfg));
}

/*
 * Given a path (or filename) "path", resolve the configuration file
 * that's at the path or within the path containing the file "path".
 */
static int
req2config(struct kreq *r)
{
	struct state	*st = r->arg;
	char		 buf[PATH_MAX];
	size_t		 sz;

	sz = strlcpy(buf, st->path, sizeof(buf));
	if (sz >= sizeof(buf)) {
		fprintf(stderr, "%s: too long\n", buf);
		return(0);
	}

	return('/' == buf[sz - 1] ? 
		req2config_dir(r, buf, sizeof(buf)) :
		req2config_file(r, buf));
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
 * We know that the request is a well-formed CalDav object because it
 * appears in the fieldmap and we parsed it during HTTP unpacking.
 * So we really only check its media type.
 */
static struct caldav *
req2caldav(struct kreq *r, enum type type)
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

	/* Check our media type. */
	if (type != p->type) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_415]);
		khttp_body(r);
		caldav_free(p);
		return(NULL);
	}

	fprintf(stderr, "%s", r->fieldmap[0]->val);
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
	struct state	*st = r->arg;
	int		 fd;
	size_t		 sz;
	const char	*d;

	if (NULL == (p = req2ical(r)))
		return;

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
		cur = ical_parsefile_open(st->path, &fd);
		d = r->reqmap[KREQU_IF]->val + 2;
		if (NULL == cur) {
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_404]);
		} else if (strncmp(cur->digest, d, 32)) {
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_412]);
		} else if (-1 == lseek(fd, 0, SEEK_SET) ||
				-1 == ftruncate(fd, 0)) {
			perror(st->path);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_505]);
		} else {
			ical_printfile(fd, p);
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_201]);
			khttp_head(r, kresps[KRESP_ETAG], 
				"%s", p->digest);
		}
		ical_parsefile_close(st->path, fd);
		ical_free(cur);
	} else if (ical_putfile(st->path, p)) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_201]);
		khttp_head(r, kresps[KRESP_ETAG], 
			"%s", p->digest);
	} else if (NULL == (cur = ical_parsefile(st->path))) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_505]);
	} else {
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
	struct state	*st = r->arg;
	const char	*cp;

	if (NULL == (p = ical_parsefile(st->path))) {
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
			khttp_head(r, kresps[KRESP_STATUS], 
				"%s", khttps[KHTTP_304]);
			khttp_head(r, kresps[KRESP_ETAG], 
				"%s", p->digest);
			khttp_body(r);
			ical_free(p);
			return;
		} 
	} 

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
 * The directory we search is always the root of the request, i.e.,
 * st->path.
 */
static void
propfind_collection(struct kxmlreq *xml, const struct caldav *dav)
{
	struct state	*st = xml->req->arg;
	size_t	 	 i, nf;
	const char	*tag;
	int 		 accepted[PROP__MAX + 1];

	memset(accepted, 0, sizeof(accepted));
	accepted[PROP_CALENDAR_HOME_SET] = 1;
	accepted[PROP_CALENDAR_USER_ADDRESS_SET] = 1;
	accepted[PROP_CURRENT_USER_PRINCIPAL] = 1;
	accepted[PROP_DISPLAYNAME] = 1;
	accepted[PROP_GETCONTENTTYPE] = -1;
	accepted[PROP_GETETAG] = -1;
	accepted[PROP_EMAIL_ADDRESS_SET] = 1;
	accepted[PROP_PRINCIPAL_URL] = 1;
	accepted[PROP_RESOURCETYPE] = 1;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_text(xml, xml->req->pname);
	kxml_text(xml, xml->req->fullpath);
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);

	fprintf(stderr, "%s: %s: (self)\n", st->path, __func__);

	for (nf = i = 0; i < dav->propsz; i++) {
		if (0 == accepted[dav->props[i].key]) {
			nf++;
			fprintf(stderr, "Unknown prop: %s (%s)\n", 
				dav->props[i].name, 
				dav->props[i].xmlns);
			continue;
		} else if (accepted[dav->props[i].key] < 0) {
			fprintf(stderr, "Ignored prop: %s (%s)\n", 
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
		switch (dav->props[i].key) {
		case (PROP_CALENDAR_HOME_SET):
			kxml_push(xml, XML_DAV_HREF);
			kxml_text(xml, xml->req->pname);
			kxml_text(xml, xml->req->fullpath);
			kxml_pop(xml);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s%s\n", tag, dav->props[i].xmlns, 
				xml->req->pname, xml->req->fullpath);
			break;
		case (PROP_CALENDAR_USER_ADDRESS_SET):
			kxml_push(xml, XML_DAV_HREF);
			kxml_text(xml, "mailto:");
			kxml_text(xml, st->cfg->emailaddress);
			kxml_pop(xml);
			fprintf(stderr, "Known prop: %s (%s): "
				"mailto:%s\n", tag, 
				dav->props[i].xmlns,
				st->cfg->emailaddress);
			break;
		case (PROP_CURRENT_USER_PRINCIPAL):
			kxml_push(xml, XML_DAV_HREF);
			kxml_text(xml, xml->req->pname);
			kxml_text(xml, st->prncpl->homedir);
			kxml_pop(xml);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s%s\n", tag, dav->props[i].xmlns, 
				xml->req->pname, st->prncpl->homedir);
			break;
		case (PROP_DISPLAYNAME):
			kxml_text(xml, st->cfg->displayname);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s\n", tag, dav->props[i].xmlns,
				st->cfg->displayname);
			break;
		case (PROP_EMAIL_ADDRESS_SET):
			kxml_text(xml, st->cfg->emailaddress);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s\n", tag, dav->props[i].xmlns,
				st->cfg->emailaddress);
			break;
		case (PROP_PRINCIPAL_URL):
			kxml_push(xml, XML_DAV_HREF);
			kxml_text(xml, xml->req->pname);
			kxml_text(xml, xml->req->fullpath);
			kxml_pop(xml);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s%s\n", tag, dav->props[i].xmlns, 
				xml->req->pname, xml->req->fullpath);
			break;
		case (PROP_RESOURCETYPE):
			kxml_pushnull(xml, XML_DAV_COLLECTION);
			kxml_pushnull(xml, XML_CALDAV_CALENDAR);
			fprintf(stderr, "Known prop: %s (%s)\n", 
				tag, dav->props[i].xmlns);
			break;
		default:
			abort();
			break;
		} 
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, tag);
		khttp_putc(xml->req, '>');
	}
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_STATUS);
	kxml_text(xml, "HTTP/1.1 ");
	kxml_text(xml, khttps[KHTTP_200]);
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
			if (1 == accepted[dav->props[i].key])
				continue;
			khttp_puts(xml->req, "<X:");
			khttp_puts(xml->req, dav->props[i].name);
			khttp_puts(xml->req, " xmlns:X=\"");
			khttp_puts(xml->req, dav->props[i].xmlns);
			khttp_puts(xml->req, "\" />");
		}
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_text(xml, "HTTP/1.1 ");
		kxml_text(xml, khttps[KHTTP_404]);
		kxml_pop(xml);
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
	int 		 accepted[PROP__MAX + 1];

	memset(accepted, 0, sizeof(accepted));
	accepted[PROP_GETCONTENTTYPE] = 1;
	accepted[PROP_GETETAG] = 1;

	kxml_push(xml, XML_DAV_RESPONSE);
	kxml_push(xml, XML_DAV_HREF);
	kxml_text(xml, xml->req->pname);
	kxml_text(xml, xml->req->fullpath);
	if (NULL != name) {
		kxml_text(xml, name);
		fprintf(stderr, "%s: %s: %s%s%s\n", st->path, __func__, xml->req->pname, xml->req->fullpath, name);
	} else
		fprintf(stderr, "%s: %s: %s%s\n", st->path, __func__, xml->req->pname, xml->req->fullpath);
	kxml_pop(xml);

	/* See if we must reconstitute the file to open. */
	if (NULL != name) {
		pathp = buf;
		strlcpy(buf, st->path, sizeof(buf));
		sz = strlcat(buf, name, sizeof(buf));
		if (sz >= sizeof(buf)) {
			fprintf(stderr, "%s: too long\n", buf);
			kxml_push(xml, XML_DAV_STATUS);
			kxml_text(xml, "HTTP/1.1 ");
			kxml_text(xml, khttps[KHTTP_403]);
			kxml_pop(xml);
			kxml_pop(xml);
			return;
		}
	} else 
		pathp = name;

	/* We can only request iCal object, so parse now. */
	if (NULL == (ical = ical_parsefile(pathp))) {
		fprintf(stderr, "%s: parse error\n", pathp);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_text(xml, "HTTP/1.1 ");
		kxml_text(xml, khttps[KHTTP_403]);
		kxml_pop(xml);
		kxml_pop(xml);
		return;
	} 

	kxml_push(xml, XML_DAV_PROPSTAT);
	kxml_push(xml, XML_DAV_PROP);
	for (nf = i = 0; i < dav->propsz; i++) {
		if ( ! accepted[dav->props[i].key]) {
			nf++;
			fprintf(stderr, "Unknown prop: %s (%s)\n", 
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
		switch (dav->props[i].key) {
		case (PROP_GETCONTENTTYPE):
			khttp_puts(xml->req, 
				kmimetypes[KMIME_TEXT_CALENDAR]);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s\n", tag, dav->props[i].xmlns, 
				kmimetypes[KMIME_TEXT_CALENDAR]);
			break;
		case (PROP_GETETAG):
			khttp_puts(xml->req, ical->digest);
			fprintf(stderr, "Known prop: %s (%s): "
				"%s\n", tag, dav->props[i].xmlns, 
				ical->digest);
			break;
		default:
			abort();
		}
		khttp_puts(xml->req, "</X:");
		khttp_puts(xml->req, tag);
		khttp_putc(xml->req, '>');
	}
	kxml_pop(xml);
	kxml_push(xml, XML_DAV_STATUS);
	kxml_text(xml, "HTTP/1.1 ");
	kxml_text(xml, khttps[KHTTP_200]);
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
			if (accepted[dav->props[i].key])
				continue;
			khttp_puts(xml->req, "<X:");
			khttp_puts(xml->req, dav->props[i].name);
			khttp_puts(xml->req, " xmlns:X=\"");
			khttp_puts(xml->req, dav->props[i].xmlns);
			khttp_puts(xml->req, "\" />");
		}
		kxml_pop(xml);
		kxml_push(xml, XML_DAV_STATUS);
		kxml_text(xml, "HTTP/1.1 ");
		kxml_text(xml, khttps[KHTTP_404]);
		kxml_pop(xml);
		kxml_pop(xml);
		kxml_pop(xml);
	}

	kxml_pop(xml);
	ical_free(ical);
}

/*
 * PROPFIND is used to define properties for calendar collections (i.e.,
 * directories consisting of calendar resources) or resources
 * themselves.
 * Switch on that behaviour here.
 */
static void
propfind(struct kreq *r)
{
	struct caldav	*dav;
	struct state	*st = r->arg;
	struct kxmlreq	 xml;
	int		 depth;
	DIR		*dirp;
	struct dirent	*dp;

	if (NULL == (dav = req2caldav(r, TYPE_PROPFIND)))
		return;

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
		if (NULL == (dirp = opendir(st->path))) {
			perror(st->path);
		}
		while (NULL != dirp && NULL != (dp = readdir(dirp))) {
			fprintf(stderr, "%s: scanning: %s\n", st->path, dp->d_name);
			if ('.' == dp->d_name[0])
				continue;
			if (0 == strcmp(dp->d_name, "kcaldav.conf"))
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

static void
options(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_ALLOW], "%s", 
		"OPTIONS, GET, PUT, PROPFIND");
	khttp_head(r, "DAV", "1, 2, "
		"access-control, calendar-access");
	khttp_body(r);
}

int
main(void)
{
	struct kreq	 r;
	struct kvalid	 valid = { kvalid, "" };
	char		 prncpl[PATH_MAX];
	size_t		 i;
	struct state	 st;
	int		 rc;

	if (KCGI_OK != khttp_parse(&r, &valid, 1, NULL, 0, 0))
		return(EXIT_FAILURE);

	memset(&st, 0, sizeof(struct state));
	r.arg = &st;

	/*
	 * Resolve a path from the HTTP request.
	 * We do this first because it doesn't require any allocation.
	 */
	if ( ! req2path(&r)) {
		send403(&r);
		goto out;
	} 

	strlcpy(prncpl, CALDIR, sizeof(prncpl));
	i = strlcat(prncpl, "/kcaldav.passwd", sizeof(prncpl));
	if (i >= sizeof(prncpl)) {
		fprintf(stderr, "%s: too long\n", prncpl);
		send403(&r);
		goto out;
	}

	/*
	 * Parse our HTTP credentials.
	 * This must be digest authorisation passed from the web server.
	 * NOTE: Apache will strip this out, so it's necessary to add a
	 * rewrite rule to keep these.
	 */
	st.auth = httpauth_parse
		(NULL == r.reqmap[KREQU_AUTHORIZATION] ?
		 NULL : r.reqmap[KREQU_AUTHORIZATION]->val);

	if (NULL == st.auth) {
		/* Allocation failure! */
		fprintf(stderr, "%s: memory failure during "
			"HTTP authorisation\n", r.fullpath);
		send505(&r);
		goto out;
	} else if (0 == st.auth->authorised) {
		/* No authorisation found (or failed). */
		fprintf(stderr, "%s: invalid HTTP "
			"authorisation\n", r.fullpath);
		send401(&r);
		goto out;
	}

	fprintf(stderr, "%s: valid HTTP: %s\n", 
		r.fullpath, st.auth->user);

	/*
	 * Next, parse the our passwd file and look up the given HTTP
	 * authorisation name within the database.
	 * This will return -1 on allocation failure or 0 if the password
	 * file doesn't exist or is malformed.
	 * It might set the principal to NULL if not found.
	 */
	if ((rc = prncpl_parse(prncpl, st.auth, &st.prncpl)) < 0) {
		fprintf(stderr, "%s: memory failure during "
			"principal authorisation\n", r.fullpath);
		send505(&r);
		goto out;
	} else if (0 == rc) {
		fprintf(stderr, "%s/kcaldav.passwd: not a valid "
			"principal authorisation file\n", CALDIR);
		send403(&r);
		goto out;
	} else if (NULL == st.prncpl) {
		fprintf(stderr, "%s: invalid principal "
			"authorisation\n", r.fullpath);
		send401(&r);
		goto out;
	}

	fprintf(stderr, "%s: valid principal: %s\n", 
		r.fullpath, st.prncpl->name);

	/* 
	 * We require a configuration file in the directory where we've
	 * been requested to introspect.
	 * It's ok if "path" is a directory.
	 */
	if ((rc = req2config(&r)) < 0) {
		fprintf(stderr, "%s/kcaldav.conf: memory failure "
			"during configuration parse\n", st.path);
		send505(&r);
		goto out;
	} else if (0 == rc) {
		fprintf(stderr, "%s/kcaldav.conf: not a valid "
			"configuration file\n", st.path);
		send403(&r);
		goto out;
	}

	switch (r.method) {
	case (KMETHOD_OPTIONS):
		fprintf(stderr, "OPTIONS: %s (%s)\n", r.fullpath, st.path);
		options(&r);
		break;
	case (KMETHOD_PUT):
		fprintf(stderr, "PUT: %s (%s)\n", r.fullpath, st.path);
		put(&r);
		break;
	case (KMETHOD_PROPFIND):
		fprintf(stderr, "PROPFIND: %s (%s)\n", r.fullpath, st.path);
		propfind(&r);
		break;
	case (KMETHOD_GET):
		fprintf(stderr, "GET: %s (%s)\n", r.fullpath, st.path);
		get(&r);
		break;
	default:
		send405(&r);
		break;
	}

out:
	khttp_free(&r);
	config_free(st.cfg);
	prncpl_free(st.prncpl);
	httpauth_free(st.auth);
	return(EXIT_SUCCESS);
}
