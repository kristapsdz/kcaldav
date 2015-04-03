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
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

#ifndef CALDIR
#error "CALDIR token not defined!"
#endif

/*
 * Configure all paths used in our system.
 * We do this here to prevent future mucking around with string copying,
 * concatenation, and path safety.
 * Return zero on failure and non-zero on success.
 */
static int
req2path(struct kreq *r, const char *caldir)
{
	struct state	*st = r->arg;
	size_t	 	 sz;
	char		*cp;

	/* Absolutely don't let it empty paths! */
	if (NULL == r->fullpath || '\0' == r->fullpath[0]) {
		kerrx("zero-length request URI");
		return(0);
	} 

	/* Don't let in paths having relative directory parts. */
	if ('/' != r->fullpath[0]) {
		kerrx("%s: relative path", r->fullpath);
		return(0);
	} else if (strstr(r->fullpath, "../") || 
			strstr(r->fullpath, "/..")) {
		kerrx("%s: insecure path", r->fullpath);
		return(0);
	} 

	/* The filename component can't start with a dot. */
	cp = strrchr(r->fullpath, '/');
	assert(NULL != cp);
	if ('.' == cp[1]) {
		kerrx("%s: dotted filename", r->fullpath);
		return(0);
	}
	
	/* Check our calendar directory for security. */
	if (strstr(caldir, "../") || strstr(caldir, "/..")) {
		kerrx("%s: insecure path", caldir);
		return(0);
	} else if ('\0' == caldir[0]) {
		kerrx("zero-length calendar root");
		return(0);
	}

	/* Create our full pathname. */
	sz = strlcpy(st->rpath, r->pname, sizeof(st->rpath));
	if (sz && '/' == st->rpath[sz - 1])
		st->rpath[sz - 1] = '\0';
	sz = strlcat(st->rpath, r->fullpath, sizeof(st->rpath));
	if (sz > sizeof(st->rpath)) {
		kerrx("%s: path too long", st->rpath);
		return(0);
	}

	/* Create our principal filename. */
	sz = strlcpy(st->prncplfile, caldir, sizeof(st->prncplfile));
	if (sz >= sizeof(st->prncplfile)) {
		kerrx("%s: path too long", st->prncplfile);
		return(0);
	} else if ('/' != st->prncplfile[sz - 1])
		strlcat(st->prncplfile, "/", sizeof(st->prncplfile));
	sz = strlcat(st->prncplfile, 
		"kcaldav.passwd", sizeof(st->prncplfile));
	if (sz > sizeof(st->prncplfile)) {
		kerrx("%s: path too long", st->prncplfile);
		return(0);
	}

	/* Create our file-system mapped pathname. */
	sz = strlcpy(st->path, caldir, sizeof(st->path));
	if ('/' == st->path[sz - 1])
		st->path[sz - 1] = '\0';
	sz = strlcat(st->path, r->fullpath, sizeof(st->path));
	if (sz >= sizeof(st->path)) {
		kerrx("%s: path too long", st->path);
		return(0);
	}

	/* Is the request on a collection? */
	st->isdir = '/' == st->path[sz - 1];

	if ( ! st->isdir) {
		/* Create our file-system mapped temporary pathname. */
		sz = strlcpy(st->temp, caldir, sizeof(st->temp));
		if ('/' == st->temp[sz - 1])
			st->temp[sz - 1] = '\0';
		strlcat(st->temp, r->fullpath, sizeof(st->temp));
		cp = strrchr(st->temp, '/');
		assert(NULL != cp);
		assert('\0' != cp[1]);
		cp[1] = '.';
		cp[2] = '\0';
		strlcat(st->temp, 
			strrchr(r->fullpath, '/') + 1, 
			sizeof(st->temp));
		strlcat(st->temp, ".", sizeof(st->temp));
		sz = strlcat(st->temp, ".XXXXXXXX", sizeof(st->temp));
		if (sz >= sizeof(st->temp)) {
			kerrx("%s: path too long", st->temp);
			return(0);
		}
	}

	/* If we're not a directory, adjust our dir component. */
	strlcpy(st->dir, st->path, sizeof(st->dir));
	if ( ! st->isdir) {
		cp = strrchr(st->dir, '/');
		assert(NULL != cp);
		cp[1] = '\0';
	} 

	/* Create our ctag filename. */
	strlcpy(st->ctagfile, st->dir, sizeof(st->ctagfile));
	sz = strlcat(st->ctagfile, 
		"/kcaldav.ctag", sizeof(st->ctagfile));
	if (sz >= sizeof(st->ctagfile)) {
		kerrx("%s: path too long", st->ctagfile);
		return(0);
	}

	/* Create our configuration filename. */
	strlcpy(st->configfile, st->dir, sizeof(st->configfile));
	sz = strlcat(st->configfile, 
		"/kcaldav.conf", sizeof(st->configfile));
	if (sz >= sizeof(st->configfile)) {
		kerrx("%s: path too long", st->configfile);
		return(0);
	}

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
		ical = ical_parse(NULL, kp->val, kp->valsz);
		ical_free(ical);
		return(NULL != ical);
	}
	dav = caldav_parse(kp->val, kp->valsz);
	caldav_free(dav);
	return(NULL != dav);
}

int
main(int argc, char *argv[])
{
	struct kreq	 r;
	struct kvalid	 valid = { kvalid, "" };
	struct state	*st;
	int		 rc;
	const char	*caldir = NULL;

	setlinebuf(stderr);

	while (-1 != getopt(argc, argv, "")) 
		/* Spin. */ ;

	argv += optind;
	if ((argc -= optind) > 0)
		caldir = argv[0];

	if (KCGI_OK != khttp_parse(&r, &valid, 1, NULL, 0, 0))
		return(EXIT_FAILURE);

	if (NULL == (r.arg = st = calloc(1, sizeof(struct state)))) {
		kerr(NULL);
		http_error(&r, KHTTP_505);
		goto out;
	}

	if (KMETHOD__MAX == r.method) {
		http_error(&r, KHTTP_405);
		goto out;
	}

	/*
	 * Resolve paths from the HTTP request.
	 * This sets the many paths we care about--all of which are for
	 * our convenience--and make sure they're secure.
	 */
	if ( ! req2path(&r, NULL == caldir ? CALDIR : caldir)) {
		kerrx("path configuration failure (startup)");
		http_error(&r, KHTTP_403);
		goto out;
	} 

	/*
	 * Parse our HTTP credentials.
	 * This must be digest authorisation passed from the web server.
	 * NOTE: Apache will strip this out, so it's necessary to add a
	 * rewrite rule to keep these.
	 */
	st->auth = httpauth_parse
		(NULL == r.reqmap[KREQU_AUTHORIZATION] ?
		 NULL : r.reqmap[KREQU_AUTHORIZATION]->val);

	if (NULL == st->auth) {
		kerrx("memory failure (httpauth)");
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == st->auth->authorised) {
		/* No authorisation found (or failed). */
		kerrx("%s: unauthorised", st->rpath);
		http_error(&r, KHTTP_401);
		goto out;
	} else if (strcmp(st->auth->uri, st->rpath)) {
		kerrx("%s: bad authorisation URI: %s", 
			st->rpath, st->auth->uri);
		http_error(&r, KHTTP_401);
		goto out;
	} 

	/*
	 * Next, parse the our passwd file and look up the given HTTP
	 * authorisation name within the database.
	 * This will return -1 on allocation failure or 0 if the password
	 * file doesn't exist or is malformed.
	 * It might set the principal to NULL if not found.
	 */
	rc = prncpl_parse(st->prncplfile, 
		kmethods[r.method], st->auth, &st->prncpl);

	if (rc < 0) {
		kerrx("memory failure (principal)");
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == rc) {
		kerrx("%s: bad principal file", st->prncplfile);
		http_error(&r, KHTTP_403);
		goto out;
	} else if (NULL == st->prncpl) {
		kerrx("%s: bad principal authorisation: %s", 
			st->prncplfile, st->auth->user);
		http_error(&r, KHTTP_401);
		goto out;
	}

	/* 
	 * We require a configuration file in the directory where we've
	 * been requested to introspect.
	 * It's ok if "path" is a directory.
	 */
	rc = config_parse(st->configfile, &st->cfg, st->prncpl);

	if (rc < 0) {
		kerrx("memory failure (config)");
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == rc) {
		kerrx("%s: bad config", st->configfile);
		http_error(&r, KHTTP_403);
		goto out;
	} else if (PERMS_NONE == st->cfg->perms) {
		kerrx("%s: principal without privilege: %s", 
			st->path, st->prncpl->name);
		http_error(&r, KHTTP_403);
		goto out;
	}

	/* 
	 * We're ready to go!
	 * (We still may fail privileges for individual resources, but
	 * beyond that, the only failure is on the request URI.)
	 */
	switch (r.method) {
	case (KMETHOD_PUT):
		method_put(&r);
		break;
	case (KMETHOD_PROPFIND):
		method_propfind(&r);
		break;
	case (KMETHOD_GET):
		method_get(&r);
		break;
	case (KMETHOD_REPORT):
		method_report(&r);
		break;
	case (KMETHOD_DELETE):
		method_delete(&r);
		break;
	case (KMETHOD_OPTIONS):
		method_options(&r);
		break;
	default:
		kerrx("%s: ignoring method %s",
			st->path, kmethods[r.method]);
		http_error(&r, KHTTP_405);
		break;
	}

out:
	khttp_free(&r);
	config_free(st->cfg);
	prncpl_free(st->prncpl);
	httpauth_free(st->auth);
	free(st);
	return(EXIT_SUCCESS);
}
