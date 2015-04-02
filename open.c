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

#ifdef __linux__
#include <sys/file.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <bsd/libutil.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

/*
 * Wrapper around open(2) with O_EXLOCK for systems (like Linux) that
 * don't have O_EXLOCK support.
 * Return the file descriptor (or -1 on failure).
 */
int
open_lock_ex(const char *file, int flags, mode_t mode)
{
	int	 fd, er; /* preserve the errno */

#ifdef	__linux__
	if (-1 != (fd = flopen(file, flags, mode))) 
		return(fd);
#else
	if (-1 != (fd = open(file, flags | O_EXLOCK, mode))) 
		return(fd);
#endif
	er = errno;
	fprintf(stderr, "%s: open(O_EXLOCK): "
		"%s\n", file, strerror(er));
	errno = er;
	return(-1);
}

/*
 * Wrapper around open(2) with O_SHLOCK for systems (like Linux) that
 * don't have O_SHLOCK support.
 * Return the file descriptor (or -1 on failure).
 */
int
open_lock_sh(const char *file, int flags, mode_t mode)
{
	int	 fd, er; /* preserve the errno */

#ifndef	HAVE_OPEN_LOCK
	if (-1 == (fd = open(file, flags, mode))) {
		er = errno;
		fprintf(stderr, "%s: open: "
			"%s\n", file, strerror(er));
		errno = er;
		return(fd);
	} else if (-1 != flock(fd, LOCK_EX))
		return(fd);

	er = errno;
	fprintf(stderr, "%s: flock(LOCK_EX): "
		"%s\n", file, strerror(er));
	if (-1 == close(fd)) 
		fprintf(stderr, "%s: close: "
			"%s\n", file, strerror(errno));
	errno = er;
	return(-1);
#else
	if (-1 != (fd = open(file, flags | O_EXLOCK, mode))) 
		return(fd);
	er = errno;
	fprintf(stderr, "%s: open(O_EXLOCK): "
		"%s\n", file, strerror(er));
	errno = er;
	return(-1);
#endif
}

/*
 * Close a file opened with open_lock_ex() or open_lock_sh().
 * Return -1 on failure, 1 on success.
 */
int
close_unlock(const char *file, int fd)
{
	int	 er;

	if (-1 == flock(fd, LOCK_UN)) {
		er = errno;
		fprintf(stderr, "%s: flock(LOCK_UN): "
			"%s\n", file, strerror(er));
		errno = er;
		if (-1 != close(fd))
			return(-1);
		fprintf(stderr, "%s: close: %s\n", 
			file, strerror(errno));
		errno = er;
		return(-1);
	} else if (-1 == close(fd)) {
		er = errno;
		fprintf(stderr, "%s: close: %s\n", 
			file, strerror(er));
		errno = er;
		return(-1);
	}

	return(1);
}
