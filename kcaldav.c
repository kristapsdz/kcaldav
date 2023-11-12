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

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#if HAVE_MD5
# include <md5.h>
#endif
#if HAVE_SANDBOX_INIT
# include <sandbox.h> /* sandbox_init(3) */
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if !HAVE_ARC4RANDOM
# include <time.h> /* time(3) */
#endif
#if HAVE_PLEDGE
# include <unistd.h> /* pledge(2), unveil(2) */
#endif

#include <kcgi.h>
#include <kcgixml.h>
#include <sqlite3.h>

#include "libkcaldav.h"
#include "db.h"
#include "server.h"

static int verbose;

static const char *const pages[PAGE__MAX] = {
	"delcoln", /* PAGE_DELCOLN */
	"delproxy", /* PAGE_DELPROXY */
	"index", /* PAGE_INDEX */
	"logout", /* PAGE_LOGOUT */
	"modproxy", /* PAGE_MODPROXY */
	"newcoln", /* PAGE_NEWCOLN */
	"setcolnprops", /* PAGE_SETCOLNPROPS */
	"setemail", /* PAGE_SETEMAIL */
	"setpass", /* PAGE_SETPASS */
};

static const char *const valids[VALID__MAX] = {
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
	/*
	 * See whether our nonce lookup fails.
	 * This is still occuring over a read-only database, as an
	 * adversary could be playing us by submitting replay attacks
	 * (or random nonce values) over and over again in the hopes of
	 * filling up our nonce database.
	 */

	switch (db_nonce_validate(auth->nonce, auth->count)) {
	case NONCE_ERR:
		return (-2);
	case NONCE_REPLAY:
		return (-1);
	case NONCE_NOTFOUND:
		/*
		 * We don't have the nonce.
		 * This means that the client has either used one of our
		 * bogus initial nonces or is using one from a much
		 * earlier session.
		 * Tell them to retry with a new nonce.
		 */
		if (!db_nonce_new(np))
			return (-2);
		return 0;
	default:
		break;
	} 

	/*
	 * Now we actually update our nonce file.
	 * We only get here if the nonce value exists and is fresh.
	 */

	switch (db_nonce_update(auth->nonce, auth->count)) {
	case NONCE_ERR:
		return (-2);
	case NONCE_REPLAY:
		return (-1);
	case NONCE_NOTFOUND:
		if (!db_nonce_new(np))
			return (-2);
		return 0;
	default:
		break;
	} 

	return 1;
}

/*
 * The name of a calendar.
 * Less than... 1K?
 */
static int
kvalid_name(struct kpair *kp)
{

	if (!kvalid_stringne(kp))
		return 0;
	return (kp->valsz < 1024);
}

/*
 * The description of a calendar.
 * Make sure this is less than 4K.
 */
static int
kvalid_description(struct kpair *kp)
{

	if (!kvalid_stringne(kp))
		return 0;
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

	if (!kvalid_stringne(kp))
		return 0;
	else if (kp->valsz > 256)
		return 0;

	return http_safe_string(kp->val);
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

	if (!kvalid_stringne(kp))
		return 0;
	if (kp->valsz != 7 && kp->valsz != 9)
		return 0;
	if (kp->val[0] != '#')
		return 0;

	for (i = 1; i < kp->valsz; i++) {
		if (isdigit((unsigned char)kp->val[i]))
			continue;
		if (isalpha((unsigned char)kp->val[i]) && 
		    ((kp->val[i] >= 'a' && kp->val[i] <= 'f') ||
		     (kp->val[i] >= 'A' && kp->val[i] <= 'F')))
			continue;
		return 0;
	}

	return 1;
}

/*
 * Validate a password MD5 hash.
 * These are fixed length and with fixed characteristics.
 */
static int
kvalid_hash(struct kpair *kp)
{
	size_t	 i;

	if (!kvalid_stringne(kp))
		return 0;
	if (kp->valsz != 32)
		return 0;

	for (i = 0; i < kp->valsz; i++) {
		if (isdigit((unsigned char)kp->val[i]))
			continue;
		if (isalpha((unsigned char)kp->val[i]) && 
		    islower((unsigned char)kp->val[i]) &&
		    kp->val[i] >= 'a' && kp->val[i] <= 'f')
			continue;
		return 0;
	}

	return 1;
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

	if (kp->ctypepos == KMIME_TEXT_CALENDAR) {
		ical = ical_parse(NULL, kp->val, kp->valsz, NULL, NULL);
		ical_free(ical);
		return (ical != NULL);
	}

	/* Try to parse an XML file. */

	dav = caldav_parse(kp->val, kp->valsz, NULL);
	caldav_free(dav);
	return (dav != NULL);
}

/*
 * Our proxy bits are either 1 or 2.
 */
static int
kvalid_proxy_bits(struct kpair *kp)
{
	if (!kvalid_uint(kp))
		return 0;
	return kp->parsed.i == 1 || kp->parsed.i == 2;
}

/*
 * Provide a version of kutil_verr that doesn't exit.
 */
static void
kutil_verr_noexit(struct kreq *r,
	const char *id, const char *fmt, va_list ap)
{

	kutil_vlog(r, "ERROR", id, fmt, ap);
}

/*
 * Provide a version of kutil_verrx that doesn't exit.
 */
static void
kutil_verrx_noexit(struct kreq *r,
	const char *id, const char *fmt, va_list ap)
{

	kutil_vlogx(r, "ERROR", id, fmt, ap);
}

void
kutil_err_noexit(struct kreq *r, const char *id, const char *fmt, ...)
{
	va_list	 ap;

	va_start(ap, fmt);
	kutil_verr_noexit(r, id, fmt, ap);
	va_end(ap);
}

void
kutil_errx_noexit(struct kreq *r, const char *id, const char *fmt, ...)
{
	va_list	 ap;

	va_start(ap, fmt);
	kutil_verrx_noexit(r, id, fmt, ap);
	va_end(ap);
}

static void
kutil_vdbg(struct kreq *r, const char *id, const char *fmt, va_list ap)
{

	if (verbose >= 1)
		kutil_vlogx(r, "INFO", id, fmt, ap);
}

void
kutil_dbg(struct kreq *r, const char *id, const char *fmt, ...)
{
	va_list	 ap;

	va_start(ap, fmt);
	kutil_vdbg(r, id, fmt, ap);
	va_end(ap);
}

static void
db_msg_info(void *arg, const char *id, const char *fmt, va_list ap)
{

	if (verbose >= 2)
		kutil_vlogx(arg, "DB-INFO", id, fmt, ap);
}

static void
db_msg_err(void *arg, const char *id, const char *fmt, va_list ap)
{

	if (verbose >= 2)
		kutil_vlogx(arg, "DB-ERR", id, fmt, ap);
}

static void
db_msg_errx(void *arg, const char *id, const char *fmt, va_list ap)
{

	if (verbose >= 2)
		kutil_vlogx(arg, "DB-ERR", id, fmt, ap);
}

static void
db_msg_dbg(void *arg, const char *id, const char *fmt, va_list ap)
{

	if (verbose >= 2)
		kutil_vlogx(arg, "DB-DEBUG", id, fmt, ap);
}

static void
state_free(struct state *st)
{

	if (st == NULL)
		return;

	if (st->prncpl != st->rprncpl)
		db_prncpl_free(st->rprncpl);
	db_prncpl_free(st->prncpl);
	free(st->principal);
	free(st->collection);
	free(st->resource);
	free(st);
}

/*
 * Load our principal account into the state object, priming the
 * database beforhand.
 * This will load all of our collections, too.
 * Returns <0 on fatal error, 0 if the principal doesn't exist, >0 on
 * success.
 */
static int
state_load(struct kreq *r, struct state *st,
	const char *nonce, const char *name)
{
	int	 rc;

	db_set_msg_arg(r);
	db_set_msg_dbg(db_msg_dbg);
	db_set_msg_info(db_msg_info);
	db_set_msg_err(db_msg_err);
	db_set_msg_errx(db_msg_errx);

	if (!db_init(st->caldir, 0))
		return(-1);

	st->nonce = nonce;
	if ((rc = db_prncpl_load(&st->prncpl, name)) <= 0)
		return rc;

	db_set_msg_ident(name);
	return 1;
}

int
main(void)
{
	struct kreq	 r;
	struct kvalid	 valid[VALID__MAX] = {
		{ kvalid_proxy_bits, valids[VALID_BITS] },
		{ kvalid_body, valids[VALID_BODY] },
		{ kvalid_colour, valids[VALID_COLOUR] },
		{ kvalid_description, valids[VALID_DESCRIPTION] },
		{ kvalid_email, valids[VALID_EMAIL] },
		{ kvalid_int, valids[VALID_ID] },
		{ kvalid_name, valids[VALID_NAME] },
		{ kvalid_hash, valids[VALID_PASS] },
		{ kvalid_path, valids[VALID_PATH] } }; 
	struct state	*st = NULL;
	char		*np, *logfile = NULL;
	struct conf	 conf;
	const char	*cfgfile = NULL;
	size_t		 i, sz;
	enum kcgi_err	 er;
	int		 rc;

	/*
	 * Hard-coded configuration file path.  If unset or set to an
	 * empty string, no configuration file is used.
	 */
#ifdef CFGFILE
	cfgfile = CFGFILE;
#endif

	/* Needed for non-OpenBSD systems for a weak PRNG. */

#if !HAVE_ARC4RANDOM
	srandom(time(NULL));
#endif

	/* Pledge allowing some file-system access. */

#if HAVE_PLEDGE
	if (pledge("proc stdio rpath cpath wpath flock fattr unveil", NULL) == -1)
		kutil_err(NULL, NULL, "pledge");
#endif

	/* Read run-time configuration.  Act upon it then free. */

	if ((rc = conf_read(cfgfile, &conf)) == -1)
		kutil_err(NULL, NULL, "%s", cfgfile);
	else if (rc == 0)
		kutil_errx(NULL, NULL, "%s: malformed", cfgfile);

	verbose = conf.verbose;
	if (conf.logfile != NULL && *conf.logfile != '\0')
		if (!kutil_openlog(conf.logfile))
			kutil_err(NULL, NULL, "%s", logfile);

	free(conf.logfile);
	memset(&conf, 0, sizeof(struct conf));
	
	/* Parse the main body. */

	er = khttp_parsex
		(&r, ksuffixmap, kmimetypes, KMIME__MAX, valid, 
		 VALID__MAX, pages, PAGE__MAX, KMIME_TEXT_HTML,
		 PAGE_INDEX, NULL, NULL, verbose >= 3 ? 
		 (KREQ_DEBUG_WRITE | KREQ_DEBUG_READ_BODY) : 0, 
		 NULL);

	if (er != KCGI_OK)
		kutil_errx(NULL, NULL, 
			"khttp_parse: %s", kcgi_strerror(er));

	/*
	 * Tighten the sandbox: drop proc and only allow for the calendar
	 * directory to be accessed.
	 */

#if HAVE_SANDBOX_INIT
	rc = sandbox_init(kSBXProfileNoInternet, SANDBOX_NAMED, &np);
	if (rc == -1)
		kutil_errx(NULL, NULL, "sandbox_init: %s", np);
#endif

#if HAVE_PLEDGE
	/*
	 * Directories required by sqlite3.
	 */
	if (unveil(CALDIR, "rwxc") == -1)
		kutil_err(NULL, NULL, "unveil");
	if (unveil("/tmp", "rwxc") == -1)
		kutil_err(NULL, NULL, "unveil");
	if (unveil("/var/tmp", "rwxc") == -1)
		kutil_err(NULL, NULL, "unveil");
	if (unveil("/dev", "rwx") == -1)
		kutil_err(NULL, NULL, "unveil");
	if (pledge("stdio rpath cpath wpath flock fattr", NULL) == -1)
		kutil_err(NULL, NULL, "pledge");
#endif

	/* 
	 * Begin by disallowing bogus HTTP methods and processing the
	 * OPTIONS method as well.
	 * Not all agents (e.g., Thunderbird's Lightning) are smart
	 * enough to resend an OPTIONS request with HTTP authorisation,
	 * so let this happen now.
	 */

	if (r.method == KMETHOD__MAX) {
		http_error(&r, KHTTP_405);
		goto out;
	} else if (r.method == KMETHOD_OPTIONS) {
		method_options(&r);
		goto out;
	}

	/* 
	 * Next, require that our HTTP authentication is in place.
	 * If it's not, then we're going to put up a bogus nonce value
	 * so that the client (whomever it is) sends us their login
	 * credentials and we can do more high-level authentication.
	 */

	if (r.rawauth.type != KAUTH_DIGEST) {
		http_error(&r, KHTTP_401);
		goto out;
	} else if (r.rawauth.authorised == 0) {
		kutil_warnx(&r, NULL, "bad HTTP authorisation");
		http_error(&r, KHTTP_401);
		goto out;
	} 

	/*
	 * Ok, we have enough information to actually begin processing
	 * this client request (i.e., we have some sort of username and
	 * hashed password), so allocate our state.
	 */

	r.arg = st = kcalloc(1, sizeof(struct state));

	if (r.fullpath[0] == '\0') {
		np = khttp_urlabs(r.scheme, r.host, r.port, r.pname,
			(char *)NULL);
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

	if (!http_paths(r.fullpath,
	     &st->principal, &st->collection, &st->resource))
		goto out;

	/* Copy over the calendar directory as well. */

	sz = strlcpy(st->caldir, CALDIR, sizeof(st->caldir));

	if (sz >= sizeof(st->caldir)) {
		kutil_errx_noexit(&r, NULL, "caldir too long");
		http_error(&r, KHTTP_505);
		goto out;
	} else if (st->caldir[sz - 1] == '/')
		st->caldir[sz - 1] = '\0';

	/*
	 * Now load the requested principal and all of its collections
	 * and other stuff.
	 * We'll do all the authentication afterward: this just loads.
	 */

	rc = state_load(&r, st, 
		r.rawauth.d.digest.nonce, 
		r.rawauth.d.digest.user);

	if (rc < 0) {
		http_error(&r, KHTTP_505);
		goto out;
	} else if (rc == 0) {
		http_error(&r, KHTTP_401);
		goto out;
	} 

	rc = khttpdigest_validatehash(&r, st->prncpl->hash);
	if (rc < 0) {
		kutil_warnx(&r, NULL, "bad authorisation sequence");
		http_error(&r, KHTTP_401);
		goto out;
	} else if (rc == 0) {
		kutil_warnx(&r, NULL, "failed authorisation sequence");
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

	/*
	 * Perform the steps required to check the nonce database
	 * without allowing an attacker to overwrite the database.
	 * If this clears, that means that the principal is real and not
	 * replaying prior HTTP authentications.
	 */

	if ((rc = nonce_validate(&r.rawauth.d.digest, &np)) < -1) {
		kutil_errx_noexit(&r, st->prncpl->name, 
			"cannot validate nonce");
		http_error(&r, KHTTP_505);
		goto out;
	} else if (rc < 0) {
		kutil_warnx(&r, st->prncpl->name, 
			"nonce replay attack");
		http_error(&r, KHTTP_403);
		goto out;
	} else if (rc == 0) {
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

	if (r.mime == KMIME_APP_JSON &&
	    (r.method == KMETHOD_GET || r.method == KMETHOD_POST)) {
		method_json(&r);
		goto out;
	} 

	/* 
	 * The client is probing.
	 * Send them to the server root.
	 */

	if (st->principal[0] == '\0') {
		np = khttp_urlabs(r.scheme, r.host, r.port, r.pname,
			(char *)NULL);
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
		rc = db_prncpl_load
			(&st->rprncpl, st->principal);
		if (rc < 0) {
			http_error(&r, KHTTP_505);
			goto out;
		} else if (rc == 0) {
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
			kutil_warnx(&r, st->prncpl->name,
				"disallowed reverse proxy "
				"on principal: %s",
				st->rprncpl->email);
			http_error(&r, KHTTP_403);
			goto out;
		}
		st->proxy = st->rprncpl->proxies[i].bits;

		switch (r.method) {
		case KMETHOD_PUT:
		case KMETHOD_PROPPATCH:
		case KMETHOD_DELETE:
			/* Implies read. */
			if (st->proxy == PROXY_WRITE)
				break;
			kutil_warnx(&r, st->prncpl->name,
				"disallowed reverse proxy "
				"write on principal: %s",
				st->rprncpl->email);
			http_error(&r, KHTTP_403);
			goto out;
		default:
			if (st->proxy == PROXY_READ || 
			    st->proxy == PROXY_WRITE)
				break;
			kutil_warnx(&r, st->prncpl->name,
				"disallowed reverse proxy "
				"read on principal: %s",
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

	if (st->collection[0] != '\0') {
		for (i = 0; i < st->rprncpl->colsz; i++) {
			if (strcmp(st->rprncpl->cols[i].url, st->collection))
				continue;
			st->cfg = &st->rprncpl->cols[i];
			break;
		}
		if (st->cfg == NULL &&
		    strcmp(st->collection, "calendar-proxy-read") &&
  		    strcmp(st->collection, "calendar-proxy-write")) {
			kutil_warnx(&r, st->prncpl->name,
				"request unknown collection");
			http_error(&r, KHTTP_404);
			goto out;
		}
	}

	switch (r.method) {
	case KMETHOD_PUT:
		method_put(&r);
		break;
	case KMETHOD_PROPFIND:
		method_propfind(&r);
		break;
	case KMETHOD_PROPPATCH:
		method_proppatch(&r);
		break;
	case KMETHOD_POST:
		/*
		 * According to RFC 4918 section 9.5, we can implement
		 * POST on a collection any way, so ship it to the
		 * dynamic site for dynamic updates.
		 * POST to a resource, however, gets 405'd.
		 */

		if (st->resource[0] == '\0') {
			kutil_warnx(&r, st->prncpl->name,
				"ignore POST to collection");
			http_error(&r, KHTTP_404);
		} else {
			kutil_warnx(&r, st->prncpl->name,
				"bad POST to resource");
			http_error(&r, KHTTP_405);
		}
		break;
	case KMETHOD_GET:
		/*
		 * According to RFC 4918 section 9.4, GET for
		 * collections is undefined and we can do what we want.
		 * Thus, return an HTML page describing the collection.
		 * Otherwise, use the regular WebDAV handler.
		 */

		if (st->resource[0] == '\0') {
			kutil_warnx(&r, st->prncpl->name,
				"ignore GET of collection");
			http_error(&r, KHTTP_404);
		} else
			method_get(&r);
		break;
	case KMETHOD_REPORT:
		method_report(&r);
		break;
	case KMETHOD_DELETE:
		method_delete(&r);
		break;
	default:
		kutil_warnx(&r, st->prncpl->name,
			"ignore unsupported HTTP method: %s",
			kmethods[r.method]);
		http_error(&r, KHTTP_405);
		break;
	}

out:
	khttp_free(&r);
	db_set_msg_ident(NULL);
	db_set_msg_arg(NULL);
	state_free(st);
	return EXIT_SUCCESS;
}
