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
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "main.h"

#ifndef CALDIR
#error "CALDIR token not defined!"
#endif

/*
 * Make sure that requrested path is sane, then append it to our local
 * calendar root (CALDIR).
 * Returns zero on failure or non-zero on success.
 */
static int
req2path(struct kreq *r)
{
	struct state	*st = r->arg;
	size_t	 	 sz;
	char		*cp;

	/* Absolutely don't let it empty paths! */
	if (NULL == r->fullpath || '\0' == r->fullpath[0]) {
		fprintf(stderr, "empty request\n");
		return(0);
	} 

	/* Don't let in paths having relative directory parts. */
	if ('/' != r->fullpath[0]) {
		fprintf(stderr, "%s: relative path\n", r->fullpath);
		return(0);
	} else if (strstr(r->fullpath, "../") || 
			strstr(r->fullpath, "/..")) {
		fprintf(stderr, "%s: relative path\n", r->fullpath);
		return(0);
	} 

	/* The filename component can't start with a dot. */
	cp = strrchr(r->fullpath, '/');
	assert(NULL != cp);
	if ('.' == cp[1]) {
		fprintf(stderr, "%s: illegal path\n", r->fullpath);
		return(0);
	}
	
	/* Check our calendar directory for security. */
	if (strstr(CALDIR, "../") || strstr(CALDIR, "/..")) {
		fprintf(stderr, "%s: insecure CALDIR\n", CALDIR);
		return(0);
	} else if ('\0' == CALDIR[0]) {
		fprintf(stderr, "empty CALDIR\n");
		return(0);
	}

	/* Create our principal filename. */
	sz = strlcpy(st->prncplfile, CALDIR, sizeof(st->prncplfile));
	if (sz >= sizeof(st->prncplfile)) {
		fprintf(stderr, "%s: too long\n", st->prncplfile);
		return(0);
	} else if ('/' != st->prncplfile[sz - 1])
		strlcat(st->prncplfile, "/", sizeof(st->prncplfile));
	sz = strlcat(st->prncplfile, 
		"kcaldav.passwd", sizeof(st->prncplfile));
	if (sz > sizeof(st->prncplfile)) {
		fprintf(stderr, "%s: too long\n", st->prncplfile);
		return(0);
	}

	/* Create our file-system mapped pathname. */
	sz = strlcpy(st->path, CALDIR, sizeof(st->path));
	if ('/' == st->path[sz - 1])
		st->path[sz - 1] = '\0';
	sz = strlcat(st->path, r->fullpath, sizeof(st->path));
	if (sz >= sizeof(st->path)) {
		fprintf(stderr, "%s: too long\n", st->path);
		return(0);
	}

	/* Is the request on a collection? */
	st->isdir = '/' == st->path[sz - 1];

	if ( ! st->isdir) {
		/* Create our file-system mapped temporary pathname. */
		sz = strlcpy(st->temp, CALDIR, sizeof(st->temp));
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
			fprintf(stderr, "%s: too long\n", st->temp);
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
		fprintf(stderr, "%s: too long\n", st->ctagfile);
		return(0);
	}

	/* Create our configuration filename. */
	strlcpy(st->configfile, st->dir, sizeof(st->configfile));
	sz = strlcat(st->configfile, 
		"/kcaldav.conf", sizeof(st->configfile));
	if (sz >= sizeof(st->configfile)) {
		fprintf(stderr, "%s: too long\n", st->configfile);
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
		ical = ical_parse(kp->val);
		ical_free(ical);
		return(NULL != ical);
	}
	dav = caldav_parse(kp->val, kp->valsz);
	caldav_free(dav);
	return(NULL != dav);
}

int
main(void)
{
	struct kreq	 r;
	struct kvalid	 valid = { kvalid, "" };
	struct state	 st;
	int		 rc;

	if (KCGI_OK != khttp_parse(&r, &valid, 1, NULL, 0, 0))
		return(EXIT_FAILURE);

	memset(&st, 0, sizeof(struct state));
	r.arg = &st;

	/* Process options before authentication. */
	if (KMETHOD_OPTIONS == r.method) {
		method_options(&r);
		goto out;
	}

	/*
	 * Resolve a path from the HTTP request.
	 * We do this first because it doesn't require any allocation.
	 */
	if ( ! req2path(&r)) {
		http_error(&r, KHTTP_403);
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
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == st.auth->authorised) {
		/* No authorisation found (or failed). */
		fprintf(stderr, "%s: invalid HTTP "
			"authorisation\n", r.fullpath);
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
	if ((rc = prncpl_parse(st.prncplfile, st.auth, &st.prncpl)) < 0) {
		fprintf(stderr, "%s: memory failure during "
			"principal authorisation\n", st.prncplfile);
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == rc) {
		fprintf(stderr, "%s: not a valid principal "
			"authorisation file\n", st.prncplfile);
		http_error(&r, KHTTP_403);
		goto out;
	} else if (NULL == st.prncpl) {
		fprintf(stderr, "%s: invalid principal "
			"authorisation\n", st.prncplfile);
		http_error(&r, KHTTP_401);
		goto out;
	}

	/* 
	 * We require a configuration file in the directory where we've
	 * been requested to introspect.
	 * It's ok if "path" is a directory.
	 */
	if ((rc = config_parse(st.configfile, &st.cfg, st.prncpl)) < 0) {
		fprintf(stderr, "%s: memory failure "
			"during configuration parse\n", st.configfile);
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == rc) {
		fprintf(stderr, "%s: not a valid "
			"configuration file\n", st.configfile);
		http_error(&r, KHTTP_403);
		goto out;
	} else if (PERMS_NONE == st.cfg->perms) {
		fprintf(stderr, "%s: principal without "
			"any privilege\n", st.configfile);
		http_error(&r, KHTTP_403);
		goto out;
	}

	switch (r.method) {
	case (KMETHOD_PUT):
		fprintf(stderr, "PUT: %s (%s)\n", r.fullpath, st.path);
		method_put(&r);
		break;
	case (KMETHOD_PROPFIND):
		fprintf(stderr, "PROPFIND: %s (%s) (depth=%s)\n", 
			r.fullpath, st.path,
			NULL != r.reqmap[KREQU_DEPTH] ?
			r.reqmap[KREQU_DEPTH]->val : "infinity");
		method_propfind(&r);
		break;
	case (KMETHOD_GET):
		fprintf(stderr, "GET: %s (%s)\n", r.fullpath, st.path);
		method_get(&r);
		break;
	case (KMETHOD_REPORT):
		fprintf(stderr, "REPORT: %s (%s)\n", r.fullpath, st.path);
		method_report(&r);
		break;
	case (KMETHOD_DELETE):
		fprintf(stderr, "DELETE: %s (%s)\n", r.fullpath, st.path);
		method_delete(&r);
		break;
	default:
		http_error(&r, KHTTP_405);
		break;
	}

out:
	khttp_free(&r);
	config_free(st.cfg);
	prncpl_free(st.prncpl);
	httpauth_free(st.auth);
	return(EXIT_SUCCESS);
}
