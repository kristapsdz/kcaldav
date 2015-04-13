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

#include <kcgi.h>
#include <kcgixml.h>
#include <kcgihtml.h>

#include "extern.h"
#include "kcaldav.h"

static void
dosetpass(struct kreq *r)
{
	char	*url;
	char	 page[PATH_MAX];

	if (NULL != r->fieldmap[VALID_PASS] &&
		 NULL == r->fieldnmap[VALID_PASS]) {
		snprintf(page, sizeof(page), 
			"%s/%s.%s#setpass-ok", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);
		/* TODO: set password here. */
	} else
		snprintf(page, sizeof(page), 
			"%s/%s.%s#setpass-error", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);

	url = kutil_urlabs(r->scheme, r->host, r->port, page);
	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_303]);
	khttp_head(r, kresps[KRESP_LOCATION], "%s", url);
	khttp_body(r);
	free(url);
}

static void
dosetemail(struct kreq *r)
{
	char	*url;
	char	 page[PATH_MAX];

	if (NULL != r->fieldmap[VALID_EMAIL] &&
		 NULL == r->fieldnmap[VALID_EMAIL]) {
		snprintf(page, sizeof(page), 
			"%s/%s.%s#setemail-ok", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);
		/* TODO: set email here. */
	} else
		snprintf(page, sizeof(page), 
			"%s/%s.%s#setemail-error", r->pname, 
			pages[PAGE_INDEX], ksuffixes[r->mime]);

	url = kutil_urlabs(r->scheme, r->host, r->port, page);
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
	struct khtmlreq	 html;
	struct state	*st = r->arg;
	char		*link, *url;

	assert(KMIME_TEXT_HTML == r->mime);

	fprintf(stderr, "moop\n");

	if (KSCHEME__MAX != r->scheme)
		kasprintf(&link, "%s://%s%s%s",
			kschemes[r->scheme],
			r->host, r->pname,
			st->prncpl->homedir);
	else
		kasprintf(&link, "%s%s%s",
			r->host, r->pname,
			st->prncpl->homedir);

	memset(&html, 0, sizeof(struct khtmlreq));
	html.req = r;

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[KMIME_TEXT_HTML]);
	khttp_body(r);
	khtml_elem(&html, KELEM_DOCTYPE);
	khtml_elem(&html, KELEM_HTML);
	khtml_elem(&html, KELEM_HEAD);
	khtml_elem(&html, KELEM_TITLE);
	khtml_puts(&html, "kcaldav: ");
	khtml_puts(&html, st->auth.user);
	khtml_close(&html, 2);
	khtml_elem(&html, KELEM_BODY);
	khtml_elem(&html, KELEM_P);
	khtml_puts(&html, "Welcome, ");
	khtml_puts(&html, st->auth.user);
	khtml_puts(&html, ".");
	khtml_close(&html, 1);
	khtml_elem(&html, KELEM_P);
	khtml_puts(&html, "Some helpful info:");
	khtml_close(&html, 1);
	khtml_elem(&html, KELEM_UL);

	khtml_elem(&html, KELEM_LI);
	khtml_puts(&html, "Your CalDAV address is: ");
	khtml_attr(&html, KELEM_INPUT,
		KATTR_TYPE, "text",
		KATTR_DISABLED, "disabled",
		KATTR_VALUE, link, KATTR__MAX);
	khtml_close(&html, 1);

	khtml_elem(&html, KELEM_LI);
	url = kutil_urlpart(r, r->pname, 
		ksuffixes[r->mime], pages[PAGE_SETEMAIL], NULL);
	khtml_attr(&html, KELEM_FORM,
		KATTR_ACTION, url,
		KATTR_METHOD, "post", KATTR__MAX);
	free(url);
	khtml_puts(&html, "Your e-mail address is: ");
	khtml_attr(&html, KELEM_INPUT,
		KATTR_VALUE, st->prncpl->email, 
		KATTR_NAME, valids[VALID_EMAIL],
		KATTR_TYPE, "email", KATTR__MAX);
	khtml_attr(&html, KELEM_INPUT,
		KATTR_VALUE, "Change E-mail", 
		KATTR_TYPE, "submit", KATTR__MAX);
	khtml_close(&html, 2);

	khtml_elem(&html, KELEM_LI);
	url = kutil_urlpart(r, r->pname, 
		ksuffixes[r->mime], pages[PAGE_SETPASS], NULL);
	khtml_attr(&html, KELEM_FORM,
		KATTR_ACTION, url,
		KATTR_METHOD, "post", KATTR__MAX);
	free(url);
	khtml_puts(&html, "Your password is: ");
	khtml_attr(&html, KELEM_INPUT,
		KATTR_NAME, valids[VALID_PASS],
		KATTR_TYPE, "password", KATTR__MAX);
	khtml_attr(&html, KELEM_INPUT,
		KATTR_VALUE, "Change Password", 
		KATTR_TYPE, "submit", KATTR__MAX);

	khtml_close(&html, 0);
	free(link);
}

/*
 * If the request is for HTML and a GET method, then we're dumped here
 * to process dynamic pages.
 * We only allow a few of these; others get 404'd.
 */
void
method_dynamic_get(struct kreq *r)
{

	switch (r->page) {
	case (PAGE_INDEX):
		doindex(r);
		return;
	default:
		break;
	}
	http_error(r, KHTTP_404);
}

/*
 * If we're here, then a dynamic HTML page (method_dynamic_get()) is
 * submitted a form.
 * Process it here, but always redirect back to GET.
 */
void
method_dynamic_post(struct kreq *r)
{

	switch (r->page) {
	case (PAGE_SETPASS):
		dosetpass(r);
		return;
	case (PAGE_SETEMAIL):
		dosetemail(r);
		return;
	default:
		break;
	}
	http_error(r, KHTTP_404);
}
