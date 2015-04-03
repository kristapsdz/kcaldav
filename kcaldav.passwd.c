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

static int
setpass(struct pentry *p, const char *realm)
{
	char		 pbuf[BUFSIZ];
	char		*pp;
	MD5_CTX		 ctx;
	size_t		 i;
	unsigned char	 ha[MD5_DIGEST_LENGTH];

	assert(NULL != p->pass);
	explicit_bzero(p->pass, strlen(p->pass));
	free(p->pass);

	if (NULL == (p->pass = malloc(MD5_DIGEST_LENGTH * 2 + 1))) {
		kerr(NULL);
		return(0);
	}

	pp = readpassphrase("New password: ", 
		pbuf, sizeof(pbuf), RPP_REQUIRE_TTY);
	if (NULL == pp) {
		fprintf(stderr, "unable to read passphrase\n");
		return(0);
	}

	MD5Init(&ctx);
	MD5Update(&ctx, p->user, strlen(p->user));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, realm, strlen(realm));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, pbuf, strlen(pbuf));
	MD5Final(ha, &ctx);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&p->pass[i * 2], 3, "%02x", ha[i]);
	explicit_bzero(pbuf, strlen(pbuf));
	return(1);
}

static void
pentryfree(struct pentry *p)
{

	if (NULL != p->pass)
		explicit_bzero(p->pass, strlen(p->pass));

	free(p->pass);
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

	if (NULL == (p->pass = strdup(ep->pass)))
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
pentrywrite(FILE *f, const char *file, const struct pentry *p)
{
	size_t	 len;
	int	 wrote;

	len = strlen(p->user) +
		strlen(p->pass) +
		strlen(p->uid) + 
		strlen(p->gid) +
		strlen(p->cls) +
		strlen(p->change) + 
		strlen(p->expire) + 
		strlen(p->gecos) + 
		strlen(p->homedir) +
		10;

	wrote = fprintf(f, "%s:%s:%s:%s:%s:%s:%s:%s:%s:\n",
		p->user, p->pass, p->uid, p->gid, p->cls,
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
	int	 	 c, fl, fd, newfd, create, found;
	char	 	 buf[PATH_MAX];
	const char	*file, *pname, *auser, *realm;
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

	while (-1 != (c = getopt(argc, argv, "cf:"))) 
		switch (c) {
		case ('c'):
			create = 1;
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
		pentriesfree(els, elsz);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	} else if (-1 == fclose(f)) {
		kerr("%s: fclose", file);
		pentriesfree(els, elsz);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	}

	/*
	 * Now we actually do the processing.
	 * We want to handle three separate cases:
	 *   (1) empty file, create with new entry
	 *   (2) non-empty file, add new entry
	 *   (3) non-empty file, modify existing entry
	 */
	if (0 == elsz && create) {
		/* New/empty file. */
		assert(0 == found);
		eline.user = (char *)auser;
		eline.pass = (char *)"";
		eline.uid = (char *)"";
		eline.gid = (char *)"";
		eline.cls = (char *)"";
		eline.change = (char *)"";
		eline.expire = (char *)"";
		eline.gecos = (char *)"";
		eline.homedir = (char *)"";
		els = malloc(sizeof(struct pentry));
		if (NULL == els) {
			kerr(NULL);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		} else if ( ! pentrydup(els, &eline)) {
			kerr(NULL);
			pentryfree(els);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		} else if ( ! setpass(els, realm)) {
			pentriesfree(els, elsz);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		}
		elsz = 1;
		found = 1;
	} else if (elsz && found) {
		/* File with existing entry. */
		for (i = 0; i < elsz ; i++)
			if (0 == strcmp(els[i].user, auser))
				break;
		assert(i < elsz);
		if ( ! setpass(&els[i], realm)) {
			pentriesfree(els, elsz);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		}
	} else if (elsz && create && ! found) {
		/* File needing new entry. */
		eline.user = (char *)auser;
		eline.pass = (char *)"";
		eline.uid = (char *)"";
		eline.gid = (char *)"";
		eline.cls = (char *)"";
		eline.change = (char *)"";
		eline.expire = (char *)"";
		eline.gecos = (char *)"";
		eline.homedir = (char *)"";
		pp = reallocarray(els, 
			elsz, sizeof(struct pentry));
		if (NULL == pp) {
			kerr(NULL);
			pentriesfree(els, elsz);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		} 
		els = pp;
		elsz++;
		if ( ! pentrydup(&els[elsz - 1], &eline)) {
			kerr(NULL);
			pentriesfree(els, elsz);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		} else if ( ! setpass(&els[elsz - 1], realm)) {
			pentriesfree(els, elsz);
			close_unlock(file, fd);
			return(EXIT_FAILURE);
		}
	} else {
		fprintf(stderr, "%s: no pre-existing "
			"principal, cannot add new\n", auser);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	}

	/* Now we re-open the file stream for writing. */
	if (lseek(fd, 0, SEEK_SET)) {
		kerr("%s: lseek", file);
		pentriesfree(els, elsz);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	} else if (-1 == (newfd = dup(fd))) {
		kerr("%s: dup", file);
		pentriesfree(els, elsz);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
	} else if (NULL == (f = fdopen(newfd, "w"))) {
		kerr("%s: fopen", file);
		pentriesfree(els, elsz);
		close_unlock(file, fd);
		return(EXIT_FAILURE);
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

	printf("%s: user modified: %s\n", file, auser);
	pentriesfree(els, elsz);
	close_unlock(file, fd);
	return(i == elsz ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-f passwd] user\n", pname);
	return(EXIT_FAILURE);
}
