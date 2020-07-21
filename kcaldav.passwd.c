/*	$Id$ */
/*
 * Copyright (c) 2015, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#if HAVE_MD5
# include <md5.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_READPASSPHRASE
# include <readpassphrase.h>
#endif
#if !HAVE_ARC4RANDOM
# include <time.h> /* time(3) */
#endif
#include <unistd.h>

#include "libkcaldav.h"
#include "extern.h"

int verbose = 0;

/*
 * See http_safe_string() in util.c.
 * This isn't included directly because it's in the kcaldav front-end
 * library.
 * It verifies non-empty strings against RFC 3986, section 3.3.
 */
static int
check_safe_string(const char *cp)
{

	if (*cp == '\0')
		return 0;

	if (strcmp(cp, ".") == 0 || strcmp(cp, "..") == 0)
		return 0;

	for (; *cp != '\0'; cp++)
		switch (*cp) {
		case '.':
		case '-':
		case '_':
		case '~':
		case '!':
		case '$':
		case '(':
		case ')':
		case '*':
		case '+':
		case ',':
		case ';':
		case '=':
		case ':':
		case '@':
			break;
		default:
			if (isalnum((unsigned char)*cp))
				break;
			return 0;
		}

	return 1;
}

/*
 * Read the file "name" into the returned buffer.
 * The file is guaranteed to be a proper iCalendar file.
 */
static char *
read_whole_file(const char *name)
{
	int		 fd;
	char		*p = NULL;
	char		 buf[BUFSIZ];
	ssize_t		 ssz;
	size_t		 sz = 0;
	struct ical	*ical = NULL;

	/*
	 * Open the file readonly and read it in BUFSIZ at a time til
	 * we've read the whole file.
	 * NUL-terminate always, although we might read in NUL bytes
	 * (these are filtered in ical_parse()).
	 */

	if (-1 == (fd = open(name, O_RDONLY, 0)))
		err(1, "%s", name);

	while ((ssz = read(fd, buf, sizeof(buf))) > 0) {
		p = realloc(p, sz + ssz + 1);
		if (NULL == p)
			err(1, NULL);
		memcpy(&p[sz], buf, ssz);
		p[sz + ssz] = '\0';
		sz += ssz;
	}
	if (ssz < 0)
		err(1, "%s", name);
	close(fd);
	fd = -1;

	/* Try to parse as an iCalendar. */

	if ((ical = ical_parse(name, p, sz)) == NULL) 
		errx(1, "%s: not an iCalendar file", name);

	ical_free(ical);
	return p;
}

/*
 * Get a new password from the operator.
 * Store this in "digest", which must be MD5_DIGEST_LENGTH*2+1 in length
 * (as usual).
 * Both the user and realm strings must be non-empty.
 */
static void
gethash(int new, char *digest, 
	const char *user, const char *realm)
{
	char		 pbuf[BUFSIZ];
	char		*pp;
	const char	*phrase;
	MD5_CTX		 ctx;
	size_t		 i;
	unsigned char	 ha[MD5_DIGEST_LENGTH];

	switch (new) {
	case 0:
		phrase = "Old password: ";
		break;
	case 1:
		phrase = "New password: ";
		break;
	case 2:
		phrase = "Repeat new password: ";
		break;
	default:
		abort();
	}

	/* Read in password. */

	pp = readpassphrase(phrase, pbuf, 
		sizeof(pbuf), RPP_REQUIRE_TTY);

	if (pp == NULL) {
		explicit_bzero(pbuf, sizeof(pbuf));
		errx(1, "unable to read passphrase");
	} else if (pbuf[0] == '\0') {
		explicit_bzero(pbuf, sizeof(pbuf));
		exit(1);
	}

	if (strlen(pbuf) < 6) {
		explicit_bzero(pbuf, sizeof(pbuf));
		errx(1, "come on: more than five letters");
	}

	/* Hash according to RFC 2069's HA1. */

	MD5Init(&ctx);
	MD5Update(&ctx, (const uint8_t *)user, strlen(user));
	MD5Update(&ctx, (const uint8_t *)":", 1);
	MD5Update(&ctx, (const uint8_t *)realm, strlen(realm));
	MD5Update(&ctx, (const uint8_t *)":", 1);
	MD5Update(&ctx, (const uint8_t *)pbuf, strlen(pbuf));
	MD5Final(ha, &ctx);

	explicit_bzero(pbuf, sizeof(pbuf));

	/* Convert to hex format. */

	for (i = 0; i < MD5_DIGEST_LENGTH; i++) 
		snprintf(&digest[i * 2], 3, "%02x", ha[i]);
}

int
main(int argc, char *argv[])
{
	int	 	 c, adduser = 0, rc = 0, passwd = 1;
	char	 	 dnew[MD5_DIGEST_LENGTH * 2 + 1],
			 drep[MD5_DIGEST_LENGTH * 2 + 1],
			 dold[MD5_DIGEST_LENGTH * 2 + 1];
	const char	*realm = KREALM, *altuser = NULL, 
	      		*dir = CALPREFIX, *email = NULL, 
			*coln = NULL, *uid, *cp;
	size_t		 i, sz;
	uid_t		 euid = geteuid();
	gid_t		 egid = getegid();
	struct prncpl	*p = NULL;
	struct coln	*col;
	char		*user = NULL, *emailp = NULL, *res;

#if HAVE_PLEDGE
	if (pledge("stdio rpath cpath wpath flock fattr tty id", NULL) == -1)
		err(1, "pledge");
#endif

#if !HAVE_ARC4RANDOM
	srandom(time(NULL));
#endif

	/* 
	 * Temporarily drop privileges.
	 * We don't want to have them until we're opening the password
	 * file itself.
	 */

	if (setegid(getgid()) == -1) 
		err(1, "setegid");
	else if (seteuid(getuid()) == -1)
		err(1, "seteuid");

	while ((c = getopt(argc, argv, "Cd:e:f:nu:v")) != -1) 
		switch (c) {
		case 'C':
			adduser = 1;
			break;
		case 'd':
			coln = optarg;
			break;
		case 'e':
			email = optarg;
			break;
		case 'f':
			dir = optarg;
			break;
		case 'n':
			passwd = 0;
			break;
		case 'u':
			altuser = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			goto usage;
		}

	argv += optind;
	argc -= optind;

	/* Override -n used with with -C. */

	if (adduser)
		passwd = 1;

	/* Safety: check collection name. */

	if (coln != NULL && !check_safe_string(coln))
		errx(1, "%s: unsafe collection name", coln);

	/* Safety: check resource names. */

	for (i = 0; i < (size_t)argc; i++) {
		if ((uid = strrchr(argv[i], '/')) == NULL)
			uid = argv[i];
		else
			uid++;
		if (!check_safe_string(uid))
			errx(1, "%s: unsafe resource name", argv[i]);
	}

	/* 
	 * Assign our principal name.
	 * This is either going to be given with -u (which is a
	 * privileged operation) or inherited from our login creds.
	 */

	if (altuser == NULL) {
		if ((cp = getlogin()) == NULL)
			err(1, "getlogin");
		user = strdup(cp);
	} else
		user = strdup(altuser);

	if (user == NULL)
		err(1, NULL);
	
	/* Safety: check user name. */

	if (!check_safe_string(user))
		errx(1, "%s: unsafe principal name", user);

	/* 
	 * If we're not creating a new entry and we're setting our own
	 * password, get the existing password. 
	 */

	if (!adduser && altuser == NULL)
		gethash(0, dold, user, realm);

	/* If we're going to set our password, hash it now. */

	if (passwd) {
		gethash(1, dnew, user, realm);
		gethash(2, drep, user, realm);
		if (memcmp(dnew, drep, sizeof(dnew)))
			errx(1, "passwords do not match");
	}

	/* Drop TTY pledge. */

#if HAVE_PLEDGE
	if (pledge("stdio rpath cpath wpath flock fattr id", NULL) == -1)
		err(1, "pledge");
#endif

	/* Regain privileges. */

	if (setgid(egid) == -1)
		err(1, "setgid");
	if (setuid(euid) == -1) 
		err(1, "setuid");

	/*
	 * Open the database file itself.
	 * This is a privileged operation, hence occuring with our
	 * regained effective credentials.
	 * We only try to create the database if "adduser" is set.
	 */

	if (!db_init(dir, adduser))
		errx(1, "failed to open database");

	/* Now drop privileges entirely! */

	if (setgid(getgid()) == -1)
		err(1, "setgid");
	if (setuid(getuid()) == -1)
		err(1, "setuid");

	/* 
	 * Drop id-setting pledge.
	 * The rest of this is for database management and errors.
	 */

#if HAVE_PLEDGE
	if (pledge("stdio rpath cpath wpath flock fattr", NULL) == -1)
		err(1, "pledge");
#endif

	/* 
	 * Check security of privileged operations.
	 * This will also create the database if the database has been
	 * created with "adduser" but doesn't exist yet.
	 */

	if (adduser || altuser != NULL) {
		if ((c = db_owner_check_or_set(getuid())) == 0)
			errx(1, "db owner does not match real user");
		else if (c < 0)
			errx(1, "failed check or set db owner");
	}

	/* Now either create or update the principal. */

	if (adduser) {
		/*
		 * Create e-mail by having our user followed by @
		 * followed by the system hostname.
		 * XXX: check if 256 for hostname is insufficient.
		 */

		if (email == NULL) {
			sz = strlen(user) + 256 /* max hostname */ + 2;
			if ((emailp = malloc(sz)) == NULL)
				err(1, NULL);
			strlcpy(emailp, user, sz);
			strlcat(emailp, "@", sz);
			gethostname
				(emailp + strlen(user) + 1, 
				 sz - strlen(user) - 1);
			email = emailp;
		}

		c = db_prncpl_new(user, dnew, email, 
			coln == NULL ? "calendar" : coln);
		if (c == 0)
			errx(1, "%s: principal already exists", user);
		else if (c < 0)
			errx(1, "failed to create principal");

		printf("principal created: %s\n", user);
	} else {
		if ((c = db_prncpl_load(&p, user)) == 0)
			errx(1, "%s: principal does not exist", user);
		else if (c < 0)
			errx(1, "failed to load principal");

		/* 
		 * If we have a new collection, create it.
		 * Don't report if it already exists.
		 */

		if (coln != NULL) {
			if ((c = db_collection_new(coln, p)) < 0)
				errx(1, "failed to create collection");
			else if (c > 0)
				printf("collection added: %s\n", coln);
		}

		/* Does the hash match what we gave? */

		if (altuser == NULL)
			if (memcmp(p->hash, dold, sizeof(dold)))
				errx(1, "password mismatch");

		/* Set new e-mail, if applicable. */

		if (email != NULL) {
			free(p->email);
			if ((p->email = strdup(email)) == NULL)
				err(1, NULL);
		}

		/* Set new password (hash), if applicable. */

		if (passwd) {
			free(p->hash);
			if ((p->hash = strdup(dnew)) == NULL)
				err(1, NULL);
		}

		if ((c = db_prncpl_update(p)) == 0)
			errx(1, "%s: e-mail already exists", p->email);
		else if (c < 0)
			errx(1, "failed to update principal");

		printf("principal updated: %s\n", user);
	}

	/* If we have no resources on the command line, exit. */

	if (argc == 0)
		goto out;

	/*
	 * We need to load some resources.
	 * First re-load our principal, which will have its new
	 * directories contained therein.
	 */

	prncpl_free(p);
	p = NULL;

	if ((c = db_prncpl_load(&p, user)) == 0)
		errx(1, "%s: principal disappeared!?", user);
	else if (c < 0)
		errx(1, "failed to re-load principal");

	/* 
	 * Search for the new collection, defaulting to the standard
	 * calendar name if we didn't provide on.
	 */

	if (coln == NULL)
		coln = "calendar";

	for (col = NULL, i = 0; i < p->colsz; i++) 
		if (strcmp(p->cols[i].url, coln) == 0) {
			col = &p->cols[i];
			break;
		}

	if (col == NULL)
		errx(1, "%s: collection disappeared!?", coln);

	/*
	 * Now go through each file on the command line, load it, then
	 * push it into the collection.
	 */

	for (i = 0; i < (size_t)argc; i++) {
		res = read_whole_file(argv[i]);
		assert(res != NULL);

		/* Get file-name component. */

		if ((uid = strrchr(argv[i], '/')) == NULL)
			uid = argv[i];
		else
			uid++;

		rc = db_resource_new(res, uid, col->id);
		free(res);
		if (rc == 0)
			errx(1, "%s: resource exists", argv[i]);
		else if (rc < 0)
			errx(1, "failed to create resource");

		printf("resource added: %s\n", argv[i]);
	}

out:
	prncpl_free(p);
	free(user);
	free(emailp);
	return 0;
usage:
	fprintf(stderr, "usage: %s "
		"[-Cnv] "
		"[-d collection] "
		"[-e email] "
		"[-f caldir] "
		"[-u principal] [resource...]\n", 
		getprogname());
	return 1;
}
