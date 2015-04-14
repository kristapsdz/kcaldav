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

#include "extern.h"
#include "kcaldav.h"

enum	templ {
	TEMPL_CLASS_INSECURE,
	TEMPL_CLASS_READONLY,
	TEMPL_CLASS_WRITABLE,
	TEMPL_EMAIL,
	TEMPL_HTURI,
	TEMPL_NAME,
	TEMPL_URI,
	TEMPL_PAGE_EMAIL,
	TEMPL_PAGE_HOME,
	TEMPL_PAGE_PASS,
	TEMPL_REALM,
	TEMPL_VALID_EMAIL,
	TEMPL_VALID_PASS,
	TEMPL__MAX
};

static	const char *const templs[TEMPL__MAX] = {
	"class-insecure", /* TEMPL_CLASS_INSECURE */
	"class-readonly", /* TEMPL_CLASS_READONLY */
	"class-writable", /* TEMPL_CLASS_WRITABLE */
	"email", /* TEMPL_EMAIL */
	"hturi", /* TEMPL_HTURI */
	"name", /* TEMPL_NAME */
	"uri", /* TEMPL_URI */
	"page-email", /* TEMPL_PAGE_EMAIL */
	"page-home", /* TEMPL_PAGE_HOME */
	"page-pass", /* TEMPL_PAGE_PASS */
	"realm", /* TEMPL_REALM */
	"valid-email", /* TEMPL_VALID_EMAIL */
	"valid-pass", /* TEMPL_VALID_PASS */
};

static int
dotemplate(size_t key, void *arg)
{
	struct kreq	*r = arg;
	struct state	*st = r->arg;

	switch (key) {
	case (TEMPL_CLASS_INSECURE):
		if (KSCHEME_HTTPS == r->scheme)
			khttp_puts(r, "noshow");
		break;
	case (TEMPL_CLASS_READONLY):
		if (st->prncpl->writable)
			khttp_puts(r, "noshow");
		break;
	case (TEMPL_CLASS_WRITABLE):
		if ( ! st->prncpl->writable)
			khttp_puts(r, "noshow");
		break;
	case (TEMPL_EMAIL):
		khttp_puts(r, st->prncpl->email);
		break;
	case (TEMPL_HTURI):
		khttp_puts(r, HTDOCS);
		break;
	case (TEMPL_NAME):
		khttp_puts(r, st->prncpl->name);
		break;
	case (TEMPL_URI):
		khttp_puts(r, kschemes[r->scheme]);
		khttp_puts(r, "://");
		khttp_puts(r, r->host);
		khttp_puts(r, r->pname);
		khttp_puts(r, st->prncpl->homedir);
		break;
	case (TEMPL_PAGE_EMAIL):
		khttp_puts(r, r->pname);
		khttp_putc(r, '/');
		khttp_puts(r, pages[PAGE_SETEMAIL]);
		khttp_puts(r, ".html");
		break;
	case (TEMPL_PAGE_HOME):
		khttp_puts(r, r->pname);
		khttp_putc(r, '/');
		khttp_puts(r, pages[PAGE_INDEX]);
		khttp_puts(r, ".html");
		break;
	case (TEMPL_PAGE_PASS):
		khttp_puts(r, r->pname);
		khttp_putc(r, '/');
		khttp_puts(r, pages[PAGE_SETPASS]);
		break;
	case (TEMPL_REALM):
		khttp_puts(r, KREALM);
		break;
	case (TEMPL_VALID_EMAIL):
		khttp_puts(r, valids[VALID_EMAIL]);
		break;
	case (TEMPL_VALID_PASS):
		khttp_puts(r, valids[VALID_PASS]);
		break;
	default:
		abort();
		break;
	}
	return(1);
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

/*
 * Respond to the /index.html or just / page.
 * This just prints out stuff we can change.
 * The requested content type must be HTML.
 */
static void
doindex(struct kreq *r)
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

	assert(KMIME_TEXT_HTML == r->mime);

	switch (r->page) {
	case (PAGE_INDEX):
		doindex(r);
		break;
	default:
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

	if ( ! st->prncpl->writable) {
		kerrx("%s: POST on readonly principal "
			"file: %s", st->path, st->auth.user);
		http_error(r, KHTTP_403);
		return;
	}

	switch (r->page) {
	case (PAGE_SETPASS):
		dosetpass(r);
		break;
	case (PAGE_SETEMAIL):
		dosetemail(r);
		break;
	default:
		http_error(r, KHTTP_404);
		break;
	}
}
