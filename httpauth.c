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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static	const char *const httpalgs[HTTPALG__MAX] = {
	"MD5", /* HTTPALG_MD5 */
	"MD5-sess" /* HTTPALG_MD5_SESS */
};

static	const char *const httpqops[HTTPQOP__MAX] = {
	"auth", /* HTTPQOP_AUTH */
	"auth-int" /* HTTPQOP_AUTH_INT */
};

/*
 * Parses the next token part.
 * Returns the start of the token.
 */
static const char *
httpauth_nexttok(const char **next, char delim)
{
	const char	*cp;

	while (isspace((int)**next))
		(*next)++;

	for (cp = *next; '\0' != **next; (*next)++)
		if ('\0' != delim && delim == **next)
			break;
		else if (isspace((int)**next))
			break;

	if ('\0' != delim && delim == **next) 
		(*next)++;

	while (isspace((int)**next))
		(*next)++;

	return(cp);
}

/*
 * Parse a token.
 * We don't strictly follow RFC 2615's TOKEN specification, which says
 * that tokens can be followed by any separator.
 * We only use commas as separators.
 */
static int
httpauth_token(size_t *val, const char **cp,
	const char *const *vals, size_t valsz)
{
	const char	*start, *end;
	size_t		 len;

	for (start = *cp; '\0' != **cp; (*cp)++)
		if (isspace((int)**cp))
			break;
		else if (',' == **cp)
			break;

	end = *cp;
	while (isspace((int)**cp))
		(*cp)++;
	if (',' == **cp)
		(*cp)++;
	while (isspace((int)**cp))
		(*cp)++;

	len = end - start;

	for (*val = 0; *val < valsz; (*val)++)
		if (strlen(vals[*val]) != len)
			continue;
		else if (0 == strncasecmp(start, vals[*val], len))
			break;

	if (*val < valsz) {
		kerrx("unknown token: %.*s", (int)len, start);
		return(0);
	}

	return(1);
}

static int
httpauth_alg(enum httpalg *val, const char **cp)
{
	size_t	 i;

	if ( ! httpauth_token(&i, cp, httpalgs, HTTPALG__MAX)) {
		kerrx("unknown HTTP algorithm");
		return(0);
	}
	*val = i;
	return(1);
}

static int
httpauth_qop(enum httpqop *val, const char **cp)
{
	size_t	 i;

	if ( ! httpauth_token(&i, cp, httpqops, HTTPQOP__MAX)) {
		kerrx("unknown HTTP QOP");
		return(0);
	}
	*val = i;
	return(1);
}

/*
 * Parse a quoted-pair (or non-quoted pair) from the string "cp".
 * Puts the location of the next token back into "cp" and fills the
 * pointer in "val" (if non-NULL) with a malloc'd string of the token.
 * Returns zero if memory allocation fails, non-zero otherwise.
 */
static int
httpauth_value(char **val, const char **cp)
{
	int	 	 quot;
	const char	*start, *end;

	if (NULL != val)
		*val = NULL;

	if ('\0' == **cp)
		return(1);

	if ((quot = ('"' == **cp)))
		(*cp)++;

	for (start = *cp; '\0' != **cp; (*cp)++)
		if (quot && '"' == **cp && '\\' != (*cp)[-1])
			break;
		else if ( ! quot && isspace((int)**cp))
			break;
		else if ( ! quot && ',' == **cp)
			break;

	end = *cp;

	if (quot && '"' == **cp)
		(*cp)++;
	while (isspace((int)**cp))
		(*cp)++;
	if (',' == **cp)
		(*cp)++;
	while (isspace((int)**cp))
		(*cp)++;

	if (NULL == val)
		return(1);

	free(*val);
	*val = malloc(end - start + 1);
	if (NULL == *val) {
		kerr(NULL);
		return(0);
	}

	memcpy(*val, start, end - start);
	(*val)[end - start] = '\0';
	return(1);
}

/*
 * Parse HTTP ``Digest'' authentication tokens from the nil-terminated
 * string, which can be NULL or malformed.
 * This always returns an object unless memory allocation fails, in
 * which case it returns zero.
 */
struct httpauth *
httpauth_parse(const char *cp)
{
	struct httpauth	*auth;
	const char	*start;
	int		 rc;

	auth = calloc(1, sizeof(struct httpauth));
	if (NULL == auth) {
		kerr(NULL);
		return(NULL);
	} else if (NULL == cp)
		return(auth);

	/* 
	 * This conforms to RFC 2617, section 3.2.2.
	 * However, we only parse a subset of the tokens.
	 */
	start = httpauth_nexttok(&cp, '\0');
	if (strncasecmp(start, "Digest", 6))
		return(auth);

	for (rc = 1; 1 == rc && '\0' != *cp; ) {
		start = httpauth_nexttok(&cp,  '=');
		if (0 == strncasecmp(start, "username", 8))
			rc = httpauth_value(&auth->user, &cp);
		else if (0 == strncasecmp(start, "realm", 5))
			rc = httpauth_value(&auth->realm, &cp);
		else if (0 == strncasecmp(start, "nonce", 5))
			rc = httpauth_value(&auth->nonce, &cp);
		else if (0 == strncasecmp(start, "cnonce", 6))
			rc = httpauth_value(&auth->cnonce, &cp);
		else if (0 == strncasecmp(start, "response", 8))
			rc = httpauth_value(&auth->response, &cp);
		else if (0 == strncasecmp(start, "uri", 3))
			rc = httpauth_value(&auth->uri, &cp);
		else if (0 == strncasecmp(start, "algorithm", 9))
			rc = httpauth_alg(&auth->alg, &cp);
		else if (0 == strncasecmp(start, "qop", 3))
			rc = httpauth_qop(&auth->qop, &cp);
		else if (0 == strncasecmp(start, "nc", 2))
			rc = httpauth_value(&auth->count, &cp);
		else
			rc = httpauth_value(NULL, &cp);
	}

	if (0 == rc) {
		httpauth_free(auth);
		return(NULL);
	}

	/* Minimum requirements. */
	auth->authorised = 
		NULL != auth->user &&
		NULL != auth->realm &&
		NULL != auth->nonce &&
		NULL != auth->response &&
		NULL != auth->uri;

	/* Additional requirements: MD5-sess. */
	if (auth->authorised && HTTPALG_MD5_SESS == auth->alg) 
		auth->authorised = NULL != auth->cnonce;

	/* Additional requirements: qop. */
	if (auth->authorised && 
		(HTTPQOP_AUTH == auth->qop ||
		 HTTPQOP_AUTH_INT == auth->qop))
		auth->authorised = 
			NULL != auth->count &&
			NULL != auth->cnonce;

	return(auth);
}

void
httpauth_free(struct httpauth *p)
{

	if (NULL == p)
		return;

	free(p->realm);
	free(p->user);
	free(p->uri);
	free(p->response);
	free(p->nonce);
	free(p->count);
	free(p->cnonce);
	free(p);
}
