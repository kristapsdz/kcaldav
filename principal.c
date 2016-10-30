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
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <kcgi.h>

#include "extern.h"

void
prncpl_free(struct prncpl *p)
{
	size_t	 i;

	if (NULL == p)
		return;

	free(p->name);
	free(p->hash);
	free(p->email);
	for (i = 0; i < p->colsz; i++) {
		free(p->cols[i].url);
		free(p->cols[i].displayname);
		free(p->cols[i].colour);
		free(p->cols[i].description);
	}
	free(p->cols);
	for (i = 0; i < p->proxiesz; i++) {
		free(p->proxies[i].email);
		free(p->proxies[i].name);
	}
	free(p->proxies);
	for (i = 0; i < p->rproxiesz; i++) {
		free(p->rproxies[i].email);
		free(p->rproxies[i].name);
	}
	free(p->rproxies);
	free(p);
}

