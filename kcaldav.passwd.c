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
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#include <bsd/readpassphrase.h>
#else
#include <readpassphrase.h>
#endif

#include "extern.h"
#include "md5.h"

/*
 * Read in the password from stdin.
 * This requires that we're connected to a terminal.
 * Then compose the password into a hash HA1 as defined by RFC 2069,
 * which we'll use for authentication.
 */
static int
setpass(struct pentry *p, const char *realm)
{
	char		 pbuf[BUFSIZ];
	char		*pp;
	MD5_CTX		 ctx;
	size_t		 i;
	unsigned char	 ha[MD5_DIGEST_LENGTH];

	/* Zero and free, just in case. */
	assert(NULL != p->hash);
	explicit_bzero(p->hash, strlen(p->hash));
	free(p->hash);
	p->hash = malloc(MD5_DIGEST_LENGTH * 2 + 1);
	if (NULL == p->hash) {
		kerr(NULL);
		return(0);
	}

	/* Read in password. */
	pp = readpassphrase("New password: ", 
		pbuf, sizeof(pbuf), RPP_REQUIRE_TTY);
	if (NULL == pp) {
		fprintf(stderr, "unable to read passphrase\n");
		explicit_bzero(pbuf, strlen(pbuf));
		return(0);
	}

	/* Hash according to RFC 2069's HA1. */
	MD5Init(&ctx);
	MD5Update(&ctx, p->user, strlen(p->user));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, realm, strlen(realm));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, pbuf, strlen(pbuf));
	MD5Final(ha, &ctx);

	/* Convert to hex format. */
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&p->hash[i * 2], 3, "%02x", ha[i]);

	/* Zero the password buffer. */
	explicit_bzero(pbuf, strlen(pbuf));
	return(1);
}

static void
pentryfree(struct pentry *p)
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

static void
pentriesfree(struct pentry *p, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++)
		pentryfree(&p[i]);
	free(p);
}

static int
pentrydup(struct pentry *p, const struct pentry *ep)
{

	memset(p, 0, sizeof(struct pentry));

	if (NULL == (p->hash = strdup(ep->hash)))
		return(0);
	else if (NULL == (p->user = strdup(ep->user)))
		return(0);
	else if (NULL == (p->uid = strdup(ep->uid)))
		return(0);
	else if (NULL == (p->gid = strdup(ep->gid)))
		return(0);
	else if (NULL == (p->cls = strdup(ep->cls)))
		return(0);
	else if (NULL == (p->change = strdup(ep->change)))
		return(0);
	else if (NULL == (p->expire = strdup(ep->expire)))
		return(0);
	else if (NULL == (p->gecos = strdup(ep->gecos)))
		return(0);
	else if (NULL == (p->homedir = strdup(ep->homedir)))
		return(0);

	return(1);
}

static int
pentrynew(struct pentry *p, const char *user, const char *homedir)
{
	size_t 	 sz;

	memset(p, 0, sizeof(struct pentry));

	if (NULL == (p->hash = strdup("")))
		return(0);
	else if (NULL == (p->user = strdup(user)))
		return(0);
	else if (NULL == (p->uid = strdup("")))
		return(0);
	else if (NULL == (p->gid = strdup("")))
		return(0);
	else if (NULL == (p->cls = strdup("")))
		return(0);
	else if (NULL == (p->change = strdup("")))
		return(0);
	else if (NULL == (p->expire = strdup("")))
		return(0);
	else if (NULL == (p->gecos = strdup("")))
		return(0);

	if (NULL == homedir) {
		sz = strlen(user) + 2;
		if (NULL == (p->homedir = malloc(sz))) 
			return(0);
		strlcpy(p->homedir, "/", sz);
		strlcat(p->homedir, user, sz);
	} else if (NULL == (p->homedir = strdup(homedir)))
		return(0);

	return(1);
}

static int
pentrywrite(FILE *f, const char *file, const struct pentry *p)
{
	size_t	 len;
	int	 wrote;

	len = strlen(p->user) +
		strlen(p->hash) +
		strlen(p->uid) + 
		strlen(p->gid) +
		strlen(p->cls) +
		strlen(p->change) + 
		strlen(p->expire) + 
		strlen(p->gecos) + 
		strlen(p->homedir) +
		10;

	wrote = fprintf(f, "%s:%s:%s:%s:%s:%s:%s:%s:%s:\n",
		p->user, p->hash, p->uid, p->gid, p->cls,
		p->change, p->expire, p->gecos, p->homedir);

	if (wrote < 1)
		kerr("%s: write", file);
	else if (len != (size_t)wrote)
		kerrx("%s: short write", file);
	else
		return(1);

	return(0);
}

int
main(int argc, char *argv[])
{
	int	 	 c, fl, fd, newfd, create, found, rc;
	char	 	 buf[PATH_MAX];
	const char	*file, *pname, *auser, *realm, *homedir;
	size_t		 i, sz, len, line, elsz;
	struct pentry	 eline;
	struct pentry	*els;
	char		*cp;
	FILE		*f;
	void		*pp;

	if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;

	/* Prepare our default CALDIR/kcaldav.passwd file. */
	sz = strlcpy(buf, CALDIR, sizeof(buf));
	if (sz >= sizeof(buf)) {
		kerrx("%s: path too long", buf);
		return(EXIT_FAILURE);
	} else if ('/' == buf[sz - 1])
		buf[sz - 1] = '\0';

	sz = strlcat(buf, "/kcaldav.passwd", sizeof(buf));
	if (sz >= sizeof(buf)) {
		kerrx("%s: path too long", buf);
		return(EXIT_FAILURE);
	}

	realm = "kcaldav";
	file = buf;
	create = 0;
	els = NULL;
	elsz = 0;
	homedir = NULL;
	rc = 0;

	while (-1 != (c = getopt(argc, argv, "cd:f:"))) 
		switch (c) {
		case ('c'):
			create = 1;
			break;
		case ('d'):
			homedir = optarg;
			break;
		case ('f'):
			file = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;
	if (0 == argc) 
		goto usage;

	auser = argv[0];

	/*
	 * If we're allowed to create entries, allow us to create the
	 * file as well.
	 * Otherwise, read only--but both with an exclusive lock.
	 */
	fl = create ? O_RDWR|O_CREAT : O_RDWR;
	if (-1 == (fd = open_lock_ex(file, fl, 0600)))
		return(EXIT_FAILURE);

	/* 
	 * Make a copy of our file descriptor because fclose() will
	 * close() the one that we currently have.
	 */
	if (-1 == (newfd = dup(fd))) {
		kerr("%s: dup", file);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	} else if (NULL == (f = fdopen(newfd, "r"))) {
		kerr("%s: fopen", file);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	}

	/*
	 * Scan the entire file into memory.
	 * This will fill in "els" with each principal entry.
	 * Each line is explicitly zeroed so as not to leave any
	 * password hashes in memory.
	 */
	line = 0;
	found = 0;
	while (NULL != (cp = fgetln(f, &len))) {
		if ( ! prncpl_line(cp, len, file, ++line, &eline)) {
			explicit_bzero(cp, len);
			break;
		}
		pp = reallocarray(els, 
			elsz + 1, sizeof(struct pentry));
		if (NULL == pp) {
			kerr(NULL);
			break;
		} 
		els = pp;
		elsz++;
		if ( ! pentrydup(&els[elsz - 1], &eline)) {
			kerr(NULL);
			explicit_bzero(cp, len);
			break;
		} else if (0 == strcmp(eline.user, auser))
			found = 1;

		/* Next line... */
		explicit_bzero(cp, len);
	}

	/* 
	 * If we had errors (we didn't reach EOF), bail out. 
	 * Bail if we can't close the file as well.
	 */
	if ( ! feof(f)) {
		if (ferror(f))
			kerr("%s: fgetln", file);
		if (-1 == fclose(f))
			kerr("%s: fclose", file);
		goto out;
	} else if (-1 == fclose(f)) {
		kerr("%s: fclose", file);
		goto out;
	}

	/*
	 * Now we actually do the processing.
	 * We want to handle three separate cases:
	 *   (1) empty file, create with new entry
	 *   (2) non-empty file, add new entry
	 *   (3) non-empty file, modify existing entry
	 */
	if (create && (0 == elsz || (elsz && ! found))) {
		/* New/empty file or new entry. */
		pp = reallocarray
			(els, elsz + 1, 
			 sizeof(struct pentry));
		if (NULL == pp) {
			kerr(NULL);
			goto out;
		} 
		els = pp;
		i = elsz++;
		if ( ! pentrynew(&els[i], auser, homedir)) {
			kerr(NULL);
			goto out;
		} else if ( ! setpass(&els[i], realm))
			goto out;
	} else if (elsz && found) {
		/* File with existing entry. */
		for (i = 0; i < elsz ; i++)
			if (0 == strcmp(els[i].user, auser))
				break;
		assert(i < elsz);
		if (NULL != homedir) {
			free(els[i].homedir);
			els[i].homedir = strdup(homedir);
			if (NULL == els[i].homedir) {
				kerr(NULL);
				goto out;
			}
		}
		if ( ! setpass(&els[i], realm))
			goto out;
	} else {
		fprintf(stderr, "%s: no pre-existing "
			"principal, cannot add new\n", auser);
		goto out;
	}

	/* Now we re-open the file stream for writing. */
	if (lseek(fd, 0, SEEK_SET)) {
		kerr("%s: lseek", file);
		goto out;
	} else if (-1 == (newfd = dup(fd))) {
		kerr("%s: dup", file);
		goto out;
	} else if (NULL == (f = fdopen(newfd, "w"))) {
		kerr("%s: fopen", file);
		goto out;
	}

	/* Flush to the file stream. */
	for (i = 0; i < elsz; i++)
		if ( ! pentrywrite(f, file, &els[i])) {
			fprintf(stderr, "%s: WARNING: FILE IN "
				"INCONSISTENT STATE\n", file);
			break;
		}

	if (-1 == fclose(f))
		kerr("%s: fclose", file);

	if (i == elsz) {
		printf("%s: user modified: %s\n", file, auser);
		rc = 1;
	}
out:
	pentriesfree(els, elsz);
	close_unlock(file, fd);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s "
		"[-d homedir] "
		"[-f file] "
		"user\n", pname);
	return(EXIT_FAILURE);
}
