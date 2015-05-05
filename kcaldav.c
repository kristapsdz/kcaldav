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
#include <ctype.h>
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
#ifdef KTRACE
#include <unistd.h>
#endif

#include <kcgi.h>
#include <kcgixml.h>
#include <sqlite3.h>

#include "extern.h"
#include "kcaldav.h"

#ifndef CALDIR
#error "CALDIR token not defined!"
#endif

int verbose = 0;

const char *const pages[PAGE__MAX] = {
	"index", /* PAGE_INDEX */
	"newcoln", /* PAGE_NEWCOLN */
	"setemail", /* PAGE_SETEMAIL */
	"setpass", /* PAGE_SETPASS */
};

const char *const valids[VALID__MAX] = {
	"", /* VALID_BODY */
	"colour", /* VALID_COLOUR */
	"description", /* VALID_DESCRIPTION */
	"email", /* VALID_EMAIL */
	"name", /* VALID_NAME */
	"pass", /* VALID_PASS */
	"path", /* VALID_PATH */
};

/*
 * The description of a calendar.
 * Make sure this is less than 4K.
 */
static int
kvalid_description(struct kpair *kp)
{

	if ( ! kvalid_stringne(kp))
		return(0);
	return (kp->valsz < 4096);
}

/*
 * The name of a calendar collection.
 * First, make sure that this is a safe, non-empty string.
 * Second, make sure that it's less than 256B.
 */
static int
kvalid_path(struct kpair *kp)
{

	if ( ! kvalid_stringne(kp))
		return(0);
	else if (kp->valsz > 256)
		return(0);
	return(http_safe_string(kp->val));
}

/*
 * The HTML5 string representation of a colour is a hex RGB.
 * We accept any hexadecimal case.
 * (Accept RGBA too, just in case.)
 */
static int
kvalid_colour(struct kpair *kp)
{
	size_t	 i;

	if ( ! kvalid_stringne(kp))
		return(0);
	if (7 != kp->valsz && 9 != kp->valsz)
		return(0);
	if ('#' != kp->val[0])
		return(0);
	for (i = 1; i < kp->valsz; i++) {
		if (isdigit((int)kp->val[i]))
			continue;
		if (isalpha((int)kp->val[i]) && 
			((kp->val[i] >= 'a' && 
			  kp->val[i] <= 'f') ||
			 (kp->val[i] >= 'A' &&
			  kp->val[i] <= 'F')))
			continue;
		return(0);
	}
	return(1);
}

/*
 * Validate a password MD5 hash.
 * These are fixed length and with fixed characteristics.
 */
static int
kvalid_hash(struct kpair *kp)
{
	size_t	 i;

	if ( ! kvalid_stringne(kp))
		return(0);
	if (32 != kp->valsz)
		return(0);
	for (i = 0; i < kp->valsz; i++) {
		if (isdigit((int)kp->val[i]))
			continue;
		if (isalpha((int)kp->val[i]) && 
			 islower((int)kp->val[i]) &&
			 kp->val[i] >= 'a' && 
			 kp->val[i] <= 'f')
			continue;
		return(0);
	}
	return(1);
}

/* 
 * Validate iCalendar OR CalDav object.
 * Use the content-type as parsed by kcgi(3) to clue us in to the actual
 * type to validate.
 */
static int
kvalid_body(struct kpair *kp)
{
	struct ical	*ical;
	struct caldav	*dav;

	switch (kp->ctypepos) {
	case (KMIME_TEXT_CALENDAR):
		ical = ical_parse(NULL, kp->val, kp->valsz);
		ical_free(ical);
		return(NULL != ical);
	case (KMIME_TEXT_XML):
		dav = caldav_parse(kp->val, kp->valsz);
		caldav_free(dav);
		return(NULL != dav);
	default:
		return(0);
	}
}

static void
state_free(struct state *st)
{

	if (NULL == st)
		return;
	prncpl_free(st->prncpl);
	free(st->principal);
	free(st->collection);
	free(st->resource);
	free(st);
}

/*
 * Load our principal account into the state object, priming the
 * database beforhand.
 * This will load all of our collections, too.
 */
static int
state_load(struct state *st, const char *name)
{

	if ( ! db_init(st->caldir, 0))
		return(-1);
	return(db_prncpl_load(&st->prncpl, name));
}

int
main(int argc, char *argv[])
{
	struct kreq	 r;
	struct kvalid	 valid[VALID__MAX] = {
		{ kvalid_body, valids[VALID_BODY] },
		{ kvalid_colour, valids[VALID_COLOUR] },
		{ kvalid_description, valids[VALID_DESCRIPTION] },
		{ kvalid_email, valids[VALID_EMAIL] },
		{ kvalid_string, valids[VALID_NAME] },
		{ kvalid_hash, valids[VALID_PASS] },
		{ kvalid_path, valids[VALID_PATH] } }; 
	struct state	*st;
	struct httpauth	 auth;
	int		 rc, v;
	const char	*caldir;
	char		*np;
	size_t		 i, sz;

	/* 
	 * This prevents spurrious line breaks from occuring in our
	 * debug or error log output.
	 */
	setlinebuf(stderr);

	st = NULL;
	caldir = NULL;
	v = 0;

	while (-1 != (rc = getopt(argc, argv, "v")))
		switch (rc) {
		case ('v'):
			v = 1;
			break;
		default:
			return(EXIT_FAILURE);
		}

	argv += optind;
	argc -= optind;

	/* Optionally set the caldir. */
	if (argc > 0)
		caldir = argv[0];

	if (KCGI_OK != khttp_parse
		(&r, valid, VALID__MAX, 
		 pages, PAGE__MAX, PAGE_INDEX))
		return(EXIT_FAILURE);

	verbose = v;

	/* 
	 * Begin by disallowing bogus HTTP methods and processing the
	 * OPTIONS method as well.
	 * Not all agents (e.g., Thunderbird's Lightning) are smart
	 * enough to resend an OPTIONS request with HTTP authorisation,
	 * so let this happen now.
	 */
	if (KMETHOD__MAX == r.method) {
		http_error(&r, KHTTP_405);
		goto out;
	} else if (KMETHOD_OPTIONS == r.method) {
		method_options(&r);
		goto out;
	}

	/* 
	 * Next, require that our HTTP authentication is in place.
	 * If it's not, then we're going to put up a bogus nonce value
	 * so that the client (whomever it is) sends us their login
	 * credentials and we can do more high-level authentication.
	 */
	if (KAUTH_DIGEST != r.rawauth.type) {
		http_error(&r, KHTTP_401);
		goto out;
	} else if (0 == r.rawauth.authorised) {
		kerrx("%s: bad HTTP authorisation tokens", r.fullpath);
		http_error(&r, KHTTP_401);
		goto out;
	} 

	/*
	 * Ok, we have enough information to actually begin processing
	 * this client request (i.e., we have some sort of username and
	 * hashed password), so allocate our state.
	 */
	if (NULL == (r.arg = st = calloc(1, sizeof(struct state)))) {
		kerr(NULL);
		http_error(&r, KHTTP_505);
		goto out;
	}

	/* 
	 * Copy HTTP authorisation. 
	 * This is just a hack to prevent kcgi(3) from being pulled into
	 * the supporting libraries for this system.
	 * However, while here, we also pull other information into the
	 * structure that we'll need for authentication: the request
	 * itself, the HTTP method, etc.
	 */
	memset(&auth, 0, sizeof(struct httpauth));

	if (r.rawauth.d.digest.alg == KHTTPALG_MD5_SESS)
		auth.alg = HTTPALG_MD5_SESS;
	else
		auth.alg = HTTPALG_MD5;
	if (r.rawauth.d.digest.qop == KHTTPQOP_NONE)
		auth.qop = HTTPQOP_NONE;
	else if (r.rawauth.d.digest.qop == KHTTPQOP_AUTH)
		auth.qop = HTTPQOP_AUTH;
	else 
		auth.qop = HTTPQOP_AUTH_INT;

	auth.user = r.rawauth.d.digest.user;
	auth.uri = r.rawauth.d.digest.uri;
	auth.realm = r.rawauth.d.digest.realm;
	auth.nonce = r.rawauth.d.digest.nonce;
	auth.cnonce = r.rawauth.d.digest.cnonce;
	auth.response = r.rawauth.d.digest.response;
	auth.count = r.rawauth.d.digest.count;
	auth.opaque = r.rawauth.d.digest.opaque;
	auth.method = kmethods[r.method];

	if (NULL != r.fieldmap && 
		 NULL != r.fieldmap[VALID_BODY]) {
		auth.req = r.fieldmap[VALID_BODY]->val;
		auth.reqsz = r.fieldmap[VALID_BODY]->valsz;
	} else if (NULL != r.fieldnmap && 
		 NULL != r.fieldnmap[VALID_BODY]) {
		auth.req = r.fieldnmap[VALID_BODY]->val;
		auth.reqsz = r.fieldnmap[VALID_BODY]->valsz;
	} else {
		auth.req = "";
		auth.reqsz = 0;
	}

	/* 
	 * First, validate our request paths.
	 * This is just a matter of copying them over.
	 */
	if ( ! http_paths(r.fullpath, &st->principal,
		 &st->collection, &st->resource))
		goto out;

	kdbg("%s: %s: /<%s>/<%s>/<%s>", 
		auth.user, kmethods[r.method], 
		st->principal, st->collection, st->resource);

	/* Copy over the calendar directory as well. */
	sz = strlcpy(st->caldir, NULL == caldir ? 
		CALDIR : caldir, sizeof(st->caldir));

	if (sz >= sizeof(st->caldir)) {
		kerrx("%s: caldir too long!", st->caldir);
		goto out;
	} else if ('/' == st->caldir[sz - 1])
		st->caldir[sz - 1] = '\0';

	/*
	 * Now load the requested principal and all of its collections
	 * and other stuff.
	 * We'll do all the authentication afterward: this just loads.
	 */
	if ((rc = state_load(st, auth.user)) < 0) {
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == rc) {
		http_error(&r, KHTTP_401);
		goto out;
	} 
	
	rc = prncpl_validate(st->prncpl, &auth);

	if (0 == rc) {
		kerrx("%s: bad authorisation", auth.user);
		http_error(&r, KHTTP_401);
		goto out;
	} 
	
#if 0
	if (strcmp(r.rawauth.d.digest.uri, st->rpath)) {
		kerrx("%s: bad authorisation URI", r.fullpath);
		http_error(&r, KHTTP_401);
		goto out;
	}
#endif

	kdbg("%s: principal validated", st->prncpl->name);

	/*
	 * Perform the steps required to check the nonce database
	 * without allowing an attacker to overwrite the database.
	 * If this clears, that means that the principal is real and not
	 * replaying prior HTTP authentications.
	 */
	if ((rc = httpauth_nonce(&auth, &np)) < -1) {
		http_error(&r, KHTTP_505);
		goto out;
	} else if (rc < 0) {
		http_error(&r, KHTTP_403);
		goto out;
	} else if (0 == rc) {
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_401]);
		khttp_head(&r, kresps[KRESP_WWW_AUTHENTICATE],
			"Digest realm=\"%s\", "
			"algorithm=\"MD5-sess\", "
			"qop=\"auth,auth-int\", "
			"nonce=\"%s\", "
			"stale=true", KREALM, np);
		khttp_body(&r);
		goto out;
	}

	/*
	 * If we're looking for HTML pages, then no need to load the
	 * configuration file, as we won't use it.
	 * Prior sections will require that a calendar configuration
	 * file exists for the requested URI.
	 * For HTML access (the browser), we don't care.
	 */
	if (KMIME_TEXT_HTML == r.mime && PAGE__MAX != r.page) {
		if (KMETHOD_GET == r.method) {
			method_dynamic_get(&r);
			goto out;
		} else if (KMETHOD_POST == r.method) {
			method_dynamic_post(&r);
			goto out;
		}
	}

	if (strcmp(st->principal, st->prncpl->name)) {
		kerrx("%s: requesting other principal "
			"collection", st->prncpl->name);
		http_error(&r, KHTTP_404);
		goto out;
	}
	/*
	 * If we're going to look for a calendar collection, try to do
	 * so now by querying the collections for our principal.
	 */
	if ('\0' != st->collection[0]) {
		for (i = 0; i < st->prncpl->colsz; i++) {
			if (strcmp(st->prncpl->cols[i].url, st->collection))
				continue;
			st->cfg = &st->prncpl->cols[i];
			break;
		}
		if (NULL == st->cfg) {
			kerrx("%s: requesting unknown "
				"collection", st->prncpl->name);
			http_error(&r, KHTTP_404);
			goto out;
		}
	}

	switch (r.method) {
	case (KMETHOD_PUT):
		method_put(&r);
		break;
	case (KMETHOD_PROPFIND):
		method_propfind(&r);
		break;
	case (KMETHOD_PROPPATCH):
		method_proppatch(&r);
		break;
	case (KMETHOD_POST):
		/*
		 * According to RFC 4918 section 9.5, we can implement
		 * POST on a collection any way, so ship it to the
		 * dynamic site for dynamic updates.
		 * POST to a resource, however, gets 405'd.
		 */
		if ('\0' == st->resource[0]) {
			method_dynamic_post(&r);
			break;
		}
		kerrx("%s: ignoring method %s",
			st->prncpl->name, kmethods[r.method]);
		http_error(&r, KHTTP_405);
		break;
	case (KMETHOD_GET):
		/*
		 * According to RFC 4918 section 9.4, GET for
		 * collections is undefined and we can do what we want.
		 * Thus, return an HTML page describing the collection.
		 * Otherwise, use the regular WebDAV handler.
		 */
		if ('\0' == st->resource[0])
			method_dynamic_get(&r);
		else
			method_get(&r);
		break;
	case (KMETHOD_REPORT):
		method_report(&r);
		break;
	case (KMETHOD_DELETE):
		method_delete(&r);
		break;
	default:
		kerrx("%s: ignoring method %s",
			st->prncpl->name, kmethods[r.method]);
		http_error(&r, KHTTP_405);
		break;
	}

out:
	khttp_free(&r);
	state_free(st);
	return(EXIT_SUCCESS);
}
