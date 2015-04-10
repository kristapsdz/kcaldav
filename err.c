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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef LOGTIMESTAMP
#include <time.h>
#endif

#include "extern.h"

void
kvdbg(const char *file, size_t line, const char *fmt, ...)
{
	va_list	 ap;

	if ( ! verbose)
		return;

	fprintf(stderr, "%s:%zu: DEBUG: ", file, line);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void
kverrx(const char *file, size_t line, const char *fmt, ...)
{
	va_list	 ap;
	char	 buf[32];
#ifdef LOGTIMESTAMP
	time_t	 t = time(NULL);

	strftime(buf, sizeof(buf), "[%F %R]:", localtime(&t));
#else
	buf[0] = '\0';
#endif

	fprintf(stderr, "%s%s:%zu: ", buf, file, line);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void
kverr(const char *file, size_t line, const char *fmt, ...)
{
	int	 er = errno;
	va_list	 ap;
	char	 buf[32];
#ifdef LOGTIMESTAMP
	time_t	 t = time(NULL);

	strftime(buf, sizeof(buf), "[%F %R]:", localtime(&t));
#else
	buf[0] = '\0';
#endif

	fprintf(stderr, "%s%s:%zu: ", buf, file, line);
	if (NULL != fmt) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	fprintf(stderr, ": %s\n", strerror(er));
}
