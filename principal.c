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
static int
validate(const char *hash, const char *method,
	const struct httpauth *auth,
	const char *req, size_t reqsz)
{
	MD5_CTX	 	 ctx;
	unsigned char	 ha1[MD5_DIGEST_LENGTH],
			 ha2[MD5_DIGEST_LENGTH],
			 ha3[MD5_DIGEST_LENGTH];
	char		 skey1[MD5_DIGEST_LENGTH * 2 + 1],
			 skey2[MD5_DIGEST_LENGTH * 2 + 1],
			 skey3[MD5_DIGEST_LENGTH * 2 + 1],
			 count[9];
	size_t		 i;

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
		MD5Update(&ctx, method, strlen(method));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, auth->uri, strlen(auth->uri));
		MD5Update(&ctx, ":", 1);
		MD5Update(&ctx, req, reqsz);
		MD5Final(ha2, &ctx);
	} else {
		MD5Init(&ctx);
		MD5Update(&ctx, method, strlen(method));
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

/*
 * Verify that a (possibly empty) string is lowercase hex.
 */
static int
prncpl_hexstring(const char *cp)
{

	/* No non-ASCII, quote, colon, or escape chars. */
	for ( ; '\0' != *cp; cp++) {
		if (isdigit(*cp))
			continue;
		if (isalpha(*cp) && islower(*cp))
			continue;
		return(0);
	}
	return(1);
}

static int
prncpl_pentry_wwrite(int fd, const char *file, const char *word)
{
	size_t	 len;
	ssize_t	 ssz;

	len = strlen(word);

	if ((ssz = write(fd, word, len)) < 0)
		kerr("%s: write", file);
	else if (len != (size_t)ssz)
		kerrx("%s: short write", file);
	else if ('\n' == word[0])
		return(1);
	else if ((ssz = write(fd, ":", 1)) < 0)
		kerr("%s: write", file);
	else if (1 != (size_t)ssz)
		kerrx("%s: short write", file);
	else
		return(1);

	return(0);
}

/*
 * Make sure we write exactly the correct number of bytes into the
 * password file.
 * Any short writes must result in an immediate error, as the password
 * file is now garbled!
 */
int
prncpl_pentry_write(int fd, const char *file, const struct pentry *p)
{

	if ( ! prncpl_pentry_wwrite(fd, file, p->user))
		kerrx("%s: failed write user", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->hash))
		kerrx("%s: failed write hash", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->uid))
		kerrx("%s: failed write uid", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->gid))
		kerrx("%s: failed write gid", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->cls))
		kerrx("%s: failed write class", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->change))
		kerrx("%s: failed write change", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->expire))
		kerrx("%s: failed write expires", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->gecos))
		kerrx("%s: failed write gecos", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, p->homedir))
		kerrx("%s: failed write homedir", file);
	else if ( ! prncpl_pentry_wwrite(fd, file, "\n"))
		kerrx("%s: failed write end of line", file);
	else
		return(1);

	return(0);
}

/*
 * Make sure that the string is valid (though possibly empty).
 * We accept OCTET strings (see section 2.2 in RFC 2616) but disallow
 * quotes (so quoted pairs aren't confused), colons (so the password
 * file isn't confused), and escapes (so escape mechanisms aren't
 * confused).
 */
static int
prncpl_string(const char *cp)
{

	for ( ; '\0' != *cp; cp++)
		if ('"' == *cp || ':' == *cp || '\\' == *cp)
			return(0);

	return(1);
}

/*
 * Free a principal file entry, safely zeroing its hash as well.
 * This DOES NOT free the pointer itself.
 */
void
prncpl_pentry_free(struct pentry *p)
{

	if (NULL != p->hash)
		explicit_bzero(p->hash, strlen(p->hash));

	free(p->hash);
	free(p->user);
	free(p->uid);
	free(p->gid);
	free(p->cls);
	free(p->change);
	free(p->expire);
	free(p->gecos);
	free(p->homedir);
}

void
prncpl_pentry_freelist(struct pentry *p, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++) 
		prncpl_pentry_free(&p[i]);
	free(p);
}

/*
 * Duplicate all fields of "ep" into "p", calling prncpl_pentry_check()
 * if the fields allocate properly.
 * Returns zero on failure (the error has already been reported) or if
 * the fields are bogus, else it returns non-zero.
 */
int
prncpl_pentry_dup(struct pentry *p, const struct pentry *ep)
{

	memset(p, 0, sizeof(struct pentry));

	if (NULL == (p->hash = strdup(ep->hash)))
		kerr(NULL);
	else if (NULL == (p->user = strdup(ep->user)))
		kerr(NULL);
	else if (NULL == (p->uid = strdup(ep->uid)))
		kerr(NULL);
	else if (NULL == (p->gid = strdup(ep->gid)))
		kerr(NULL);
	else if (NULL == (p->cls = strdup(ep->cls)))
		kerr(NULL);
	else if (NULL == (p->change = strdup(ep->change)))
		kerr(NULL);
	else if (NULL == (p->expire = strdup(ep->expire)))
		kerr(NULL);
	else if (NULL == (p->gecos = strdup(ep->gecos)))
		kerr(NULL);
	else if (NULL == (p->homedir = strdup(ep->homedir)))
		kerr(NULL);
	else
		return(prncpl_pentry_check(p));

	return(0);
}

/*
 * Validate the entries in "p".
 * This function MUST be used when reading from or before writing to the
 * password file!
 */
int
prncpl_pentry_check(const struct pentry *p)
{

	if ( ! prncpl_string(p->user))
		kerrx("bad user string");
	else if ( ! prncpl_string(p->hash))
		kerrx("bad hash string");
	else if ( ! prncpl_string(p->uid))
		kerrx("bad uid string");
	else if ( ! prncpl_string(p->gid))
		kerrx("bad gid string");
	else if ( ! prncpl_string(p->cls))
		kerrx("bad class string");
	else if ( ! prncpl_string(p->change))
		kerrx("bad change string");
	else if ( ! prncpl_string(p->expire))
		kerrx("bad expire string");
	else if ( ! prncpl_string(p->gecos))
		kerrx("bad gecos string");
	else if ( ! prncpl_string(p->homedir))
		kerrx("bad homedir string");
	else if ('\0' == p->user[0])
		kerrx("empty username");
	else if ('\0' == p->gecos[0])
		kerrx("empty gecos");
	else if (MD5_DIGEST_LENGTH * 2 != strlen(p->hash))
		kerrx("bad MD5 digest length");
	else if ( ! prncpl_hexstring(p->hash))
		kerrx("bad MD5 hex string");
	else if ('/' != p->homedir[0])
		kerrx("homedir not absolute");
	else if (strstr(p->homedir, "../") || strstr(p->homedir, "/.."))
		kerrx("path traversed in homedir");
	else
		return(1);

	return(0);
}

/*
 * Given a newline-terminated "string" (NOT nil-terminated), first
 * terminate the string the parse its colon-separated parts.
 * The "p" fields must (1) each exist in the string and (2) pass
 * prncpl_pentry_check(), which is run automatically.
 * If either of these fails, zero is returned; else non-zero.
 */
int
prncpl_line(char *string, size_t sz,
	const char *file, size_t line, struct pentry *p)
{

	memset(p, 0, sizeof(struct pentry));
	if (0 == sz || '\n' != string[sz - 1]) {
		kerrx("%s:%zu: no newline", file, line);
		return(0);
	} 
	string[sz - 1] = '\0';

	/* Read in all of the required fields. */
	if (NULL == (p->user = strsep(&string, ":")))
		kerrx("%s:%zu: missing name", file, line);
	else if (NULL == (p->hash = strsep(&string, ":")))
		kerrx("%s:%zu: missing hash", file, line);
	else if (NULL == (p->uid = strsep(&string, ":")))
		kerrx("%s:%zu: missing uid", file, line);
	else if (NULL == (p->gid = strsep(&string, ":")))
		kerrx("%s:%zu: missing gid", file, line);
	else if (NULL == (p->cls = strsep(&string, ":")))
		kerrx("%s:%zu: missing class", file, line);
	else if (NULL == (p->change = strsep(&string, ":")))
		kerrx("%s:%zu: missing change", file, line);
	else if (NULL == (p->expire = strsep(&string, ":")))
		kerrx("%s:%zu: missing expire", file, line);
	else if (NULL == (p->gecos = strsep(&string, ":")))
		kerrx("%s:%zu: missing gecos", file, line);
	else if (NULL == (p->homedir = strsep(&string, ":")))
		kerrx("%s:%zu: missing homedir", file, line);
	else if ( ! prncpl_pentry_check(p))
		kerrx("%s:%zu: invalid field(s)", file, line);
	else
		return(1);

	return(0);
}

int
prncpl_replace(const char *file, const char *name, 
	const char *hash, const char *email)
{
	size_t	 	 len, line, psz, i;
	struct pentry	 entry;
	struct pentry	*ps;
	void		*pp;
	char		*cp;
	FILE		*f;
	int	 	 fd;

	if (-1 == (fd = open_lock_ex(file, O_RDWR, 0)))
		return(0);
	else if (NULL == (f = fdopen_lock(file, fd, "r")))
		return(0);

	/*
	 * Begin by reading in the existing file.
	 * We use fgetln() to do this properly.
	 * Validate all fields as we read them in (in the usual way).
	 */
	psz = 0;
	ps = NULL;
	line = 0;
	while (NULL != (cp = fgetln(f, &len))) {
		if ( ! prncpl_line(cp, len, file, ++line, &entry))
			goto err;
		/*
		 * Reallocate the number of available pentries, copy the
		 * temporary pentry into the array, then increment the
		 * number of objects.
		 * Make sure we fail properly in the event of errors.
		 */
		pp = reallocarray(ps, psz + 1, sizeof(struct pentry));
		if (NULL == pp) {
			kerr(NULL);
			goto err;
		}
		ps = pp;
		if ( ! prncpl_pentry_dup(&ps[psz], &entry)) {
			kerrx("%s:%zu: failed dup", file, line);
			goto err;
		}
		psz++;
		explicit_bzero(cp, len);
		cp = NULL;

		/*
		 * If this is the entry that concerns us, then begin
		 * making our changes now.
		 */
		if (strcmp(name, ps[psz - 1].user)) 
			continue;
		if (NULL != hash) {
			free(ps[psz - 1].hash);
			ps[psz - 1].hash = strdup(hash);
			if (NULL == ps[psz - 1].hash) {
				kerr(NULL);
				goto err;
			}
		}
		if (NULL != email) {
			free(ps[psz - 1].gecos);
			ps[psz - 1].gecos = strdup(email);
			if (NULL == ps[psz - 1].gecos) {
				kerr(NULL);
				goto err;
			}
		}
	}

	if ( ! feof(f)) {
		kerr("%s: fgetln", file);
		goto err;
	} else if (-1 == fclose(f)) {
		kerr("%s: fclose", file);
		goto err;
	}
	
	if (-1 == ftruncate(fd, 0)) {
		kerr("%s: ftruncate", file);
		fprintf(stderr, "%s: WARNING: FILE IN "
			"INCONSISTENT STATE\n", file);
		close_unlock(file, fd);
		prncpl_pentry_freelist(ps, psz);
		return(0);
	} else if (-1 == lseek(fd, 0, SEEK_SET)) {
		kerr("%s: lseek", file);
		fprintf(stderr, "%s: WARNING: FILE IN "
			"INCONSISTENT STATE\n", file);
		close_unlock(file, fd);
		prncpl_pentry_freelist(ps, psz);
		return(0);
	}

	/* 
	 * Flush to the open file descriptor.
	 * If this fails at any time, we're pretty much hosed.
	 * TODO: backup the original file and swap it back in, if
	 * something goes wrong.
	 */
	for (i = 0; i < psz; i++)
		if ( ! prncpl_pentry_write(fd, file, &ps[i])) {
			fprintf(stderr, "%s: WARNING: FILE IN "
				"INCONSISTENT STATE\n", file);
			break;
		}

	if (i == psz)
		kinfo("%s: principal file modified "
			"by %s", file, name);

	close_unlock(file, fd);
	prncpl_pentry_freelist(ps, psz);
	return(i == psz);
err:
	if (NULL != cp)
		explicit_bzero(cp, len);

	fclose_unlock(file, f, fd);
	prncpl_pentry_freelist(ps, psz);
	return(0);
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
prncpl_parse(const char *file, const char *m,
	const struct httpauth *auth, struct prncpl **pp,
	const char *r, size_t rsz)
{
	size_t	 	 len, line;
	struct pentry	 entry;
	char		*cp;
	FILE		*f;
	int	 	 rc, fd;

	*pp = NULL;

	/* We want to have a shared (readonly) lock. */
	if (-1 == (fd = open_lock_sh(file, O_RDONLY, 0)))
		return(0);
	else if (NULL == (f = fdopen_lock(file, fd, "r")))
		return(0);

	rc = -2;
	line = 0;
	/*
	 * Carefully make sure that we explicit_bzero() then contents of
	 * the memory buffer after each read.
	 * This prevents an adversary to gaining control of the
	 * password hashes that are in this file via our memory.
	 */
	while (NULL != (cp = fgetln(f, &len))) {
		if ( ! prncpl_line(cp, len, file, ++line, &entry)) {
			rc = 0;
			explicit_bzero(cp, len);
			break;
		}

		/* XXX: blind parse. */
		if (NULL == auth)
			continue;

		/* Is this the user we're looking for? */

		if (strcmp(entry.user, auth->user)) {
			explicit_bzero(cp, len);
			continue;
		} 
		
		if ( ! validate(entry.hash, m, auth, r, rsz)) {
			kerrx("%s:%zu: HTTP authorisation "
				"failed", file, line);
			explicit_bzero(cp, len);
			rc = 1;
			break;
		}

		/* Allocate the principal. */
		rc = -1;
		if (NULL == (*pp = calloc(1, sizeof(struct prncpl)))) 
			kerr(NULL);
		else if (NULL == ((*pp)->name = strdup(entry.user)))
			kerr(NULL);
		else if (NULL == ((*pp)->homedir = strdup(entry.homedir)))
			kerr(NULL);
		else if (NULL == ((*pp)->email = strdup(entry.gecos)))
			kerr(NULL);
		else
			rc = 1;

		if (NULL != *pp)
			(*pp)->writable = -1 != access(file, W_OK);

		explicit_bzero(cp, len);
		break;
	}

	/* If we had errors, bail out now. */
	if ( ! feof(f) && rc <= 0) {
		if (-2 == rc)
			kerr("%s: fgetln", file);
		fclose_unlock(file, f, fd);
		prncpl_free(*pp);
		pp = NULL;
		return(rc < 0 ? -1 : rc);
	}

	fclose_unlock(file, f, fd);
	return(1);
}

void
prncpl_free(struct prncpl *p)
{

	if (NULL == p)
		return;

	free(p->name);
	free(p->homedir);
	free(p->email);
	free(p);
}

