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
#include <sys/param.h>

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/stdio.h>
#include <bsd/string.h>
#include <bsd/readpassphrase.h>
#else
#include <readpassphrase.h>
#endif
#include <unistd.h>

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
	const char	*phrase;
	MD5_CTX		 ctx;
	size_t		 i;
	unsigned char	 ha[MD5_DIGEST_LENGTH];

	assert('\0' != user[0] && '\0' != realm[0]);

	switch (new) {
	case (0):
		phrase = "Old password: ";
		break;
	case (1):
		phrase = "New password: ";
		break;
	case (2):
		phrase = "Repeat new password: ";
		break;
	default:
		abort();
	}

	/* Read in password. */
	pp = readpassphrase(phrase, pbuf, 
		sizeof(pbuf), RPP_REQUIRE_TTY);

	if (NULL == pp) {
		fprintf(stderr, "unable to read passphrase\n");
		explicit_bzero(pbuf, strlen(pbuf));
		return(0);
	} else if ('\0' == pbuf[0])
		return(0);

	if (strlen(pbuf) < 6) {
		fprintf(stderr, "come on: more than five letters\n");
		return(0);
	}

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

static int
pentrynew(struct pentry *p, const char *user, 
	const char *hash, const char *homedir, const char *email)
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

	/*
	 * If we don't have a home directory set, then default to the
	 * username within the root directory, /</user>/.
	 */
	if (NULL == homedir) {
		sz = strlen(user) + 3;
		if (NULL == (p->homedir = malloc(sz))) 
			return(0);
		strlcpy(p->homedir, "/", sz);
		strlcat(p->homedir, user, sz);
		strlcat(p->homedir, "/", sz);
	} else if (NULL == (p->homedir = strdup(homedir)))
		return(0);

	/*
	 * If we don't have an email address, then set it to be current
	 * username on the local host.
	 * If there's no hostname, then use "localhost".
	 */
	if (NULL == email) {
		sz = strlen(user) + MAXHOSTNAMELEN + 2;
		if (NULL == (p->gecos = malloc(sz)))
			return(0);
		strlcpy(p->gecos, user, sz);
		sz = strlcat(p->gecos, "@", sz);
		if (-1 == gethostname(p->gecos + sz, MAXHOSTNAMELEN))
			return(0);
		sz = strlen(user) + MAXHOSTNAMELEN + 2;
		if ('@' == p->gecos[strlen(p->gecos) - 1]) 
			strlcat(p->gecos, "localhost", sz);
	} else if (NULL == (p->gecos = strdup(email)))
		return(0);

	return(prncpl_pentry_check(p));
}

int
main(int argc, char *argv[])
{
	int	 	 c, fd, create, rc, passwd;
	char	 	 file[PATH_MAX], 
			 digestnew[MD5_DIGEST_LENGTH * 2 + 1],
			 digestrep[MD5_DIGEST_LENGTH * 2 + 1],
			 digestold[MD5_DIGEST_LENGTH * 2 + 1];
	const char	*pname, *realm, *homedir, 
	      		*altuser, *email;
	uid_t		 euid;
	gid_t		 egid;
	size_t		 i, sz, len, line, elsz;
	ssize_t		 found;
	struct pentry	 eline;
	struct pentry	*els;
	char		*cp, *user;
	FILE		*f;
	void		*pp;
	struct stat	 st;

	euid = geteuid();
	egid = getegid();

	/* 
	 * Temporarily drop privileges.
	 * We don't want to have them until we're opening the password
	 * file itself.
	 */
	if (-1 == setegid(getgid())) {
		kerr("setegid");
		return(EXIT_FAILURE);
	} else if (-1 == seteuid(getuid())) {
		kerr("seteuid");
		return(EXIT_FAILURE);
	}

	/* Establish program name. */
	if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;

	/* Prepare our default CALDIR/kcaldav.passwd file. */
	sz = strlcpy(file, CALDIR, sizeof(file));
	if (sz >= sizeof(file)) {
		kerrx("%s: default path too long", file);
		return(EXIT_FAILURE);
	} 
	
	f = NULL;
	realm = KREALM;
	create = 0;
	passwd = 1;
	user = NULL;
	els = NULL;
	elsz = 0;
	homedir = email = NULL;
	rc = 0;
	fd = -1;
	verbose = 0;
	altuser = NULL;

	while (-1 != (c = getopt(argc, argv, "Cd:e:f:nu:v"))) 
		switch (c) {
		case ('C'):
			create = passwd = 1;
			break;
		case ('d'):
			homedir = optarg;
			break;
		case ('e'):
			email = optarg;
			break;
		case ('f'):
			sz = strlcpy(file, optarg, sizeof(file));
			if (sz < sizeof(file))
				break;
			fprintf(stderr, "%s: path too long\n", file);
			goto usage;
		case ('n'):
			passwd = 0;
			break;
		case ('u'):
			altuser = optarg;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	assert('\0' != realm[0]);

	/* Arrange our kcaldav.passwd file. */
	if ('\0' == file[0]) {
		fprintf(stderr, "empty path!?\n");
		goto out;
	} else if ('/' == file[sz - 1])
		file[sz - 1] = '\0';
	sz = strlcat(file, "/kcaldav.passwd", sizeof(file));
	if (sz >= sizeof(file)) {
		kerrx("%s: path too long", file);
		goto out;
	}

	/* 
	 * Assign our user name.
	 * This is either going to be given with -u (which is a
	 * privileged operation) or inherited from our login creds.
	 */
	user = strdup(NULL == altuser ? getlogin() : altuser);
	if (NULL == user) {
		kerr(NULL);
		goto out;
	}

	/* 
	 * If we're not creating a new entry and we're setting our own
	 * password, get the existing password. 
	 */
	if ( ! create && NULL == altuser)
		if ( ! gethash(0, digestold, user, realm))
			goto out;

	/* If we're going to set our password, hash it now. */
	if (passwd) {
		if ( ! gethash(1, digestnew, user, realm)) 
			goto out;
		if ( ! gethash(2, digestrep, user, realm)) 
			goto out;
		if (memcmp(digestnew, digestrep, sizeof(digestnew))) {
			fprintf(stderr, "passwords do not match\n");
			goto out;
		}
	}

	/* Regain privileges. */
	if (-1 == setgid(egid)) {
		kerr("setgid");
		goto out;
	} else if (-1 == setuid(euid)) {
		kerr("setuid");
		goto out;
	}

	/*
	 * Open the file itself.
	 * This is a privileged operation, hence occuring with our
	 * regained effective credentials.
	 */
	if (-1 == (fd = open_lock_ex(file, O_RDWR, 0)))
		goto out;
	else if (NULL == (f = fdopen_lock(file, fd, "r")))
		goto out;

	/* Now drop privileges entirely! */
	if (-1 == setgid(getgid())) {
		kerr("setgid");
		goto out;
	} else if (-1 == setuid(getuid())) {
		kerr("setuid");
		goto out;
	}

	/* 
	 * Check security of privileged operations.
	 * Root is, of course, exempt.
	 */
	if (0 != getuid() && (create || NULL != altuser)) {
		if (-1 == fstat(fd, &st)) {
			kerr("fstat");
			goto out;
		} else if (st.st_uid != getuid()) {
			fprintf(stderr, "password file owner "
				"must match the real user\n");
			goto out;
		}
	}

	/*
	 * Scan the entire file into memory.
	 * This will fill in "els" with each principal entry.
	 * Each line is explicitly zeroed so as not to leave any
	 * password hashes in memory.
	 */
	line = 0;
	found = -1;
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
		if ( ! prncpl_pentry_dup(&els[elsz - 1], &eline)) {
			kerr(NULL);
			explicit_bzero(cp, len);
			break;
		} else if (0 == strcmp(eline.user, user))
			found = (ssize_t)(elsz - 1);

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

	if (create && found < 0) {
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
		c = pentrynew(&els[i], user, 
			digestnew, homedir, email);
		if (0 == c) {
			kerr(NULL);
			goto out;
		} 
	} else if (create) {
		fprintf(stderr, "%s: principal exists\n", user);
		goto out;
	} else if (found >= 0) {
		i = (size_t)found;
		assert(i < elsz);
		if (NULL == altuser && memcmp(els[i].hash, 
				digestold, sizeof(digestold))) {
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
		if ( ! prncpl_pentry_check(&els[i]))
			goto out;
	} else {
		fprintf(stderr, "%s: does not "
			"exist, use -C to add\n", user);
		goto out;
	}

	/* Zero the existing file and reset our position. */
	if (-1 == ftruncate(fd, 0)) {
		kerr("%s: ftruncate", file);
		fprintf(stderr, "%s: WARNING: FILE IN "
			"INCONSISTENT STATE\n", file);
		goto out;
	} else if (-1 == lseek(fd, 0, SEEK_SET)) {
		kerr("%s: lseek", file);
		fprintf(stderr, "%s: WARNING: FILE IN "
			"INCONSISTENT STATE\n", file);
		goto out;
	}

	/* 
	 * Flush to the open file descriptor.
	 * If this fails at any time, we're pretty much hosed.
	 * TODO: backup the original file and swap it back in, if
	 * something goes wrong.
	 */
	for (i = 0; i < elsz; i++)
		if ( ! prncpl_pentry_write(fd, file, &els[i])) {
			fprintf(stderr, "%s: WARNING: FILE IN "
				"INCONSISTENT STATE\n", file);
			break;
		}
out:
	free(user);
	prncpl_pentry_freelist(els, elsz);
	if (NULL != f)
		fclose(f);
	close_unlock(file, fd);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s "
		"[-Cn] "
		"[-d homedir] "
		"[-e email] "
		"[-f file] "
		"[-u user]\n", pname);
	return(EXIT_FAILURE);
}
