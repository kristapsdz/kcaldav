#include "config.h"

#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif

#include "extern.h"
#include "md5.h"

int
main(int argc, char *argv[])
{
	int	 	 c, fd;
	char	 	 buf[PATH_MAX];
	const char	*fname, *pname;
	size_t		 sz;

	if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;

	sz = strlcpy(buf, CALDIR, sizeof(buf));
	if (sz >= sizeof(buf)) {
		fprintf(stderr, "%s: name too long\n", buf);
		return(EXIT_FAILURE);
	} else if ('/' == buf[sz - 1])
		buf[sz - 1] = '\0';

	sz = strlcat(buf, "/kcaldav.passwd", sizeof(buf));
	if (sz >= sizeof(buf)) {
		fprintf(stderr, "%s: name too long\n", buf);
		return(EXIT_FAILURE);
	}

	fname = buf;

	while (-1 != (c = getopt(argc, argv, "f:"))) 
		switch (c) {
		case ('f'):
			fname = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;
	if (0 == argc) 
		goto usage;

	fd = open_lock_sh(fname, O_RDONLY, 0600);
	if (-1 == fd) 
		return(EXIT_FAILURE);

	close_unlock(fname, fd);
	return(EXIT_SUCCESS);

usage:
	fprintf(stderr, "usage: %s [-f passwd] user\n", pname);
	return(EXIT_FAILURE);
}
