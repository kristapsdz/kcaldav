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

#include <sys/param.h>

#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
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

int
main(int argc, char *argv[])
{
	int	 	 c, fd, create, rc, passwd;
	char	 	 dnew[MD5_DIGEST_LENGTH * 2 + 1],
			 drep[MD5_DIGEST_LENGTH * 2 + 1],
			 dold[MD5_DIGEST_LENGTH * 2 + 1];
	const char	*pname, *realm, *altuser, *dir, *email;
	size_t		 sz;
	uid_t		 euid;
	gid_t		 egid;
	struct prncpl	*p;
	char		*user, *emailp;

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

	if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;

	emailp = NULL;
	p = NULL;
	dir = CALDIR;
	realm = KREALM;
	create = 0;
	passwd = 1;
	email = NULL;
	user = NULL;
	rc = 0;
	fd = -1;
	verbose = 0;
	altuser = NULL;

	while (-1 != (c = getopt(argc, argv, "Ce:f:nu:v"))) 
		switch (c) {
		case ('C'):
			create = passwd = 1;
			break;
		case ('e'):
			email = optarg;
			break;
		case ('f'):
			dir = optarg;
			break;
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

	/* 
	 * Assign our user name.
	 * This is either going to be given with -u (which is a
	 * privileged operation) or inherited from our login creds.
	 */
	user = strdup(NULL == altuser ? getlogin() : altuser);
	if (NULL == user) {
		kerr(NULL);
		goto out;
	} else if ('\0' == user[0]) {
		kerrx("zero-length username");
		goto out;
	}

	/* 
	 * If we're not creating a new entry and we're setting our own
	 * password, get the existing password. 
	 */
	if ( ! create && NULL == altuser)
		if ( ! gethash(0, dold, user, realm))
			goto out;

	/* If we're going to set our password, hash it now. */
	if (passwd) {
		if ( ! gethash(1, dnew, user, realm)) 
			goto out;
		if ( ! gethash(2, drep, user, realm)) 
			goto out;
		if (memcmp(dnew, drep, sizeof(dnew))) {
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
	 * Open the database file itself.
	 * This is a privileged operation, hence occuring with our
	 * regained effective credentials.
	 * We only try to create the database if "create" is specified.
	 */
	if ( ! db_init(dir, create))
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
	 * This will also create the database schema if the database has
	 * been created with "create" but doesn't exist yet.
	 */
	if (create || NULL != altuser) {
		if (0 == (rc = db_owner_check_or_set(getuid()))) {
			fprintf(stderr, "password file owner "
				"must match the real user\n");
			goto out;
		} else if (rc < 0)
			goto out;
	}

	/* Now either create or update the principal. */
	if (create) {
		if (NULL == email) {
			sz = strlen(user) + MAXHOSTNAMELEN + 2;
			emailp = malloc(sz);
			if (NULL == emailp) {
				kerr(NULL);
				goto out;
			}
			strlcpy(emailp, user, sz);
			strlcat(emailp, "@", sz);
			gethostname(emailp + strlen(user) + 1, 
				sz - strlen(user) - 1);
			email = emailp;
		}
		c = db_prncpl_new(user, dnew, email);
		if (0 == c) {
			fprintf(stderr, "%s: principal exists\n", user);
			goto out;
		} else if (c < 0)
			goto out;
	} else if ((c = db_prncpl_load(&p, user)) > 0) {
		if (NULL == altuser)
			if (memcmp(p->hash, dold, sizeof(dold))) {
				fprintf(stderr, "password mismatch\n");
				goto out;
			}
		if (NULL != email) {
			free(p->email);
			p->email = strdup(email);
			if (NULL == p->email) {
				kerr(NULL);
				goto out;
			}
		}
		if (passwd) {
			free(p->hash);
			p->hash = strdup(dnew);
			if (NULL == p->hash) {
				kerr(NULL);
				goto out;
			}
		}
		if ( ! db_prncpl_update(p))
			goto out;
	} else if (0 == c) {
		fprintf(stderr, "%s: does not "
			"exist, use -C to add\n", user);
		goto out;
	}

	rc = 1;
out:
	prncpl_free(p);
	free(user);
	free(emailp);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s "
		"[-Cn] "
		"[-e email] "
		"[-f file] "
		"[-u user]\n", pname);
	return(EXIT_FAILURE);
}
