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
#include <errno.h>
#include <fcntl.h>
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

static void
doindex(struct kreq *r)
{
	struct khtmlreq	 html;
	struct state	*st = r->arg;

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
	if (KSCHEME__MAX != r->scheme) {
		khtml_puts(&html, kschemes[r->scheme]);
		khtml_puts(&html, "://");
	}

	khtml_puts(&html, r->host);
	khtml_puts(&html, r->pname);
	khtml_puts(&html, st->prncpl->homedir);
	khtml_close(&html, 1);
	khtml_elem(&html, KELEM_LI);
	khtml_puts(&html, "Your e-mail is: ");
	khtml_puts(&html, st->prncpl->email);
	khtml_close(&html, 0);
}

/*
 * RFC 4791 doesn't specify any special behaviour for GET, and RFC 2518
 * just says it follows HTTP, RFC 2068.
 * So use those semantics.
 */
void
method_dynamic(struct kreq *r)
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
