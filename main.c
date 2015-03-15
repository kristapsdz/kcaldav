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
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>

#include "extern.h"

struct	config {
	char	*base;
};


/*	$OpenBSD$ */
/*
 * Copyright (c) 1983, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* Code taken directly from mkdir(1).

 * mkpath -- create directories.
 *	path     - path
 */
static int
mkpath(char *path)
{
	struct stat sb;
	char *slash;
	int done = 0;

	slash = path;

	while (!done) {
		slash += strspn(slash, "/");
		slash += strcspn(slash, "/");

		done = (*slash == '\0');
		*slash = '\0';

		if (stat(path, &sb)) {
			if (errno != ENOENT || (mkdir(path, 0777) &&
			    errno != EEXIST)) {
				return (-1);
			}
		} else if (!S_ISDIR(sb.st_mode)) {
			return (-1);
		}

		*slash = '/';
	}

	return (0);
}

static void
put(struct kreq *r)
{
	struct config	*cfg = r->arg;
	char		*fname;
	int		 fd;
	struct ical	*ical;
	enum khttp	 code;

	if (0 == r->fieldsz) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return;
	} else if (NULL == (ical = ical_parse(r->fields[0].val))) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return;
	}

	kasprintf(&fname, "%s%s", cfg->base, r->fullpath);
	fd = open(fname, O_RDWR | O_CREAT | O_APPEND, 0666);
	if (-1 != fd) {
		if (write(fd, ical->data, strlen(ical->data)) < 0)
			code = KHTTP_403;
		else
			code = KHTTP_201;
	} else
		code = KHTTP_403;

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[code]);
	khttp_body(r);
	fprintf(stderr, "%s: Put file\n", fname);
	free(fname);
	ical_free(ical);
	close(fd);
}

static struct caldav *
caldav_get(struct kreq *r, enum type type)
{
	struct caldav	*p;

	if (0 == r->fieldsz) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	} 

	p = caldav_parse(r->fields[0].val, r->fields[0].valsz);
	if (NULL == p) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		caldav_free(p);
		return(NULL);
	} else if (type != p->type) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		caldav_free(p);
		return(NULL);
	}

	return(p);
}

static void
report(struct kreq *r)
{
	struct caldav	*p;
	struct config	*cfg = r->arg;
	int		 fd;
	char		*fname;

	if (NULL == (p = caldav_get(r, TYPE_CALQUERY)))
		return;

	kasprintf(&fname, "%s%s", cfg->base, r->fullpath);
	fd = open(fname, O_RDWR | O_CREAT | O_APPEND, 0666);
	if (-1 == fd) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_404]);
		khttp_body(r);
	}

	fprintf(stderr, "%s: report\n", r->fullpath);

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_200]);
	khttp_body(r);
}

static void
propfind(struct kreq *r)
{
	struct caldav	*p;

	if (NULL == (p = caldav_get(r, TYPE_PROPFIND)))
		return;

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_207]);
	khttp_body(r);

	puts("<d:multistatus xmlns:d=\"DAV:\" "
		"xmlns:cs=\"http://calendarserver.org/ns/\">");
	puts(" <d:response>");
	printf(" <d:href>/%s</d:href>\n", r->fullpath);
	puts(" <d:propstat>");
	puts(" </d:propstat>");
	puts("</d:response>");
	puts("</d:multistatus>");

	fprintf(stderr, "%s: propfind\n", r->fullpath);
	caldav_free(p);
}

static void
mkcalendar(struct kreq *r)
{
	struct config	*cfg = r->arg;
	char		*dir, *path;
	struct caldav	*p;
	FILE		*f;
	size_t		 i;

	if (NULL == (p = caldav_get(r, TYPE_MKCALENDAR)))
		return;

	dir = path = NULL;

	kasprintf(&dir, "%s%s", cfg->base, r->fullpath);
	fprintf(stderr, "mkdir(%s)\n", dir);

	if (-1 == mkpath(dir)) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_403]);
		khttp_body(r);
		goto out;
	}

	kasprintf(&path, "%sproperties.dav", dir, r->fullpath);
	if (NULL == (f = fopen(dir, "r+"))) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_403]);
		khttp_body(r);
		goto out;
	}
	fprintf(stderr, "fopen(%s)\n", path);

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_201]);
	khttp_body(r);

	fputs("<d:prop xmlns:d=\"DAV:\" "
		"xmlns:cs=\"http://calendarserver.org/ns/\">\n", f);
	for (i = 0; i < p->d.mkcal.propsz; i++) {
		fprintf(f, " <d:%s>%s</d:%s>\n",
			calelems[calprops[p->d.mkcal.props[i].key]],
			p->d.mkcal.props[i].value,
			calelems[calprops[p->d.mkcal.props[i].key]]);
	}
	fputs("</d:prop>\n", f);
	fclose(f);
out:
	caldav_free(p);
	free(dir);
	free(path);
}

static void
options(struct kreq *r)
{

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_ALLOW], "%s", 
		"OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, "
		"COPY, MOVE, PROPFIND, PROPPATCH, LOCK, UNLOCK, "
		"REPORT, ACL");
	khttp_body(r);
}

int
main(void)
{
	struct kreq	 r;
	struct config	 cfg;

	if (KCGI_OK != khttp_parse(&r, NULL, 0, NULL, 0, 0))
		return(EXIT_FAILURE);

	cfg.base = kstrdup(_PATH_TMP);

	r.arg = &cfg;

	switch (r.method) {
	case (KMETHOD_OPTIONS):
		options(&r);
		break;
	case (KMETHOD_MKCALENDAR):
		mkcalendar(&r);
		break;
	case (KMETHOD_PUT):
		put(&r);
		break;
	case (KMETHOD_PROPFIND):
		propfind(&r);
		break;
	case (KMETHOD_REPORT):
		report(&r);
		break;
	default:
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_405]);
		khttp_body(&r);
		break;
	}

	free(cfg.base);
	khttp_free(&r);
	return(EXIT_SUCCESS);
}
