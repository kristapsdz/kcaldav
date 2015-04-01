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
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "extern.h"
#include "md5.h"

/*
 * Public domain.
 * Written by Matthew Dempsky.
 */
#ifndef __OpenBSD__
__attribute__((weak)) void
__explicit_bzero_hook(void *buf, size_t len)
{
}

void
explicit_bzero(void *buf, size_t len)
{
	memset(buf, 0, len);
	__explicit_bzero_hook(buf, len);
}
#endif

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
		perror(file);
		return(0);
	}

	rc = 0;
	line = 0;
	/*
	 * Carefully make sure that we explicit_bzero() then contents of
	 * the memory buffer after each read.
	 * This prevents an adversary to gaining control of the
	 * passwords that are in this file via our memory.
	 */
	while (NULL != (cp = fgetln(f, &len))) {
		line++;
		/* Nil-terminate the buffer. */
		if (0 == len || '\n' != cp[len - 1]) {
			fprintf(stderr, "%s:%zu: no newline\n", file, line);
			continue;
		}
		cp[len - 1] = '\0';
		string = cp;

		/* Read in all of the required fields. */
		if (NULL == (user = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"name field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (pass = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"password field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (uid = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"uid field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (gid = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"gid field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (class = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"class field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (change = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"change field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (expire = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"expire field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (gecos = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"gecos field\n", file, line);
			explicit_bzero(cp, len);
			break;
		} else if (NULL == (homedir = strsep(&string, ":"))) {
			fprintf(stderr, "%s:%zu: missing "
				"homedir field\n", file, line);
			explicit_bzero(cp, len);
			break;
		}

		/* Is this the user we're looking for? */
		if (strcmp(user, auth->user)) {
			explicit_bzero(cp, len);
			continue;
		} else if ( ! validate(pass, method, auth)) {
			fprintf(stderr, "%s:%zu: password "
				"does not match HTTP authorisation "
				"response\n", file, line);
			explicit_bzero(cp, len);
			continue;
		}

		/* Allocate the principal. */
		rc = -1;
		if (NULL == (*pp = calloc(1, sizeof(struct prncpl)))) 
			perror(NULL);
		else if (NULL == ((*pp)->name = strdup(user)))
			perror(NULL);
		else if (NULL == ((*pp)->homedir = strdup(homedir)))
			perror(NULL);
		else
			rc = 1;

		explicit_bzero(cp, len);
		break;
	}

	/* If we had errors, bail out now. */
	if ( ! feof(f) && rc <= 0) {
		if (rc > 0)
			perror(file);
		fclose(f);
		prncpl_free(*pp);
		pp = NULL;
		return(rc > 0 ? -1 : rc);
	}

	fclose(f);
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

