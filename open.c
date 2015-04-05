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
#include <sys/stat.h>

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
 * Use fstat(2) to test whether the file is a symlink.
 * The descriptor must have been opened with O_SYMLINK.
 */
static int
open_test_symlink(const char *file, int fd)
{
	struct stat	 st;
	int		 er;

	if (-1 == fstat(fd, &st)) {
		er = errno;
		kerr("%s: fstat", file);
		close_unlock(file, fd);
		errno = er;
		return(-1);
	} else if (S_ISLNK(st.st_mode)) {
		kerrx("%s: symbolic link", file);
		close_unlock(file, fd);
		errno = EPERM;
		return(-1);
	}
	return(fd);
}

/*
 * Wrapper around open(2) with O_EXLOCK for systems (like Linux) that
 * don't have O_EXLOCK support.
 * This also adds O_SYMLINK and verifies that the file isn't a link.
 * Return the file descriptor (or -1 on failure).
 */
int
open_lock_ex(const char *file, int flags, mode_t mode)
{
	int	 fd, er; /* preserve the errno */

	/* Mac OSX has different semantics for O_NOFOLLOW. */
#ifdef	__APPLE__
	flags |= O_SYMLINK;
#else
	flags |= O_NOFOLLOW;
#endif

#ifdef	__linux__
	if (-1 != (fd = flopen(file, flags, mode))) 
		return(open_test_symlink(file, fd));
#else
	if (-1 != (fd = open(file, flags | O_EXLOCK, mode))) 
		return(open_test_symlink(file, fd));
#endif
	er = errno;
	kerr("%s: open exclusive", file);
	errno = er;
	return(-1);
}

/*
 * Wrapper around open(2) with O_SHLOCK for systems (like Linux) that
 * don't have O_SHLOCK support.
 * This also adds O_SYMLINK and verifies that the file isn't a link.
 * Return the file descriptor (or -1 on failure).
 */
int
open_lock_sh(const char *file, int flags, mode_t mode)
{
	int	 fd, er; /* preserve the errno */

	/* Mac OSX has different semantics for O_NOFOLLOW. */
#ifdef	__APPLE__
	flags |= O_SYMLINK;
#else
	flags |= O_NOFOLLOW;
#endif

#ifndef	HAVE_OPEN_LOCK
	if (-1 == (fd = open(file, flags, mode))) {
		er = errno;
		kerr("%s: open", file);
		errno = er;
		return(fd);
	} else if (-1 != flock(fd, LOCK_SH))
		return(open_test_symlink(file, fd));

	er = errno;
	kerr("%s: flock shared", file);
	if (-1 == close(fd)) 
		kerr("%s: close", file);
	errno = er;
	return(-1);
#else
	if (-1 != (fd = open(file, flags | O_SHLOCK, mode))) 
		return(open_test_symlink(file, fd));
	er = errno;
	kerr("%s: open shared", file);
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
	
	if (-1 == fd)
		return(1);

	if (-1 == flock(fd, LOCK_UN)) {
		er = errno;
		kerr("%s: flock unlock", file);
		errno = er;
		if (-1 == close(fd))
			kerr("%s: close", file);
		errno = er;
		return(-1);
	} else if (-1 == close(fd)) {
		er = errno;
		kerr("%s: close", file);
		errno = er;
		return(-1);
	}

	return(1);
}

/*
 * Re-opens a file opened earlier with open_lock_ex() or open_lock_sh()
 * as a FILE stream, duplicating the file descriptor so that a
 * subsequent fclose() won't close the original file descriptor.
 * Returns the stream or NULL on failure.
 */
FILE *
fdopen_lock(const char *file, int fd, const char *mode)
{
	int	 newfd;
	FILE	*f;

	if (-1 == (newfd = dup(fd))) {
		kerr("%s: dup", file);
		close_unlock(file, fd);
		return(NULL);
	} else if (NULL == (f = fdopen(newfd, mode))) {
		kerr("%s: fdopen", file);
		close(newfd);
		close_unlock(file, fd);
		return(NULL);
	}
	return(f);
}

/*
 * Close a file stream opened with fdopen_lock().
 * This will also close the underlying file descriptor as created with
 * open_lock_ex() or open_lock_sh(), so if you don't want to do that,
 * just call fclose() yourself.
 * Returns -1 on failure and 1 on success.
 */
int
fclose_unlock(const char *file, FILE *f, int fd)
{

	if (-1 == fclose(f)) {
		kerr("%s: fclose", file);
		close_unlock(file, fd);
		return(-1);
	} 

	return(close_unlock(file, fd));
}
