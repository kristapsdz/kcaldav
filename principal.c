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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif
#include <unistd.h>
#ifdef __linux__
#include <bsd/stdio.h>
#else
#include <util.h>
#endif

#include <kcgi.h>

#include "extern.h"
#include "md5.h"

/*
 * Hash validation.
 * This takes the HTTP digest fields in "auth", constructs the
 * "response" field given the information at hand, then compares the
 * response fields to see if they're different.
 * Depending on the HTTP options, this might involve a lot.
 * RFC 2617 has a handy source code guide on how to do this.
 */
int
prncpl_validate(const struct prncpl *prncpl, 
	const struct httpauth *auth)
{
	MD5_CTX	 	 ctx;
	char		*hash;
	unsigned char	 ha1[MD5_DIGEST_LENGTH],
			 ha2[MD5_DIGEST_LENGTH],
			 ha3[MD5_DIGEST_LENGTH];
	char		 skey1[MD5_DIGEST_LENGTH * 2 + 1],
			 skey2[MD5_DIGEST_LENGTH * 2 + 1],
			 skey3[MD5_DIGEST_LENGTH * 2 + 1],
			 count[9];
	size_t		 i;

	hash = prncpl->hash;

	/*
	 * MD5-sess hashes the nonce and client nonce as well as the
	 * existing hash (user/real/pass).
	 * Note that the existing hash is MD5_DIGEST_LENGTH * 2 as
	 * validated by prncpl_pentry_check().
	 */
	if (HTTPALG_MD5_SESS == auth->alg) {
		MD5Init(&ctx);
		MD5Update(&ctx, hash, strlen(hash));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->nonce, strlen(auth->nonce));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->cnonce, strlen(auth->cnonce));
		MD5Final(ha1, &ctx);
		for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
			snprintf(&skey1[i * 2], 3, "%02x", ha1[i]);
	} else 
		strlcpy(skey1, hash, sizeof(skey1));

	if (HTTPQOP_AUTH_INT == auth->qop) {
		MD5Init(&ctx);
		MD5Update(&ctx, auth->method, strlen(auth->method));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->uri, strlen(auth->uri));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->req, auth->reqsz);
		MD5Final(ha2, &ctx);
	} else {
		MD5Init(&ctx);
		MD5Update(&ctx, auth->method, strlen(auth->method));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->uri, strlen(auth->uri));
		MD5Final(ha2, &ctx);
	}

	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&skey2[i * 2], 3, "%02x", ha2[i]);

	if (HTTPQOP_AUTH_INT == auth->qop || HTTPQOP_AUTH == auth->qop) {
		snprintf(count, sizeof(count), "%08lx", auth->count);
		MD5Init(&ctx);
		MD5Update(&ctx, skey1, MD5_DIGEST_LENGTH * 2);
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->nonce, strlen(auth->nonce));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, count, strlen(count));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->cnonce, strlen(auth->cnonce));
		MD5Update(&ctx, ":", 1);
		if (HTTPQOP_AUTH_INT == auth->qop)
			MD5Update(&ctx, "auth-int", 8);
		else
			MD5Update(&ctx, "auth", 4);
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, skey2, MD5_DIGEST_LENGTH * 2);
		MD5Final(ha3, &ctx);
	} else {
		MD5Init(&ctx);
		MD5Update(&ctx, skey1, MD5_DIGEST_LENGTH * 2);
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->nonce, strlen(auth->nonce));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, skey2, MD5_DIGEST_LENGTH * 2);
		MD5Final(ha3, &ctx);
	}

	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&skey3[i * 2], 3, "%02x", ha3[i]);

	return(0 == strcmp(auth->response, skey3));
}

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
	free(p);
}

