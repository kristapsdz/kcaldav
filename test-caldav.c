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

#include <sys/stat.h>
#include <sys/mman.h>

#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "libkcaldav.h"

int
main(int argc, char *argv[])
{
	int		 fd, c;
	struct stat	 st;
	size_t		 sz;
	char		*map, *er = NULL;
	struct caldav	*p;

#if HAVE_PLEDGE
	if (pledge("stdio rpath", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
#endif

	if ((c = getopt(argc, argv, "")) != -1)
		return EXIT_FAILURE;

	argc -= optind;
	argv += optind;

	if (argc == 0)
		return EXIT_FAILURE;

	if ((fd = open(argv[0], O_RDONLY, 0)) == -1)
		err(EXIT_FAILURE, "%s", argv[0]);

#if HAVE_PLEDGE
	if (pledge("stdio", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
#endif

	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, "%s", argv[0]);

	sz = st.st_size;
	map = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (map == MAP_FAILED)
		err(EXIT_FAILURE, "%s", argv[0]);

	if ((p = caldav_parse(map, sz, &er)) == NULL)
		warnx("%s", er == NULL ? "memory failure" : er);

	munmap(map, sz);
	caldav_free(p);
	free(er);

	return p == NULL ? EXIT_FAILURE : EXIT_SUCCESS;
}
