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
#include <sys/stat.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int
main(int argc, char *argv[])
{
	int		 fd, c;
	struct stat	 st;
	size_t		 sz;
	char		*map, *buf;
	struct httpauth	*p = NULL;
	struct prncpl	*prncpl = NULL;

	if (-1 != (c = getopt(argc, argv, "")))
		return(EXIT_FAILURE);

	argc -= optind;
	argv += optind;

	if (2 != argc)
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
	} 

	buf = malloc(sz + 1);
	memcpy(buf, map, sz);
	buf[sz] = '\0';
	munmap(map, sz);
	
	if (NULL != (p = httpauth_parse(buf)) && NULL != p->user) {
		printf("username: %s\n",
			NULL == p->user ? "(none)" : p->user);
		printf("realm: %s\n",
			NULL == p->realm ? "(none)" : p->realm);
		printf("nonce: %s\n",
			NULL == p->nonce ? "(none)" : p->nonce);
		printf("response: %s\n",
			NULL == p->response ? "(none)" : p->response);
		printf("uri: %s\n",
			NULL == p->uri ? "(none)" : p->uri);
		c = prncpl_parse(argv[1], "GET", p, &prncpl);
		if (c > 0 && NULL != prncpl)
			printf("principal: %s\n", prncpl->name);
	}

	httpauth_free(p);
	prncpl_free(prncpl);
	free(buf);
	return(NULL == p ? EXIT_FAILURE : EXIT_SUCCESS);
}
