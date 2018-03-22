/*	$Id$ */
/*
 * Copyright (c) 2015, 2016 Kristaps Dzonsons <kristaps@bsd.lv>
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

#if 0
#include <sys/types.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <sys/ktrace.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#if HAVE_SANDBOX_INIT
# include <sandbox.h>
#endif
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
#include "md5.h"

#ifndef CALDIR
#error "CALDIR token not defined!"
#endif

int verbose = 0;

const char *const pages[PAGE__MAX] = {
	"delcoln", /* PAGE_DELCOLN */
	"index", /* PAGE_INDEX */
	"logout", /* PAGE_LOGOUT */
	"modproxy", /* PAGE_MODPROXY */
	"newcoln", /* PAGE_NEWCOLN */
	"setcolnprops", /* PAGE_SETCOLNPROPS */
	"setemail", /* PAGE_SETEMAIL */
	"setpass", /* PAGE_SETPASS */
};

const char *const valids[VALID__MAX] = {
	"bits", /* VALID_BITS */
	"", /* VALID_BODY */
	"colour", /* VALID_COLOUR */
	"description", /* VALID_DESCRIPTION */
	"email", /* VALID_EMAIL */
	"id", /* VALID_ID */
	"name", /* VALID_NAME */
	"pass", /* VALID_PASS */
	"path", /* VALID_PATH */
};

/*
 * Run a series of checks for the nonce validity.
 * This requires us to first open the nonce database read-only and see
 * if we've seen the nonce or not.
 * If we have and it's a replay--bam.
 * Otherwise, re-open the database read-writable and check again.
 * If we find it and it's a replay--bam.
 * If we find it and it's not, update the nonce count.
 * If we don't find it, start over.
 * Return -2 on system failure, -1 on replay, 0 on stale, 1 on ok.
 */
static int
nonce_validate(const struct khttpdigest *auth, char **np)
{
	enum nonceerr	 er;

	/*
	 * Now we see whether our nonce lookup fails.
	 * This is still occuring over a read-only database, as an
	 * adversary could be playing us by submitting replay attacks
	 * (or random nonce values) over and over again in the hopes of
	 * filling up our nonce database.
	 */
	er = db_nonce_validate(auth->nonce, auth->count);

	if (NONCE_ERR == er) {
		kerrx("%s: nonce database failure", auth->user);
		return(-2);
	} else if (NONCE_NOTFOUND == er) {
		/*
		 * We don't have the nonce.
		 * This means that the client has either used one of our
		 * bogus initial nonces or is using one from a much
		 * earlier session.
		 * Tell them to retry with a new nonce.
		 */
		if ( ! db_nonce_new(np)) {
			kerrx("%s: nonce database failure", auth->user);
			return(-2);
		}
		return(0);
	} else if (NONCE_REPLAY == er) {
		kerrx("%s: REPLAY ATTACK\n", auth->user);
		return(-1);
	} 

	/*
	 * Now we actually update our nonce file.
	 * We only get here if the nonce value exists and is fresh.
	 */
	er = db_nonce_update(auth->nonce, auth->count);

	if (NONCE_ERR == er) {
		kerrx("%s: nonce database failure", auth->user);
		return(-2);
	} else if (NONCE_NOTFOUND == er) {
		kerrx("%s: nonce update not found?", auth->user);
		if ( ! db_nonce_new(np)) {
			kerrx("%s: nonce database failure", auth->user);
			return(-2);
		}
		return(0);
	} else if (NONCE_REPLAY == er) {
		kerrx("%s: REPLAY ATTACK\n", auth->user);
		return(-1);
	} 

	return(1);
}

/*
 * The name of a calendar.
 * Less than... 1K?
 */
static int
kvalid_name(struct kpair *kp)
{

	if ( ! kvalid_stringne(kp))
		return(0);
	return (kp->valsz < 1024);
}

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
	default:
		/* Try to parse an XML file. */
		dav = caldav_parse(kp->val, kp->valsz);
		caldav_free(dav);
		return(NULL != dav);
	}
}

static void
state_free(struct state *st)
{

	if (NULL == st)
		return;
	if (st->prncpl != st->rprncpl)
		prncpl_free(st->rprncpl);
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
state_load(struct state *st, const char *nonce, const char *name)
{

	if ( ! db_init(st->caldir, 0))
		return(-1);
	st->nonce = nonce;
	return(db_prncpl_load(&st->prncpl, name));
}

int
main(void)
{
	struct kreq	 r;
	struct kvalid	 valid[VALID__MAX] = {
		{ kvalid_uint, valids[VALID_BITS] },
		{ kvalid_body, valids[VALID_BODY] },
		{ kvalid_colour, valids[VALID_COLOUR] },
		{ kvalid_description, valids[VALID_DESCRIPTION] },
		{ kvalid_email, valids[VALID_EMAIL] },
		{ kvalid_int, valids[VALID_ID] },
		{ kvalid_name, valids[VALID_NAME] },
		{ kvalid_hash, valids[VALID_PASS] },
		{ kvalid_path, valids[VALID_PATH] } }; 
	struct state	*st;
	int		 rc;
	char		*np;
	size_t		 i, sz;

	/* 
	 * This prevents spurrious line breaks from occuring in our
	 * debug or error log output.
	 */
	freopen(LOGFILE, "a", stderr);
	setlinebuf(stderr);

	st = NULL;
#if defined DEBUG && DEBUG > 1
	verbose = 2;
#elif defined DEBUG && DEBUG > 0
	verbose = 1;
#endif

	if (KCGI_OK != khttp_parsex
	    (&r, ksuffixmap, kmimetypes, KMIME__MAX, 
	     valid, VALID__MAX, pages, PAGE__MAX, 
	     KMIME_TEXT_HTML, PAGE_INDEX,
	     NULL, NULL, verbose > 1 ? 
	     KREQ_DEBUG_WRITE | KREQ_DEBUG_READ_BODY : 0, 
	     NULL))
		return(EXIT_FAILURE);

#if 0
	ktrace("/tmp/kcaldav.trace", KTROP_SET,
		KTRFAC_SYSCALL |
		KTRFAC_SYSRET |
		KTRFAC_NAMEI |
		KTRFAC_GENIO |
		KTRFAC_PSIG |
		KTRFAC_EMUL |
		KTRFAC_CSW |
		KTRFAC_STRUCT |
		KTRFAC_USER |
		KTRFAC_INHERIT, 
		getpid());
#endif

#if HAVE_SANDBOX_INIT
	rc = sandbox_init
		(kSBXProfileNoInternet, 
		 SANDBOX_NAMED, &np);
	if (-1 == rc) {
		kerrx("sandbox_init: %s", np);
		goto out;
	}
#endif

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

	if ('\0' == r.fullpath[0]) {
		np = kutil_urlabs(r.scheme, r.host, r.port, r.pname);
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_307]);
	        khttp_head(&r, kresps[KRESP_CONTENT_TYPE], 
			"%s", kmimetypes[r.mime]);
		khttp_head(&r, kresps[KRESP_LOCATION], 
			"%s/", np);
		khttp_body(&r);
		khttp_puts(&r, "Redirecting...");
		free(np);
		goto out;
	}

	/* 
	 * First, validate our request paths.
	 * This is just a matter of copying them over.
	 */
	if ( ! http_paths(r.fullpath, &st->principal,
		 &st->collection, &st->resource))
		goto out;

	kdbg("%s: %s: /<%s>/<%s>/<%s>", 
		r.rawauth.d.digest.user, kmethods[r.method], 
		st->principal, st->collection, st->resource);

	/* Copy over the calendar directory as well. */
	sz = strlcpy(st->caldir, CALDIR, sizeof(st->caldir));

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
	rc = state_load(st, 
		r.rawauth.d.digest.nonce, 
		r.rawauth.d.digest.user);

	if (rc < 0) {
		http_error(&r, KHTTP_505);
		goto out;
	} else if (0 == rc) {
		http_error(&r, KHTTP_401);
		goto out;
	} 

	rc = khttpdigest_validatehash(&r, st->prncpl->hash);
	if (rc < 0) {
		kerrx("%s: bad authorisation sequence", st->prncpl->email);
		http_error(&r, KHTTP_401);
		goto out;
	} else if (0 == rc) {
		kerrx("%s: failed authorisation sequence", st->prncpl->email);
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
	if ((rc = nonce_validate(&r.rawauth.d.digest, &np)) < -1) {
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
	if (KMIME_APP_JSON == r.mime &&
	    (KMETHOD_GET == r.method ||
	     KMETHOD_POST == r.method)) {
		method_json(&r);
		goto out;
	} 

	/* 
	 * The client is probing.
	 * Send them to the server root.
	 */
	if ('\0' == st->principal[0]) {
		kdbg("%s: redirecting probe from client", 
			st->prncpl->email);
		np = kutil_urlabs(r.scheme, r.host, r.port, r.pname);
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_307]);
	        khttp_head(&r, kresps[KRESP_CONTENT_TYPE], 
			"%s", kmimetypes[r.mime]);
		khttp_head(&r, kresps[KRESP_LOCATION], 
			"%s/%s/", np, st->prncpl->name);
		khttp_body(&r);
		khttp_puts(&r, "Redirecting...");
		free(np);
		goto out;
	}

	if (strcmp(st->principal, st->prncpl->name)) {
		kdbg("%s: requesting other principal "
			"collection", st->prncpl->name);
		rc = db_prncpl_load
			(&st->rprncpl, st->principal);
		if (rc < 0) {
			http_error(&r, KHTTP_505);
			goto out;
		} else if (0 == rc) {
			http_error(&r, KHTTP_401);
			goto out;
		}
		/* 
		 * Look us up in the requested principal's proxies,
		 * i.e., those who are allowed to proxy as the given
		 * principal.
		 */
		for (i = 0; i < st->rprncpl->proxiesz; i++)
			if (st->prncpl->id ==
			    st->rprncpl->proxies[i].proxy)
				break;
		if (i == st->rprncpl->proxiesz) {
			kerrx("%s: disallowed reverse proxy "
				"on principal: %s",
				st->prncpl->email,
				st->rprncpl->email);
			http_error(&r, KHTTP_403);
			goto out;
		}
		st->proxy = st->rprncpl->proxies[i].bits;
		switch (r.method) {
		case (KMETHOD_PUT):
		case (KMETHOD_PROPPATCH):
		case (KMETHOD_DELETE):
			/* Implies read. */
			if (PROXY_WRITE == st->proxy)
				break;
			kerrx("%s: disallowed reverse proxy "
				"write on principal: %s",
				st->prncpl->email,
				st->rprncpl->email);
			http_error(&r, KHTTP_403);
			goto out;
		default:
			if (PROXY_READ == st->proxy || 
			    PROXY_WRITE == st->proxy)
				break;
			kerrx("%s: disallowed reverse proxy "
				"read on principal: %s",
				st->prncpl->email,
				st->rprncpl->email);
			http_error(&r, KHTTP_403);
			goto out;
		}
	} else
		st->rprncpl = st->prncpl;

	/*
	 * If we're going to look for a calendar collection, try to do
	 * so now by querying the collections for our principal.
	 */
	if ('\0' != st->collection[0]) {
		for (i = 0; i < st->rprncpl->colsz; i++) {
			if (strcmp(st->rprncpl->cols[i].url, st->collection))
				continue;
			st->cfg = &st->rprncpl->cols[i];
			break;
		}
		if (NULL == st->cfg &&
		    strcmp(st->collection, "calendar-proxy-read") &&
  		    strcmp(st->collection, "calendar-proxy-write")) {
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
			kerrx("%s: ignoring post to collection",
				st->prncpl->name);
			http_error(&r, KHTTP_404);
		} else {
			kerrx("%s: bad post to resource",
				st->prncpl->name);
			http_error(&r, KHTTP_405);
		}
		break;
	case (KMETHOD_GET):
		/*
		 * According to RFC 4918 section 9.4, GET for
		 * collections is undefined and we can do what we want.
		 * Thus, return an HTML page describing the collection.
		 * Otherwise, use the regular WebDAV handler.
		 */
		if ('\0' == st->resource[0]) {
			kerrx("%s: ignoring get of collection",
				st->prncpl->name);
			http_error(&r, KHTTP_404);
		} else
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
