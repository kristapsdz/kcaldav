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
#include <limits.h>
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

static struct ical *
req2ical(struct kreq *r)
{
	struct ical	*p;
	const char	*cp;
	size_t		 sz;
	char		*buf;

	if (0 == r->fieldsz) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	} 

	cp = r->fields[0].val;
	sz = r->fields[0].valsz;
	p = NULL;
	buf = kmalloc(sz + 1);
	memcpy(buf, cp, sz);
	buf[sz] = '\0';

	fprintf(stderr, "CHecking: %s\n", buf);

	p = ical_parse(buf);
	free(buf);
	if (NULL != p) {
		fprintf(stderr, "It's good!\n");
		return(p);
	}
	fprintf(stderr, "It's bad!\n");

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_400]);
	khttp_body(r);
	ical_free(p);
	return(NULL);
}

static struct caldav *
req2caldav(struct kreq *r, enum type type)
{
	struct caldav	*p;

	if (0 == r->fieldsz) {
		khttp_head(r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_400]);
		khttp_body(r);
		return(NULL);
	} 

	p = caldav_parse(r->fields[0].val, r->fields[0].valsz);
	if (NULL != p && type == p->type)
		return(p);

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_400]);
	khttp_body(r);
	caldav_free(p);
	return(NULL);
}

static void
put(struct kreq *r)
{
	struct config	*cfg = r->arg;
	struct ical	*p;
	char		 file[PATH_MAX];
	enum khttp	 code;
	int		 rc;

	rc = snprintf(file, sizeof(file), 
		"%s%s", cfg->base, '/' == r->fullpath[0] ?
		r->fullpath + 1 : r->fullpath);

	if (rc >= (int)sizeof(file)) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_400]);
		khttp_body(r);
		return;
	} else if (NULL == (p = req2ical(r)))
		return;

	code = ical_mergefile(file, p) ? KHTTP_201 : KHTTP_403;
	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[code]);
	khttp_body(r);
	ical_free(p);
}


static void
get(struct kreq *r)
{
	struct config	*cfg = r->arg;
	char		 file[PATH_MAX], buf[BUFSIZ];
	int		 rc, fd;
	ssize_t		 ssz;

	rc = snprintf(file, sizeof(file), "%s%s", cfg->base, 
		'/' == r->fullpath[0] ? r->fullpath + 1 : r->fullpath);

	if (rc >= (int)sizeof(file)) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_403]);
		khttp_body(r);
		return;
	} else if (-1 == (fd = open(file, O_RDONLY, 0))) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_200]);
		khttp_body(r);
		return;
	}

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_200]);
	khttp_body(r);

	do
		if ((ssz = read(fd, buf, sizeof(buf))) > 0) 
			write(STDOUT_FILENO, buf, ssz);
	while (ssz > 0);

	close(fd);
}

static void
report(struct kreq *r)
{
	struct caldav	*p;
	struct config	*cfg = r->arg;
	int		 fd;
	char		*fname;

	if (NULL == (p = req2caldav(r, TYPE_CALQUERY)))
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
	size_t		 i;
	const char	*cp, *tag;

	if (NULL == (p = req2caldav(r, TYPE_PROPFIND)))
		return;

	for (i = 0; i < p->propsz; i++) {
		tag = calelems[calpropelems[p->props[i].key]];
		fprintf(stderr, "%s: propfind: %s\n", r->fullpath, tag);
	}

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_207]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], "%s", kmimetypes[KMIME_TEXT_XML]);
	khttp_body(r);
	puts("<?xml version=\"1.0\" encoding=\"utf-8\" ?>");
	puts("<D:multistatus xmlns:D=\"DAV:\">");
	puts("<D:response>");
	printf("<D:href>http://%s%s%s</D:href>\n", 
		r->host, r->pname, r->fullpath);
	puts("<D:propstat>");
	puts("<D:prop>");
	for (i = 0; i < p->propsz; i++) {
		tag = calelems[calpropelems[p->props[i].key]];
		cp = prop_default(p->props[i].key);
		if ('\0' == *cp) 
			printf("<D:%s />\n", tag);
		else
			printf("<D:%s>%s</D:%s>\n", tag, cp, tag);
	}
	puts("</D:prop>");
	puts("<D:status>HTTP/1.1 200 OK</D:status>");
	puts("</D:propstat>");
	puts("</D:response>");
	puts("</D:multistatus>");
	caldav_free(p);
}

static void
mkcalendar(struct kreq *r)
{
	struct config	*cfg = r->arg;
	struct caldav	*p;
	char		 dir[PATH_MAX], path[PATH_MAX];
	FILE		*f;
	int		 rc;
	size_t		 i;

	dir[0] = path[0] = '\0';

	if (NULL == (p = req2caldav(r, TYPE_MKCALENDAR)))
		return;

	rc = snprintf(dir, sizeof(dir), "%s%s", cfg->base, 
		'/' == r->fullpath[0] ? r->fullpath + 1 : r->fullpath);

	if (rc >= (int)sizeof(dir) || -1 == mkdir(dir, 0755)) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_403]);
		khttp_body(r);
		caldav_free(p);
		return;
	}

	rc = snprintf(path, sizeof(path), "%s%sproperties.dav", 
		dir, '/' == dir[strlen(dir) - 1] ? "" : "/");

	if (rc >= (int)sizeof(path)) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_403]);
		khttp_body(r);
		caldav_free(p);
		return;
	}

	if (NULL == (f = fopen(dir, "r+"))) {
		khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_403]);
		khttp_body(r);
		caldav_free(p);
		return;
	}

	fputs("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n", f);
	fputs("<proplist>\n", f);
	fputs(" <d:prop>\n", f);
	for (i = 0; i < p->propsz; i++) {
		fprintf(f, " <d:%s>%s</d:%s>\n",
			calelems[calpropelems[p->props[i].key]],
			p->props[i].value,
			calelems[calpropelems[p->props[i].key]]);
	}
	fputs(" </d:prop>\n", f);
	fputs("</proplist>\n", f);
	fclose(f);

	khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_201]);
	khttp_body(r);
	caldav_free(p);
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

	fprintf(stderr, "Here we go...\n");

	if (KCGI_OK != khttp_parse(&r, NULL, 0, NULL, 0, 0))
		return(EXIT_FAILURE);

	cfg.base = kstrdup(_PATH_TMP);
	assert(cfg.base[strlen(cfg.base) - 1] == '/');

	r.arg = &cfg;

	switch (r.method) {
	case (KMETHOD_OPTIONS):
		fprintf(stderr, "KMETHOD_OPTIONS\n");
		options(&r);
		break;
	case (KMETHOD_MKCALENDAR):
		fprintf(stderr, "KMETHOD_MKCALENDAR\n");
		mkcalendar(&r);
		break;
	case (KMETHOD_PUT):
		fprintf(stderr, "KMETHOD_PUT\n");
		put(&r);
		break;
	case (KMETHOD_PROPFIND):
		fprintf(stderr, "KMETHOD_PROPFIND\n");
		propfind(&r);
		break;
	case (KMETHOD_REPORT):
		fprintf(stderr, "KMETHOD_REPORT\n");
		report(&r);
		break;
	case (KMETHOD_GET):
		fprintf(stderr, "KMETHOD_GET\n");
		get(&r);
		break;
	default:
		fprintf(stderr, "Unknown method\n");
		khttp_head(&r, kresps[KRESP_STATUS], 
			"%s", khttps[KHTTP_405]);
		khttp_body(&r);
		break;
	}

	free(cfg.base);
	khttp_free(&r);
	return(EXIT_SUCCESS);
}
