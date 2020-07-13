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
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "libkcaldav.h"
#include "extern.h"
#include "kcaldav.h"

const char *const xmls[XML__MAX] = {
	"C:calendar", /* XML_CALDAV_CALENDAR */
	"C:calendar-data", /* XML_CALDAV_CALENDAR_DATA */
	"C:comp", /* XML_CALDAV_COMP */
	"C:opaque", /* XML_CALDAV_OPAQUE */
	"B:calendar-proxy-read", /* XML_CALDAVSERV_PROXY_READ */
	"B:calendar-proxy-read-for", /* XML_CALDAVSERV_PROXY_READ_FOR */
	"B:calendar-proxy-write", /* XML_CALDAVSERV_PROXY_WRITE */
	"B:calendar-proxy-write-for", /* XML_CALDAVSERV_PROXY_WRIT... */
	"D:bind", /* XML_DAV_BIND */
	"D:collection", /* XML_DAV_COLLECTION */
	"D:href", /* XML_DAV_HREF */
	"D:multistatus", /* XML_DAV_MULTISTATUS */
	"D:principal", /* XML_DAV_PRINCIPAL */
	"D:privilege", /* XML_DAV_PRIVILEGE */
	"D:prop", /* XML_DAV_PROP */
        "D:propstat", /* XML_DAV_PROPSTAT */
	"D:read", /* XML_DAV_READ */
	"D:read-current-user-privilege-set", /* XML_DAV_READ_CUR... */
	"D:resourcetype", /* XML_DAV_RESOURCETYPE */
	"D:response", /* XML_DAV_RESPONSE */
	"D:status", /* XML_DAV_STATUS */
	"D:unbind", /* XML_DAV_UNBIND */
	"D:write", /* XML_DAV_WRITE */
};

/*
 * Provided mainly for Linux that doesn't have arc4random.
 * Returns a (non-cryptographic) random number between [0, sz).
 */
static uint32_t
get_random_uniform(size_t sz)
{
#if HAVE_ARC4RANDOM
	return arc4random_uniform(sz);
#else
	return random() % sz;
#endif
}

int
xml_ical_putc(int c, void *arg)
{
	struct kxmlreq	*r = arg;

	kxml_putc(r, c);
	return(1);
}

static char
parsehex(char ch)
{

	return(isdigit((int)ch) ? ch - '0' : 
		tolower((int)ch) - 'a' + 10);
}

static void
http_decode(const char *in, char **rp)
{
	size_t	 i, j, sz;

	sz = strlen(in);
	*rp = kcalloc(sz + 1, 1);

	for (i = j = 0; i < sz; i++, j++) {
		if ('+' == in[i]) {
			(*rp)[j] = ' ';
			continue;
		} else if ('%' != in[i]) {
			(*rp)[j] = in[i];
			continue;
		}
		if ('\0' == in[i + 1] ||
		    '\0' == in[i + 2] ||
		    ! isalnum((int)in[i + 1]) ||
		    ! isalnum((int)in[i + 2])) {
			(*rp)[j] = in[i];
			continue;
		}
		(*rp)[j] = 
			parsehex(in[i + 1]) << 4 |
			parsehex(in[i + 2]);
		i += 2;
	}
}

/*
 * Decompose a path into the principal, collection, and resource parts.
 * We manage this as follows:
 *   /principal/collection/resource
 * For now, the collection can't have directories of its own.
 * Return zero on failure (no leading slash or memory failure), non-zero
 * on success.
 * All pointers will be set.
 */
int
http_paths(const char *in, char **pp, char **cp, char **rp)
{
	const char	*p;

	*pp = *cp = *rp = NULL;

	/* Strip leading absolute path. */
	if ('/' != in[0])
		return(0);
	in++;

	if (NULL != (p = strchr(in, '/'))) {
		*pp = malloc(p - in + 1);
		memcpy(*pp, in, p - in);
		(*pp)[p - in] = '\0';
		in = p + 1;
		if (NULL != (p = strrchr(in, '/'))) {
			*cp = malloc(p - in + 1);
			memcpy(*cp, in, p - in);
			(*cp)[p - in] = '\0';
			in = p + 1;
			http_decode(in, rp);
		} else {
			*cp = strdup("");
			http_decode(in, rp);
		}
	} else {
		*pp = strdup(in);
		*cp = strdup("");
		*rp = strdup("");
	}

	if (NULL == *pp || NULL == *cp || NULL == *rp) {
		kerr(NULL);
		free(*pp);
		free(*cp);
		free(*rp);
		return(0);
	}

	return(1);
}

/*
 * Check the safety of the string according to RFC 3986, section 3.3.
 * Note that we don't allow percent-encodings, `&', and the apostrophe.
 * This ensures that all words that will show up as path components
 * (e.g., the principal name) are in fact already well-formed and
 * needn't be re-encoded.
 */
int
http_safe_string(const char *cp)
{

	if ('\0' == *cp)
		return(0);
	else if (0 == strcmp(cp, ".") || 0 == strcmp(cp, ".."))
		return(0);

	for (; '\0' != *cp; cp++)
		switch (*cp) {
		case ('.'):
		case ('-'):
		case ('_'):
		case ('~'):
		case ('!'):
		case ('$'):
		case ('('):
		case (')'):
		case ('*'):
		case ('+'):
		case (','):
		case (';'):
		case ('='):
		case (':'):
		case ('@'):
			break;
		default:
			if (isalnum(*cp))
				break;
			return(0);
		}

	return(1);
}

int
http_ical_putc(int c, void *arg)
{
	struct kreq	*r = arg;

	khttp_putc(r, c);
	return(1);
}

void
http_error(struct kreq *r, enum khttp c)
{
	char	 nonce[17];
	size_t	 i;

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[c]);
	switch (c) {
	case (KHTTP_200):
	case (KHTTP_201):
	case (KHTTP_204):
	case (KHTTP_207):
	case (KHTTP_304):
		khttp_head(r, "DAV", "1, access-control, calendar-access");
		break;
	case (KHTTP_401):
		/*
		 * FIXME: we essentially throw away the nonce
		 * We should be keeping nonces in a database of client
		 * sessions, then using the increasing (not necessarily
		 * linearly, apparently) nonce-count value.
		 */
		for (i = 0; i < sizeof(nonce) - 1; i++)
			snprintf(nonce + i, 2, "%01X", 
				get_random_uniform(128));
		khttp_head(r, kresps[KRESP_WWW_AUTHENTICATE],
			"Digest realm=\"%s\", "
			"algorithm=\"MD5-sess\", "
			"qop=\"auth,auth-int\", "
			"nonce=\"%s\"", KREALM, nonce);
		break;
	default:
		break;
	}

	khttp_body(r);
}

/*
 * Parse an etag as described in HTTP 7232.
 * This simply strips out quotes and returns non-empty strings.
 * If the value is a literal, "buf" is set to non-NULL and must be
 * freed.
 * It works for both "If-Match" and "If-None-Match".
 * Returns the parsed etag or NULL on failure.
 */
const char *
http_etag_if_match(const char *val, char **buf)
{
	size_t	 sz;

	assert(val != NULL);

	*buf = NULL;

	/* Zero-length is a no-no. */

	if ((sz = strlen(val)) == 0)
		return NULL;

	/* Quoted-string needs to be \"[.+]\". */

	if (sz > 1 && val[0] == '"' && val[sz - 1] == '"') {
		val++;
		sz--;
		if (sz == 1)
			return NULL;
		*buf = kstrdup(val);
		assert(*buf != NULL);
		(*buf)[sz - 1] = '\0';
		return (*buf);
	}

	/* Un-quoted (or badly-quoted) value. */

	return val;
}
