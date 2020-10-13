/*	$Id$ */
/*
 * Copyright (c) 2015, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <kcgi.h>
#include <kcgijson.h>
#include <kcgixml.h>

#include "libkcaldav.h"
#include "db.h"
#include "server.h"

static void json_delcoln(struct kreq *, struct state *);
static void json_delproxy(struct kreq *, struct state *);
static void json_index(struct kreq *, struct state *);
static void json_logout(struct kreq *, struct state *);
static void json_modproxy(struct kreq *, struct state *);
static void json_newcoln(struct kreq *, struct state *);
static void json_setcolnprops(struct kreq *, struct state *);
static void json_setemail(struct kreq *, struct state *);
static void json_setpass(struct kreq *, struct state *);

typedef void (*pagecb)(struct kreq *, struct state *);

static const pagecb pages[PAGE__MAX] = {
	json_delcoln, /* PAGE_DELCOLN */
	json_delproxy, /* PAGE_DELPROXY */
	json_index, /* PAGE_INDEX */
	json_logout, /* PAGE_LOGOUT */
	json_modproxy, /* PAGE_MODPROXY */
	json_newcoln, /* PAGE_NEWCOLN */
	json_setcolnprops, /* PAGE_SETCOLNPROPS */
	json_setemail, /* PAGE_SETEMAIL */
	json_setpass, /* PAGE_SETPASS */
};

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

/*
 * Update the password.
 * It's given as an MD5 hash, not a password string.
 */
static void
json_setpass(struct kreq *r, struct state *st)
{
	struct prncpl	 p;
	int		 rc;
	struct kpair	*kp;

	if ((kp = r->fieldmap[VALID_PASS]) == NULL) {
		send400(r);
		return;
	} 
	
	p = *st->prncpl;
	p.hash = (char *)kp->parsed.s;

	if ((rc = db_prncpl_update(&p)) < 0)
		send500(r);
	else if (rc == 0)
		send400(r);
	else
		send200(r);
}

/*
 * Remove a collection.
 */
static void
json_delcoln(struct kreq *r, struct state *st)
{
	struct coln	*coln;
	int		 rc;
	struct kpair	*kp;

	if ((kp = r->fieldmap[VALID_ID]) == NULL) {
		send400(r);
		return;
	} 

	rc = db_collection_loadid(&coln,
		kp->parsed.i, st->prncpl->id);

	if (rc > 0) {
		db_collection_remove(coln->id, st->prncpl);
		db_collection_free(coln);
		send200(r);
	} else if (rc < 0) {
		send500(r);
	} else if (rc == 0)
		send400(r);
}

/*
 * Update a collection's properties.
 * The properties (name, colour, description) are each optional.
 * If not set, the collection keeps its current properties.
 */
static void
json_setcolnprops(struct kreq *r, struct state *st)
{
	struct coln	*colnp;
	struct coln	 coln;
	int		 rc;
	struct kpair	*kp;

	if ((kp = r->fieldmap[VALID_ID]) == NULL) {
		send400(r);
		return;
	} 

	rc = db_collection_loadid(&colnp,
		kp->parsed.i, st->prncpl->id);

	if (rc < 0) {
		send500(r);
		return;
	} else if (rc == 0) {
		send400(r);
		return;
	} 

	send200(r);
	coln = *colnp;

	if ((kp = r->fieldmap[VALID_NAME]) != NULL)
		coln.displayname = (char *)kp->parsed.s;
	if ((kp = r->fieldmap[VALID_COLOUR]) != NULL)
		coln.colour = (char *)kp->parsed.s;
	if ((kp = r->fieldmap[VALID_DESCRIPTION]) != NULL)
		coln.description = (char *)kp->parsed.s;

	db_collection_update(&coln, st->prncpl);
	db_collection_free(colnp);
}

/*
 * Set the user's e-mail address.
 * This must be unique in the system.
 */
static void
json_setemail(struct kreq *r, struct state *st)
{
	struct prncpl	 p;
	int		 rc;
	struct kpair	*kp;

	if ((kp = r->fieldmap[VALID_EMAIL]) == NULL) {
		send400(r);
		return;
	} 
	
	p = *st->prncpl;
	p.email = (char *)kp->parsed.s;

	if ((rc = db_prncpl_update(&p)) < 0)
		send500(r);
	else if (rc == 0)
		send400(r);
	else
		send200(r);
}

/*
 * Delete a proxy entry.
 */
static void
json_delproxy(struct kreq *r, struct state *st)
{
	int64_t	 	 id;
	struct kpair	*kp;

	if ((kp = r->fieldmap[VALID_EMAIL]) == NULL) {
		send400(r);
		return;
	}

	if ((id = db_prncpl_identify(kp->parsed.s)) < 0)
		send500(r);
	else if (id == 0)
		send400(r);
	else if (!db_proxy_remove(st->prncpl, id))
		send500(r);
	else
		send200(r);
}

/*
 * Create or modify a proxy entry.
 * We cannot have a proxy against ourself.
 */
static void
json_modproxy(struct kreq *r, struct state *st)
{
	int64_t	 	 id;
	int		 rc;
	struct kpair	*kp, *kpb;

	if ((kp = r->fieldmap[VALID_EMAIL]) == NULL ||
	    (kpb = r->fieldmap[VALID_BITS]) == NULL) {
		send400(r);
		return;
	}

	if ((id = db_prncpl_identify(kp->parsed.s)) < 0) {
		send500(r);
		return;
	} else if (id == 0 || id == st->prncpl->id) {
		send400(r);
		return;
	}

	if ((rc = db_proxy(st->prncpl, id, kpb->parsed.i)) < 0)
		send500(r);
	else if (rc == 0)
		send400(r);
	else
		send200(r);
}

/*
 * Create a new collection.
 */
static void
json_newcoln(struct kreq *r, struct state *st)
{
	int		 rc;
	struct kpair	*kp;

	if ((kp = r->fieldmap[VALID_PATH]) == NULL) {
		send400(r);
		return;
	}

	if ((rc = db_collection_new(kp->parsed.s, st->prncpl)) < 0)
		send500(r);
	else if (rc == 0)
		send400(r);
	else
		send200(r);
}

/*
 * Delete the nonce, effectively logging out the user.
 */
static void
json_logout(struct kreq *r, struct state *st)
{

	db_nonce_delete(st->nonce, st->prncpl);
	send200(r);
}

static void
json_index(struct kreq *r, struct state *st)
{
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
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "rproxies");
	for (i = 0; i < st->prncpl->rproxiesz; i++) {
		kjson_obj_open(&req);
		kjson_putstringp(&req, "email", 
			st->prncpl->rproxies[i].email);
		kjson_putstringp(&req, "name", 
			st->prncpl->rproxies[i].name);
		kjson_putintp(&req, "bits", 
			st->prncpl->rproxies[i].bits);
		kjson_putintp(&req, "id", 
			st->prncpl->rproxies[i].id);
		kjson_putintp(&req, "proxy", 
			st->prncpl->rproxies[i].proxy);
		kjson_obj_close(&req);
	}
	kjson_array_close(&req);
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

	assert(r->arg != NULL);
	if (r->page < PAGE__MAX)
		(*pages[r->page])(r, r->arg);
	else
		http_error(r, KHTTP_404);
}
