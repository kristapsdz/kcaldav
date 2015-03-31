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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"
#include "main.h"

/*
 * Update the ctag in "fname".
 * This consists of writing 32 random bytes to the file.
 * Returns 1 on success, 0 on failure.
 */
int
ctagcache_update(const char *fname)
{
	int	 fd, er;
	char	 buf[32];
	ssize_t	 ssz;

	if (-1 == (fd = open(fname, O_RDWR | O_CREAT, 0600))) {
		perror(fname);
		return(0);
	} else if (-1 == flock(fd, LOCK_NB | LOCK_EX)) {
		if (EWOULDBLOCK != (er = errno))
			perror(fname);
		close(fd);
		return(er == EWOULDBLOCK);
	}

	arc4random_buf(buf, 32);
	if (-1 == (ssz = write(fd, buf, 32))) {
		perror(fname);
		close(fd);
		return(0);
	} else if (ssz != 32) {
		fprintf(stderr, "%s: short write\n", fname);
		close(fd);
		return(0);
	}

	close(fd);
	return(1);
}

/*
 * Extract the ctag from the cache in "fname" and format it as hex
 * values into "str", which MUST be at least 65 bytes (64 bytes of ctag
 * and the nil terminator).
 * If no cache exists, or if errors occur, this will always return the
 * empty string.
 */
void
ctagcache_get(const char *fname, char *str)
{
	int	 fd, er;
	char	 buf[32];
	ssize_t	 ssz;
	size_t	 i;

	str[0] = '\0';

	if (-1 == (fd = open(fname, O_RDONLY | O_SHLOCK))) {
		if (ENOENT != (er = errno)) 
			perror(fname);
		return;
	}

	if (-1 == (ssz = read(fd, buf, 32))) {
		perror(fname);
		close(fd);
		return;
	} else if (ssz != 32) {
		fprintf(stderr, "%s: short read\n", fname);
		close(fd);
		return;
	}

	close(fd);
	for (i = 0; i < 32; i++)
		snprintf(&str[i * 2], 3, "%.2X", buf[i]);
}
