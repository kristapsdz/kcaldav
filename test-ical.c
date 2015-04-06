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

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int verbose = 1;

int
main(int argc, char *argv[])
{
	int		 fd, c;
	struct stat	 st;
	size_t		 sz;
	char		*map;
	struct ical	*p = NULL;

	if (-1 != (c = getopt(argc, argv, "")))
		return(EXIT_FAILURE);

	argc -= optind;
	argv += optind;

	if (0 == argc)
		return(EXIT_FAILURE);

	if (-1 == (fd = open(argv[0], O_RDONLY, 0))) {
		perror(argv[0]);
		return(EXIT_FAILURE);
	} else if (-1 == fstat(fd, &st)) {
		perror(argv[0]);
		close(fd);
		return(EXIT_FAILURE);
	} 

	sz = st.st_size;
	map = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (MAP_FAILED == map) {
		perror(argv[0]);
		return(EXIT_FAILURE);
	} else if (NULL != (p = ical_parse(argv[0], map, sz)))
		ical_printfile(STDOUT_FILENO, p);

	munmap(map, sz);
	ical_free(p);
	return(NULL == p ? EXIT_FAILURE : EXIT_SUCCESS);
}
