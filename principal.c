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
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <bsd/stdio.h>
#else
#include <util.h>
#endif

#include "extern.h"
#include "md5.h"

static int
validate(const char *pass, const char *method,
	const struct httpauth *auth)
{
	MD5_CTX	 	 ctx;
	unsigned char	 ha1[MD5_DIGEST_LENGTH],
			 ha2[MD5_DIGEST_LENGTH],
			 ha3[MD5_DIGEST_LENGTH];
	char		 skey1[MD5_DIGEST_LENGTH * 2 + 1],
			 skey2[MD5_DIGEST_LENGTH * 2 + 1],
			 skey3[MD5_DIGEST_LENGTH * 2 + 1];
	size_t		 i;

	MD5Init(&ctx);
	MD5Update(&ctx, auth->user, strlen(auth->user));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, auth->realm, strlen(auth->realm));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, pass, strlen(pass));
	MD5Final(ha1, &ctx);

	MD5Init(&ctx);
	MD5Update(&ctx, method, strlen(method));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, auth->uri, strlen(auth->uri));
	MD5Final(ha2, &ctx);

	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&skey1[i * 2], 3, "%02x", ha1[i]);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&skey2[i * 2], 3, "%02x", ha2[i]);

	MD5Init(&ctx);
	MD5Update(&ctx, skey1, strlen(skey1));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, auth->nonce, strlen(auth->nonce));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, skey2, strlen(skey2));
	MD5Final(ha3, &ctx);

	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&skey3[i * 2], 3, "%02x", ha3[i]);

	return(0 == strcmp(auth->response, skey3));
}

/*
 * Parse the passwd(5)-style database from "file".
 * Look for the principal matching the username in "auth", and fill in
 * its principal components in "pp".
 * Returns <0 on allocation failure.
 * Returns 0 on file-format failure.
 * Returns >0 on success.
 */
int
prncpl_parse(const char *file, const char *method,
	const struct httpauth *auth, struct prncpl **pp)
{
	size_t	 len, line;
	char	*cp, *string, *pass, *user, *uid, *gid, 
		*class, *change, *expire, *gecos, *homedir;
	FILE	*f;
	int	 rc;

	*pp = NULL;

	if (NULL == (f = fopen(file, "r"))) {
		kerr("%s: fopen", file);
		return(0);
	}

	rc = 0;
	line = 0;
	/*
	 * Carefully make sure that we explicit_bzero() then contents of
	 * the memory buffer after each read.
	 * This prevents an adversary to gaining control of the
	 * password hashes that are in this file via our memory.
	 */
	while (NULL != (cp = fgetln(f, &len))) {
		line++;
		/* Nil-terminate the buffer. */
		if (0 == len || '\n' != cp[len - 1]) {
			kerrx("%s:%zu: no newline", file, line);
			continue;
		}
		cp[len - 1] = '\0';
		string = cp;

		/* Read in all of the required fields. */
		if (NULL == (user = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing name", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (pass = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing passwd", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (uid = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing uid", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (gid = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing gid", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (class = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing class", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (change = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing change", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (expire = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing expire", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (gecos = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing gecos", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (homedir = strsep(&string, ":"))) {
			kerrx("%s:%zu: missing homedir", file, line);
			explicit_bzero(cp, len);
			break;
		}

		/* Is this the user we're looking for? */
		if (strcmp(user, auth->user)) {
			explicit_bzero(cp, len);
			continue;
		} else if ( ! validate(pass, method, auth)) {
			kerrx("%s:%zu: password mismatch with "
				"HTTP authorisation", file, line);
			explicit_bzero(cp, len);
			continue;
		}

		/* Allocate the principal. */
		rc = -1;
		if (NULL == (*pp = calloc(1, sizeof(struct prncpl)))) 
			kerr(NULL);
		else if (NULL == ((*pp)->name = strdup(user)))
			kerr(NULL);
		else if (NULL == ((*pp)->homedir = strdup(homedir)))
			kerr(NULL);
		else
			rc = 1;

		explicit_bzero(cp, len);
		break;
	}

	/* If we had errors, bail out now. */
	if ( ! feof(f) && rc <= 0) {
		if (rc > 0)
			kerr("%s: fgetln", file);
		if (-1 == fclose(f))
			kerr("%s: fclose", file);
		prncpl_free(*pp);
		pp = NULL;
		return(rc > 0 ? -1 : rc);
	}

	if (-1 == fclose(f))
		kerr("%s: fclose", file);

	return(1);
}

void
prncpl_free(struct prncpl *p)
{

	if (NULL == p)
		return;

	free(p->name);
	free(p->homedir);
	free(p);
}

