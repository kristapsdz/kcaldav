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
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef __linux__
#include <bsd/string.h>
#endif
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgihtml.h>
#include <kcgijson.h>
#include <kcgixml.h>

#include "extern.h"
#include "kcaldav.h"

static void
send500(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_500]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[r->mime]);
	khttp_body(r);
}

static void
send200(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[r->mime]);
	khttp_body(r);
}

static void
send400(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_400]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[r->mime]);
	khttp_body(r);
}

static void
json_setpass(struct kreq *r)
{
	struct state	*st = r->arg;
	struct prncpl	 p;
	int		 rc;

	if (NULL == r->fieldmap[VALID_PASS]) {
		send400(r);
		return;
	} 
	
	p = *st->prncpl;
	p.hash = (char *)r->fieldmap[VALID_PASS]->parsed.s;
	if ((rc = db_prncpl_update(&p)) < 0)
		send500(r);
	else if (0 == rc)
		send400(r);
	else
		send200(r);
}

static void
json_delcoln(struct kreq *r)
{
	struct state	*st = r->arg;
	struct coln	*coln;
	int		 rc;

	if (NULL == r->fieldmap[VALID_ID]) {
		send400(r);
		return;
	} 

	rc = db_collection_loadid(&coln,
		r->fieldmap[VALID_ID]->parsed.i,
		st->prncpl->id);

	if (rc < 0) {
		send500(r);
		return;
	} else if (0 == rc) {
		send400(r);
		return;
	} 

	db_collection_remove(coln->id, st->prncpl);
	db_collection_free(coln);
	send200(r);
}

static void
json_setcolnprops(struct kreq *r)
{
	struct state	*st = r->arg;
	struct coln	*colnp;
	struct coln	 coln;
	int		 rc;

	if (NULL == r->fieldmap[VALID_ID]) {
		send400(r);
		return;
	} 

	rc = db_collection_loadid(&colnp,
		r->fieldmap[VALID_ID]->parsed.i,
		st->prncpl->id);

	if (rc < 0) {
		send500(r);
		return;
	} else if (0 == rc) {
		send400(r);
		return;
	} else {
		send200(r);
		coln = *colnp;
	}

	if (NULL != r->fieldmap[VALID_NAME])
		coln.displayname = (char *)
			r->fieldmap[VALID_NAME]->parsed.s;
	if (NULL != r->fieldmap[VALID_COLOUR])
		coln.colour = (char *)
			r->fieldmap[VALID_COLOUR]->parsed.s;
	if (NULL != r->fieldmap[VALID_DESCRIPTION])
		coln.description = (char *)
			r->fieldmap[VALID_DESCRIPTION]->parsed.s;

	db_collection_update(&coln, st->prncpl);
	db_collection_free(colnp);
}

static void
json_setemail(struct kreq *r)
{
	struct state	*st = r->arg;
	struct prncpl	 p;
	int		 rc;

	if (NULL == r->fieldmap[VALID_EMAIL]) {
		send400(r);
		return;
	} 
	
	p = *st->prncpl;
	p.email = (char *)r->fieldmap[VALID_EMAIL]->parsed.s;
	if ((rc = db_prncpl_update(&p)) < 0)
		send500(r);
	else if (0 == rc)
		send400(r);
	else
		send200(r);
}

static void
json_modproxy(struct kreq *r)
{
	struct state	*st = r->arg;
	int64_t	 	 id, bits;
	int		 rc;
	struct kpair	*kp;

	if (NULL == r->fieldmap[VALID_EMAIL]) {
		send400(r);
		return;
	}

	bits = 0;
	for (kp = r->fieldmap[VALID_BITS]; NULL != kp; kp = kp->next)
		bits |= kp->parsed.i;

	id = db_prncpl_identify(r->fieldmap[VALID_EMAIL]->parsed.s);
	if (id < 0) {
		send500(r);
		return;
	} else if (0 == id) {
		send400(r);
		return;
	} else if (id == st->prncpl->id) {
		send200(r);
		return;
	}

	if ((rc = db_proxy(st->prncpl, id, bits)) < 0) {
		send500(r);
		return;
	} else if (0 == rc) {
		send400(r);
		return;
	}

	send200(r);
}

static void
json_newcoln(struct kreq *r)
{
	struct state	*st = r->arg;
	const char	*path;
	int		 rc;

	if (NULL == r->fieldmap[VALID_PATH]) {
		send400(r);
		return;
	}

	path = r->fieldmap[VALID_PATH]->parsed.s;
	if ((rc = db_collection_new(path, st->prncpl)) < 0)
		send500(r);
	else if (0 == rc)
		send400(r);
	else
		send200(r);
}

static void
json_index(struct kreq *r)
{
	struct state	*st = r->arg;
	struct kjsonreq	 req;
	size_t		 i;

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[r->mime]);
	khttp_head(r, kresps[KRESP_CACHE_CONTROL], 
		"%s", "no-cache, no-store");
	khttp_head(r, kresps[KRESP_PRAGMA], 
		"%s", "no-cache");
	khttp_head(r, kresps[KRESP_EXPIRES], 
		"%s", "-1");
	khttp_body(r);
	kjson_open(&req, r);
	kjson_obj_open(&req);
	kjson_objp_open(&req, "principal");
	kjson_putstringp(&req, "name", st->prncpl->name);
	kjson_putstringp(&req, "email", st->prncpl->email);
	kjson_putintp(&req, "quota_used", st->prncpl->quota_used);
	kjson_putintp(&req, "quota_avail", st->prncpl->quota_avail);
	kjson_putintp(&req, "id", st->prncpl->id);
	kjson_arrayp_open(&req, "colns");
	for (i = 0; i < st->prncpl->colsz; i++) {
		kjson_obj_open(&req);
		kjson_putstringp(&req, "url", 
			st->prncpl->cols[i].url);
		kjson_putstringp(&req, "displayname", 
			st->prncpl->cols[i].displayname);
		kjson_putstringp(&req, "colour", 
			st->prncpl->cols[i].colour);
		kjson_putstringp(&req, "description", 
			st->prncpl->cols[i].description);
		kjson_putintp(&req, "id", 
			st->prncpl->cols[i].id);
		kjson_obj_close(&req);
	}
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "proxies");
	for (i = 0; i < st->prncpl->proxiesz; i++) {
		kjson_obj_open(&req);
		kjson_putstringp(&req, "email", 
			st->prncpl->proxies[i].email);
		kjson_putstringp(&req, "name", 
			st->prncpl->proxies[i].name);
		kjson_putintp(&req, "bits", 
			st->prncpl->proxies[i].bits);
		kjson_putintp(&req, "id", 
			st->prncpl->proxies[i].id);
		kjson_putintp(&req, "proxy", 
			st->prncpl->proxies[i].proxy);
		kjson_obj_close(&req);
	}
	kjson_close(&req);
}

/*
 * If the request is for HTML and a GET method, then we're dumped here
 * to process dynamic pages.
 * We only allow a few of these; others get 404'd.
 */
void
method_json(struct kreq *r)
{

	switch (r->page) {
	case (PAGE_DELCOLN):
		json_delcoln(r);
		return;
	case (PAGE_INDEX):
		json_index(r);
		return;
	case (PAGE_MODPROXY):
		json_modproxy(r);
		return;
	case (PAGE_NEWCOLN):
		json_newcoln(r);
		return;
	case (PAGE_SETCOLNPROPS):
		json_setcolnprops(r);
		return;
	case (PAGE_SETEMAIL):
		json_setemail(r);
		return;
	case (PAGE_SETPASS):
		json_setpass(r);
		return;
	default:
		break;
	}

	http_error(r, KHTTP_404);
}
