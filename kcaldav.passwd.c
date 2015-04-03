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

#include <sys/mman.h>
#include <sys/stat.h>

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

int verbose = 0;

/*
 * Get a new password from the operator.
 * Store this in "digest", which must be MD5_DIGEST_LENGTH*2+1 in length
 * (as usual).
 * Both the user and realm strings must be non-empty.
 */
static int
gethash(int new, char *digest, 
	const char *user, const char *realm)
{
	char		 pbuf[BUFSIZ];
	char		*pp;
	MD5_CTX		 ctx;
	size_t		 i;
	unsigned char	 ha[MD5_DIGEST_LENGTH];

	assert('\0' != user[0] && '\0' != realm[0]);

	/* Read in password. */
	pp = readpassphrase(new ? 
		"New password: " : "Old password: ", 
		pbuf, sizeof(pbuf), RPP_REQUIRE_TTY);

	if (NULL == pp) {
		fprintf(stderr, "unable to read passphrase\n");
		explicit_bzero(pbuf, strlen(pbuf));
		return(0);
	} else if ('\0' == pbuf[0])
		return(0);

	/* Hash according to RFC 2069's HA1. */
	MD5Init(&ctx);
	MD5Update(&ctx, user, strlen(user));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, realm, strlen(realm));
	MD5Update(&ctx, ":", 1);
	MD5Update(&ctx, pbuf, strlen(pbuf));
	MD5Final(ha, &ctx);

	/* Convert to hex format. */
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&digest[i * 2], 3, "%02x", ha[i]);

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
pentrynew(struct pentry *p, const char *user, 
	const char *hash, const char *homedir)
{
	size_t 	 	 sz;
	const char	*empty = "";

	memset(p, 0, sizeof(struct pentry));

	if (NULL == (p->hash = strdup(hash)))
		return(0);
	else if (NULL == (p->user = strdup(user)))
		return(0);
	else if (NULL == (p->uid = strdup(empty)))
		return(0);
	else if (NULL == (p->gid = strdup(empty)))
		return(0);
	else if (NULL == (p->cls = strdup(empty)))
		return(0);
	else if (NULL == (p->change = strdup(empty)))
		return(0);
	else if (NULL == (p->expire = strdup(empty)))
		return(0);
	else if (NULL == (p->gecos = strdup(empty)))
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
pentrywordwrite(int fd, const char *file, const char *word)
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
static int
pentrywrite(int fd, const char *file, const struct pentry *p)
{

	if ( ! pentrywordwrite(fd, file, p->user))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->hash))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->uid))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->gid))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->cls))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->change))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->expire))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->gecos))
		return(0);
	if ( ! pentrywordwrite(fd, file, p->homedir))
		return(0);
	if ( ! pentrywordwrite(fd, file, "\n"))
		return(0);

	return(1);
}

int
main(int argc, char *argv[])
{
	int	 	 c, fd, create, found, rc, passwd, newfd;
	char	 	 file[PATH_MAX], 
			 digestnew[MD5_DIGEST_LENGTH * 2 + 1],
			 digestold[MD5_DIGEST_LENGTH * 2 + 1];
	const char	*pname, *auser, *realm, *homedir;
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
	sz = strlcpy(file, CALDIR, sizeof(file));
	if (sz >= sizeof(file)) {
		kerrx("%s: path too long", file);
		return(EXIT_FAILURE);
	} 
	
	realm = "kcaldav";
	create = 0;
	passwd = 1;
	els = NULL;
	elsz = 0;
	homedir = NULL;
	rc = 0;
	verbose = 0;

	while (-1 != (c = getopt(argc, argv, "cd:f:nv"))) 
		switch (c) {
		case ('c'):
			create = passwd = 1;
			break;
		case ('d'):
			homedir = optarg;
			if ('/' == homedir[0])
				break;
			fprintf(stderr, "%s: not absolute\n", homedir);
			goto usage;
		case ('f'):
			if ('\0' == optarg[0]) {
				fprintf(stderr, "empty path\n");
				goto usage;
			}
			sz = strlcpy(file, optarg, sizeof(file));
			if (sz < sizeof(file))
				break;
			fprintf(stderr, "%s: path too long\n", file);
			goto usage;
		case ('n'):
			passwd = 0;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;
	if (0 == argc) 
		goto usage;

	/* Arrange our path. */
	if ('/' == file[sz - 1])
		file[sz - 1] = '\0';
	sz = strlcat(file, "/kcaldav.passwd", sizeof(file));
	if (sz >= sizeof(file)) {
		kerrx("%s: path too long", file);
		return(EXIT_FAILURE);
	}

	/* 
	 * Process the username, make sure the realm is coherent, then
	 * immediately hash the password before we even acquire a lock
	 * on the password file.
	 */
	assert('\0' != realm[0]);
	auser = argv[0];
	if ('\0' == auser[0]) {
		fprintf(stderr, "empty username\n");
		goto usage;
	} 
	
	/* If we're not creating a new entry, get the existing. */
	if ( ! create && ! gethash(0, digestold, auser, realm))
		return(EXIT_FAILURE);
	/* If we're going to set our password, hash it now. */
	if (passwd && ! gethash(1, digestnew, auser, realm)) 
		return(EXIT_FAILURE);

	/*
	 * If we're allowed to create entries, allow us to create the
	 * file as well.
	 * We open this in shared mode because we don't want to block
	 * other readers while we're fiddling with password input.
	 * We then take a digest of the whole file.
	 */
	if (-1 == (fd = open_lock_ex(file, O_RDWR|O_CREAT, 0600)))
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
		kerr("%s: fdopen", file);
		close(fd);
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

	if (create && ! found) {
		/* 
		 * New/empty file or new entry.
		 * For this, we simply slap the requested new entry into
		 * the file and that's that.
		 */
		pp = reallocarray
			(els, elsz + 1, 
			 sizeof(struct pentry));
		if (NULL == pp) {
			kerr(NULL);
			goto out;
		} 
		els = pp;
		i = elsz++;
		if ( ! pentrynew(&els[i], auser, digestnew, homedir)) {
			kerr(NULL);
			goto out;
		} 
	} else if (create) {
		fprintf(stderr, "%s: principal exists\n", auser);
		goto out;
	} else if (found) {
		/* File with existing entry. */
		for (i = 0; i < elsz ; i++)
			if (0 == strcmp(els[i].user, auser))
				break;
		assert(i < elsz);
		if (memcmp(els[i].hash, digestold, sizeof(digestold))) {
			fprintf(stderr, "password mismatch\n");
			goto out;
		}
		if (NULL != homedir) {
			free(els[i].homedir);
			els[i].homedir = strdup(homedir);
			if (NULL == els[i].homedir) {
				kerr(NULL);
				goto out;
			}
		} 
		if (passwd)
			memcpy(els[i].hash, digestnew, sizeof(digestnew));
	} else {
		fprintf(stderr, "%s: does not "
			"exist, use -c to add\n", auser);
		goto out;
	}

	if (-1 == ftruncate(fd, 0)) {
		kerr("%s: ftruncate", file);
		goto out;
	} else if (-1 == lseek(fd, 0, SEEK_SET)) {
		kerr("%s: lseek", file);
		goto out;
	}

	/* Flush to the file stream. */
	for (i = 0; i < elsz; i++)
		if ( ! pentrywrite(fd, file, &els[i])) {
			fprintf(stderr, "%s: WARNING: FILE IN "
				"INCONSISTENT STATE\n", file);
			break;
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
