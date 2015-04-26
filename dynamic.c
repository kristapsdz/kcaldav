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
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>
#include <kcgihtml.h>

#include "extern.h"
#include "kcaldav.h"

enum	templ {
	TEMPL_BYTES_AVAIL,
	TEMPL_BYTES_USED,
	TEMPL_CLASS_COLLECTION_WRITABLE,
	TEMPL_CLASS_INSECURE,
	TEMPL_CLASS_READONLY,
	TEMPL_CLASS_WRITABLE,
	TEMPL_COLLECTION_WRITABLE,
	TEMPL_COLOUR,
	TEMPL_DESCRIPTION,
	TEMPL_DISPLAYNAME,
	TEMPL_EMAIL,
	TEMPL_FULLPATH,
	TEMPL_HTURI,
	TEMPL_NAME,
	TEMPL_URI,
	TEMPL_PAGE_EMAIL,
	TEMPL_PAGE_HOME,
	TEMPL_PAGE_PASS,
	TEMPL_PRIVS,
	TEMPL_REALM,
	TEMPL_VALID_COLOUR,
	TEMPL_VALID_EMAIL,
	TEMPL_VALID_NAME,
	TEMPL_VALID_PASS,
	TEMPL_VERSION,
	TEMPL__MAX
};

static	const char *const templs[TEMPL__MAX] = {
	"bytes-avail", /* TEMPL_BYTES_AVAIL */
	"bytes-used", /* TEMPL_BYTES_USED */
	"class-collection-writable", /* TEMPL_CLASS_COLLECTION_WRITABLE */
	"class-insecure", /* TEMPL_CLASS_INSECURE */
	"class-readonly", /* TEMPL_CLASS_READONLY */
	"class-writable", /* TEMPL_CLASS_WRITABLE */
	"collection-writable", /* TEMPL_COLLECTION_WRITABLE */
	"colour", /* TEMPL_COLOUR */
	"description", /* TEMPL_DESCRIPTION */
	"displayname", /* TEMPL_DISPLAYNAME */
	"email", /* TEMPL_EMAIL */
	"fullpath", /* TEMPL_FULLPATH */
	"hturi", /* TEMPL_HTURI */
	"name", /* TEMPL_NAME */
	"uri", /* TEMPL_URI */
	"page-email", /* TEMPL_PAGE_EMAIL */
	"page-home", /* TEMPL_PAGE_HOME */
	"page-pass", /* TEMPL_PAGE_PASS */
	"privileges", /* TEMPL_PRIVS */
	"realm", /* TEMPL_REALM */
	"valid-colour", /* TEMPL_VALID_COLOUR */
	"valid-email", /* TEMPL_VALID_EMAIL */
	"valid-name", /* TEMPL_VALID_NAME */
	"valid-pass", /* TEMPL_VALID_PASS */
	"version", /* TEMPL_VERSION */
};

static int
dotemplate(size_t key, void *arg)
{
	struct kreq	*r = arg;
	struct khtmlreq  req;
	struct state	*st = r->arg;
	char		 buf[32];
	size_t		 i;

	memset(&req, 0, sizeof(struct khtmlreq));
	req.req = r;

	switch (key) {
	case (TEMPL_BYTES_AVAIL):
		if (NULL == st->cfg)
			break;
		if (st->cfg->bytesavail > 1073741824) {
			snprintf(buf, sizeof(buf), "%.2f GB", 
				st->cfg->bytesavail / 
				(double)1073741824);
		} else if (st->cfg->bytesavail > 1048576) {
			snprintf(buf, sizeof(buf), "%.2f GB", 
				st->cfg->bytesavail / 
				(double)1048576);
		} else {
			snprintf(buf, sizeof(buf), "%.2f KB", 
				st->cfg->bytesavail / 
				(double)1024);
		}
		khtml_puts(&req, buf);
		break;
	case (TEMPL_BYTES_USED):
		if (NULL == st->cfg)
			break;
		if (st->cfg->bytesused > 1073741824) {
			snprintf(buf, sizeof(buf), "%.2f GB", 
				st->cfg->bytesused / 
				(double)1073741824);
		} else if (st->cfg->bytesused > 1048576) {
			snprintf(buf, sizeof(buf), "%.2f GB", 
				st->cfg->bytesused / 
				(double)1048576);
		} else {
			snprintf(buf, sizeof(buf), "%.2f KB", 
				st->cfg->bytesused / 
				(double)1024);
		}
		khtml_puts(&req, buf);
		break;
	case (TEMPL_CLASS_COLLECTION_WRITABLE):
		if (NULL == st->cfg)
			break;
		if ( ! (PERMS_WRITE & st->cfg->perms))
			khtml_puts(&req, "noshow");
		else if ( ! st->cfg->writable)
			khtml_puts(&req, "noshow");
		break;
	case (TEMPL_CLASS_INSECURE):
		if (KSCHEME_HTTPS == r->scheme)
			khtml_puts(&req, "noshow");
		break;
	case (TEMPL_CLASS_READONLY):
		if (st->prncpl->writable)
			khtml_puts(&req, "noshow");
		break;
	case (TEMPL_CLASS_WRITABLE):
		if ( ! st->prncpl->writable)
			khtml_puts(&req, "noshow");
		break;
	case (TEMPL_COLLECTION_WRITABLE):
		khtml_puts(&req, st->cfg->writable ?
			"writable" : "read-only");
		break;
	case (TEMPL_COLOUR):
		if (NULL == st->cfg)
			break;
		/*
		 * HTML5 only accepts a six-alnum hexadecimal value.
		 * However, we encode our colour as RGBA.
		 * Thus, truncate the vaue.
		 */
		khtml_putc(&req, '#');
		for (i = 1; i < 7; i++)
			khtml_putc(&req, st->cfg->colour[i]);
		break;
	case (TEMPL_DESCRIPTION):
		if (NULL == st->cfg)
			break;
		khtml_puts(&req, st->cfg->description);
		break;
	case (TEMPL_DISPLAYNAME):
		if (NULL == st->cfg)
			break;
		khtml_puts(&req, st->cfg->displayname);
		break;
	case (TEMPL_EMAIL):
		khtml_puts(&req, st->prncpl->email);
		break;
	case (TEMPL_FULLPATH):
		khtml_puts(&req, r->pname);
		khtml_puts(&req, r->fullpath);
		break;
	case (TEMPL_HTURI):
		khtml_puts(&req, HTDOCS);
		break;
	case (TEMPL_NAME):
		khtml_puts(&req, st->prncpl->name);
		break;
	case (TEMPL_URI):
		khtml_puts(&req, kschemes[r->scheme]);
		khtml_puts(&req, "://");
		khtml_puts(&req, r->host);
		khtml_puts(&req, r->pname);
		khtml_puts(&req, st->prncpl->homedir);
		break;
	case (TEMPL_PAGE_EMAIL):
		khtml_puts(&req, r->pname);
		khtml_putc(&req, '/');
		khtml_puts(&req, pages[PAGE_SETEMAIL]);
		khtml_puts(&req, ".html");
		break;
	case (TEMPL_PAGE_HOME):
		khtml_puts(&req, r->pname);
		khtml_putc(&req, '/');
		khtml_puts(&req, pages[PAGE_INDEX]);
		khtml_puts(&req, ".html");
		break;
	case (TEMPL_PAGE_PASS):
		khtml_puts(&req, r->pname);
		khtml_putc(&req, '/');
		khtml_puts(&req, pages[PAGE_SETPASS]);
		break;
	case (TEMPL_PRIVS):
		if (NULL == st->cfg)
			break;
		if ( ! (PERMS_READ & st->cfg->perms))
			break;
		khtml_puts(&req, "read");
		if (PERMS_WRITE & st->cfg->perms &&
			 PERMS_DELETE & st->cfg->perms)
			khtml_puts(&req, ", write, and delete");
		else if (PERMS_WRITE & st->cfg->perms)
			khtml_puts(&req, " and write");
		else if (PERMS_DELETE & st->cfg->perms)
			khtml_puts(&req, " and delete");
		break;
	case (TEMPL_REALM):
		khtml_puts(&req, KREALM);
		break;
	case (TEMPL_VALID_COLOUR):
		khtml_puts(&req, valids[VALID_COLOUR]);
		break;
	case (TEMPL_VALID_EMAIL):
		khtml_puts(&req, valids[VALID_EMAIL]);
		break;
	case (TEMPL_VALID_NAME):
		khtml_puts(&req, valids[VALID_NAME]);
		break;
	case (TEMPL_VALID_PASS):
		khtml_puts(&req, valids[VALID_PASS]);
		break;
	case (TEMPL_VERSION):
		khtml_puts(&req, VERSION);
		break;
	default:
		abort();
		break;
	}
	return(1);
}

static void
dosetcollection(struct kreq *r)
{
	struct state	*st = r->arg;
	size_t		 df = 0;
	const char	*fragment = NULL;
	struct config	 cfg;

	cfg = *st->cfg;

	if (NULL != r->fieldmap[VALID_NAME]) {
		cfg.displayname = (char *)
			r->fieldmap[VALID_NAME]->parsed.s;
		kinfo("%s: display name modified: %s",
			st->path, st->auth.user);
		df++;
	} else if (NULL != r->fieldnmap[VALID_NAME])
		fragment = "#error";

	if (NULL != r->fieldmap[VALID_COLOUR]) {
		cfg.colour = (char *)
			r->fieldmap[VALID_COLOUR]->parsed.s;
		kinfo("%s: colour modified: %s",
			st->path, st->auth.user);
		df++;
	} else if (NULL != r->fieldnmap[VALID_COLOUR]) 
		fragment = "#error";

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_303]);
	khttp_head(r, kresps[KRESP_LOCATION], "%s://%s%s%s%s", 
		kschemes[r->scheme], r->host, r->pname, 
		r->fullpath, NULL != fragment ? fragment : "");
	khttp_body(r);

	if (0 == df || NULL != fragment)
		return;
	if (config_replace(st->configfile, &cfg))
		return;
	kerrx("%s: couldn't replace config file", st->configfile);
}

static void
dosetpass(struct kreq *r)
{
	char		*url;
	char		 page[PATH_MAX];
	int		 rc;
	struct state	*st = r->arg;

	if (NULL != r->fieldmap[VALID_PASS]) {
		rc = prncpl_replace
			(st->prncplfile, 
			 st->prncpl->name,
			 r->fieldmap[VALID_PASS]->parsed.s, 
			 NULL);
		snprintf(page, sizeof(page), 
			"%s/%s.%s#%s", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime],
			rc ? "setpass-ok" : "error");
	} else if (NULL == r->fieldnmap[VALID_PASS]) {
		snprintf(page, sizeof(page), 
			"%s/%s.%s", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);
	} else if (r->fieldnmap[VALID_PASS]->valsz) {
		snprintf(page, sizeof(page), 
			"%s/%s.%s#setpass-error", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);
	} else
		snprintf(page, sizeof(page), 
			"%s/%s.%s", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);

	kasprintf(&url, "%s://%s%s", 
		kschemes[r->scheme], r->host, page);
	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_303]);
	khttp_head(r, kresps[KRESP_LOCATION], "%s", url);
	khttp_body(r);
	free(url);
}

static void
dosetemail(struct kreq *r)
{
	char		*url;
	char		 page[PATH_MAX];
	struct state	*st = r->arg;
	int		 rc;

	if (NULL != r->fieldmap[VALID_EMAIL]) {
		rc = prncpl_replace
			(st->prncplfile, 
			 st->prncpl->name,
			 NULL,
			 r->fieldmap[VALID_EMAIL]->parsed.s);
		snprintf(page, sizeof(page), 
			"%s/%s.%s#%s", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime],
			rc ? "setemail-ok" : "error");
	} else if (NULL == r->fieldnmap[VALID_EMAIL]) {
		snprintf(page, sizeof(page), 
			"%s/%s.%s", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);
	} else if (r->fieldnmap[VALID_EMAIL]->valsz) {
		snprintf(page, sizeof(page), 
			"%s/%s.%s#setemail-error", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);
	} else 
		snprintf(page, sizeof(page), 
			"%s/%s.%s", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);

	kasprintf(&url, "%s://%s%s", 
		kschemes[r->scheme], r->host, page);
	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_303]);
	khttp_head(r, kresps[KRESP_LOCATION], "%s", url);
	khttp_body(r);
	free(url);
}

static void
dogetcollection(struct kreq *r)
{
	struct state	*st = r->arg;
	struct ktemplate t;

	memset(&t, 0, sizeof(struct ktemplate));
	t.key = templs;
	t.keysz = TEMPL__MAX;
	t.arg = r;
	t.cb = dotemplate;

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_HTML]);
	khttp_body(r);
	khttp_template(r, &t, st->collectionfile);
}

/*
 * Respond to the /index.html or just / page.
 * This just prints out stuff we can change.
 * The requested content type must be HTML.
 */
static void
dogetindex(struct kreq *r)
{
	struct state	*st = r->arg;
	struct ktemplate t;

	memset(&t, 0, sizeof(struct ktemplate));
	t.key = templs;
	t.keysz = TEMPL__MAX;
	t.arg = r;
	t.cb = dotemplate;

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_HTML]);
	khttp_body(r);
	khttp_template(r, &t, st->homefile);
}

/*
 * If the request is for HTML and a GET method, then we're dumped here
 * to process dynamic pages.
 * We only allow a few of these; others get 404'd.
 */
void
method_dynamic_get(struct kreq *r)
{
	struct state	*st = r->arg;

	assert(KMIME_TEXT_HTML == r->mime);

	switch (r->page) {
	case (PAGE_INDEX):
		dogetindex(r);
		break;
	default:
		if (st->isdir)
			dogetcollection(r);
		else
			http_error(r, KHTTP_404);
		break;
	}
}

/*
 * If we're here, then a dynamic HTML page (method_dynamic_get()) is
 * submitted a form.
 * Process it here, but always redirect back to GET.
 * Don't let us do anything if our principal isn't writable.
 */
void
method_dynamic_post(struct kreq *r)
{
	struct state	*st = r->arg;

	assert(KMIME_TEXT_HTML == r->mime);

	switch (r->page) {
	case (PAGE_SETEMAIL):
	case (PAGE_SETPASS):
		if (st->prncpl->writable) 
			break;
		kerrx("%s: POST on readonly principal "
			"file: %s", st->path, st->auth.user);
		http_error(r, KHTTP_403);
		return;
	default:
		assert(st->isdir);
		if (st->cfg->writable) 
			break;
		kerrx("%s: POST on readonly configuration "
			"file: %s", st->path, st->auth.user);
		http_error(r, KHTTP_403);
		break;
	}

	switch (r->page) {
	case (PAGE_SETEMAIL):
		dosetemail(r);
		break;
	case (PAGE_SETPASS):
		dosetpass(r);
		break;
	default:
		dosetcollection(r);
		break;
	}
}
