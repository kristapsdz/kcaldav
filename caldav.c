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
#include <sys/mman.h>

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <expat.h>

#include "extern.h"

struct	xmlns {
	char		*prefix;
	char		*urn;
};

struct	parse {
	struct caldav	 *p;
	XML_Parser	  xp;
	struct buf	  buf;
	struct xmlns	 *xmlns;
	size_t		  xmlnsz;
};

static	const enum proptype __calprops[CALELEM__MAX] = {
	PROP_CALENDAR_HOME_SET, /* CALELEM_CALENDAR_HOME_SET */
	PROP__MAX, /* CALELEM_CALENDAR_MULTIGET */
	PROP__MAX, /* CALELEM_CALENDAR_QUERY */
	PROP_CALENDAR_USER_ADDRESS_SET, /* CALELEM_CALENDAR_USER_ADDRESS_SET */
	PROP_CURRENT_USER_PRINCIPAL, /* CALELEM_CURRENT_USER_PRINCIPAL */
	PROP_DISPLAYNAME, /* CALELEM_DISPLAYNAME */
	PROP_EMAIL_ADDRESS_SET, /* CALELEM_EMAIL_ADDRESS_SET */
	PROP_GETETAG, /* CALELEM_GETETAG */
	PROP__MAX, /* CALELEM_HREF */
	PROP__MAX, /* CALELEM_MKCALENDAR */
	PROP_PRINCIPAL_URL, /* CALELEM_PRINCIPAL_URL */
	PROP__MAX, /* CALELEM_PROP */
	PROP__MAX, /* CALELEM_PROPFIND */
	PROP_RESOURCETYPE, /* CALELEM_RESOURCETYPE */
};

static	const enum calelem __calpropelems[PROP__MAX] = {
	CALELEM_CALENDAR_HOME_SET, /* PROP_CALENDAR_HOME_SET */
	CALELEM_CALENDAR_USER_ADDRESS_SET, /* PROP_CALENDAR_USER_ADDRESS_SET */
	CALELEM_CURRENT_USER_PRINCIPAL, /* PROP_CURRENT_USER_PRINCIPAL */
	CALELEM_DISPLAYNAME, /* PROP_DISPLAYNAME */
	CALELEM_EMAIL_ADDRESS_SET, /* PROP_EMAIL_ADDRESS_SET */
	CALELEM_GETETAG, /* PROP_GETETAG */
	CALELEM_PRINCIPAL_URL,/* PROP_PRINCIPAL_URL */
	CALELEM_RESOURCETYPE,/* PROP_RESOURCETYPE */
};

const char *const __calelems[CALELEM__MAX] = {
	"calendar-home-set", /* CALELEM_CALENDAR_HOME_SET */
	"calendar-multiget", /* CALELEM_CALENDAR_MULTIGET */
	"calendar-query", /* CALELEM_CALENDAR_QUERY */
	"calendar-user-address-set", /* CALELEM_CALENDAR_USER_ADDRESS_SET */
	"current-user-principal", /* CALELEM_CURRENT_USER_PRINCIPAL */
	"displayname", /* CALELEM_DISPLAYNAME */
	"email-address-set", /* CALELEM_EMAIL_ADDRESSS_SET */
	"getetag", /* CALELEM_GETETAG */
	"href", /* CALELEM_HREF */
	"mkcalendar", /* CALELEM_MKCALENDAR */
	"principal-URL", /* CALELEM_PRINCIPAL_URL */
	"prop", /* CALELEM_PROP */
	"propfind", /* CALELEM_PROPFIND */
	"resourcetype", /* CALELEM_RESOURCETYPE */
};

const char *const *calelems = __calelems;
const enum proptype *calprops = __calprops;
const enum calelem *calpropelems = __calpropelems;

static void	parseclose(void *, const XML_Char *);
static void	propclose(void *, const XML_Char *);
static void	propopen(void *, const XML_Char *, const XML_Char **);
static void	parseopen(void *, const XML_Char *, const XML_Char **);

static enum calelem
calelem_find(const XML_Char *name)
{
	enum calelem	 i;

	for (i = 0; i < CALELEM__MAX; i++)
		if (0 == strcmp(calelems[i], name))
			break;

	return(i);
}

static void
caldav_err(struct parse *p, const char *fmt, ...)
{
	va_list	 ap;

	fprintf(stderr, "%zu:%zu: ", 
		XML_GetCurrentLineNumber(p->xp),
		XML_GetCurrentColumnNumber(p->xp));

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fputc('\n', stderr);
	XML_StopParser(p->xp, 0);
}

static void
propadd(struct parse *p, size_t ns, 
	const char *name, enum proptype prop, const char *cp)
{

	if (NULL == p->p) {
		caldav_err(p, "property list in unknown request");
		return;
	}

	assert(ns < p->xmlnsz);
	p->p->props = realloc(p->p->props,
		sizeof(struct prop) * (p->p->propsz + 1));

	if (NULL == p->p->props) {
		caldav_err(p, "memory exhausted");
		return;
	}

	p->p->props[p->p->propsz].key = prop;

	p->p->props[p->p->propsz].name = strdup(name);
	if (NULL == p->p->props[p->p->propsz].name) {
		caldav_err(p, "memory exhausted");
		return;
	}
	p->p->props[p->p->propsz].xmlns = strdup(p->xmlns[ns].urn);
	if (NULL == p->p->props[p->p->propsz].xmlns) {
		caldav_err(p, "memory exhausted");
		return;
	}
	p->p->propsz++;
}

static void
prop_free(struct prop *p)
{

	free(p->xmlns);
	free(p->name);
}

/*
 * Look up the XML namespace and adjust "sp" to point to the beginning
 * of the element name after the namespace.
 */
static int
xmlnsfind(struct parse *p, size_t *ns, const XML_Char **sp)
{
	const XML_Char	*cp, *nss;
	size_t		 sz;

	/* If we're in the default namespace, use empty string. */
	nss = *sp;
	if (NULL != (cp = strchr(*sp, ':'))) {
		sz = cp - *sp;
		*sp = cp + 1;
	} else
		sz = 0;

	/* Look up the namespace in our table... */
	for (*ns = 0; *ns < p->xmlnsz; (*ns)++) 
		if (sz != strlen(p->xmlns[*ns].prefix))
			continue;
		else if (0 == strncmp(p->xmlns[*ns].prefix, nss, sz))
			return(1);

	caldav_err(p, "no matching namespace "
		"(\"%.*s\")", (int)sz, nss);
	return(0);
}

static int
caldav_alloc(struct parse *p, enum type type)
{

	if (NULL != p->p) {
		caldav_err(p, "request type already exists");
		return(0);
	}
	p->p = calloc(1, sizeof(struct caldav));
	if (NULL != p->p) {
		p->p->type = type;
		return(1);
	}
	caldav_err(p, "memory exhausted");
	return(0);
}

void
caldav_free(struct caldav *p)
{
	size_t	 i;

	if (NULL == p)
		return;

	for (i = 0; i < p->propsz; i++)
		prop_free(&p->props[i]);
	free(p->props);
	free(p);
}

static void
cdata(void *dat)
{

	/* Intentionally left blank. */
}

static int
xmlnspush(struct parse *p, const XML_Char **attrs)
{
	const char	*pfx;
	size_t	 	 i;

	while (NULL != attrs && NULL != *attrs) {
		if (strncmp(*attrs, "xmlns:", 6)) {
			attrs += 2;
			continue;
		}
		pfx = &(*attrs)[6];
		attrs++;
		for (i = 0; i < p->xmlnsz; i++) {
			if (strcmp(pfx, p->xmlns[i].prefix))
				continue;
			free(p->xmlns[i].urn);
			free(p->xmlns[i].prefix);
			p->xmlns[i].prefix = strdup(pfx);
			p->xmlns[i].urn = strdup(*attrs);
			if (NULL == p->xmlns[i].prefix) {
				perror(NULL);
				return(0);
			} else if (NULL == p->xmlns[i].urn) {
				perror(NULL);
				return(0);
			}
			break;
		}
		if (i == p->xmlnsz) {
			p->xmlns = realloc
				(p->xmlns, sizeof(struct xmlns) *
				 (p->xmlnsz + 1));
			if (NULL == p->xmlns) {
				perror(NULL);
				return(0);
			}
			p->xmlns[p->xmlnsz].prefix = strdup(pfx);
			p->xmlns[p->xmlnsz].urn = strdup(*attrs);
			if (NULL == p->xmlns[p->xmlnsz].prefix) {
				perror(NULL);
				return(0);
			} else if (NULL == p->xmlns[p->xmlnsz].urn) {
				perror(NULL);
				return(0);
			}
			p->xmlnsz++;
		}
		attrs++;
	}

	return(1);
}

static void
parseclose(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;
	enum calelem	 elem;
	size_t		 ns;

	if ( ! xmlnsfind(p, &ns, &s)) 
		return;

	switch ((elem = calelem_find(s))) {
	case (CALELEM_MKCALENDAR):
	case (CALELEM_CALENDAR_MULTIGET):
	case (CALELEM_CALENDAR_QUERY):
	case (CALELEM_PROPFIND):
		/* Clear our parsing context. */
		XML_SetDefaultHandler(p->xp, NULL);
		XML_SetElementHandler(p->xp, NULL, NULL);
		break;
	case (CALELEM_HREF):
		if (0 == p->buf.sz)
			break;
		p->p->hrefs = realloc(p->p->hrefs, 
			(p->p->hrefsz + 1) * sizeof(char *));
		if (NULL == p->p->hrefs) {
			caldav_err(p, "memory exhausted");
			break;
		}
		p->p->hrefs[p->p->hrefsz] = strdup(p->buf.buf);
		if (NULL == p->p->hrefs[p->p->hrefsz]) {
			caldav_err(p, "memory exhausted");
			break;
		}
		p->p->hrefsz++;
		XML_SetDefaultHandler(p->xp, NULL);
		break;
	default:
		break;
	}
}

static void
propclose(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;
	enum calelem	 elem;
	size_t		 ns;

	if ( ! xmlnsfind(p, &ns, &s))
		return;

	switch ((elem = calelem_find(s))) {
	case (CALELEM__MAX):
		break;
	case (CALELEM_PROP):
		XML_SetElementHandler(p->xp, parseopen, parseclose);
		break;
	default:
		if (PROP__MAX == calprops[elem])
			break;
		propadd(p, ns, s, calprops[elem], p->buf.buf);
		XML_SetDefaultHandler(p->xp, NULL);
		break;
	}
}

static void
parsebuffer(void *dat, const XML_Char *s, int len)
{
	struct parse	*p = dat;
	
	if (0 == len)
		return;
	assert(len > 0);
	bufappend(&p->buf, s, (size_t)len);
}

static void
propopen(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	*p = dat;
	enum calelem	 elem;
	size_t		 ns;

	xmlnspush(p, atts);
	if ( ! xmlnsfind(p, &ns, &s))
		return;

	switch ((elem = calelem_find(s))) {
	case (CALELEM__MAX):
		propadd(p, ns, s, PROP__MAX, NULL);
		break;
	default:
		if (PROP__MAX == calprops[elem]) {
			propadd(p, ns, s, PROP__MAX, NULL);
			break;
		}
		p->buf.sz = 0;
		XML_SetDefaultHandler(p->xp, parsebuffer);
		break;
	}
}

static void
parseopen(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	*p = dat;
	const XML_Char	*sv = s;
	size_t		 ns;

	xmlnspush(p, atts);
	if ( ! xmlnsfind(p, &ns, &s))
		return;

	switch (calelem_find(s)) {
	case (CALELEM_MKCALENDAR):
		caldav_alloc(p, TYPE_MKCALENDAR);
		break;
	case (CALELEM_PROPFIND):
		caldav_alloc(p, TYPE_PROPFIND);
		break;
	case (CALELEM_CALENDAR_QUERY):
		caldav_alloc(p, TYPE_CALQUERY);
		break;
	case (CALELEM_HREF):
		p->buf.sz = 0;
		XML_SetDefaultHandler(p->xp, parsebuffer);
		break;
	case (CALELEM_PROP):
		XML_SetElementHandler(p->xp, propopen, propclose);
		break;
	default:
		fprintf(stderr, "Unknown: <%s>\n", sv);
		break;
	}
}

struct caldav *
caldav_parse(const char *buf, size_t sz)
{
	struct parse	 p;
	size_t		 i;

	memset(&p, 0, sizeof(struct parse));

	if (NULL == (p.xp = XML_ParserCreate(NULL))) {
		perror(NULL);
		return(NULL);
	}

	bufappend(&p.buf, " ", 1);

	XML_SetDefaultHandler(p.xp, NULL);
	XML_SetElementHandler(p.xp, parseopen, parseclose);
	XML_SetCdataSectionHandler(p.xp, cdata, cdata);
	XML_SetUserData(p.xp, &p);

	if (XML_STATUS_OK != XML_Parse(p.xp, buf, (int)sz, 1)) {
		caldav_free(p.p);
		p.p = NULL;
	}

	XML_ParserFree(p.xp);
	free(p.buf.buf);
	for (i = 0; i < p.xmlnsz; i++) {
		free(p.xmlns[i].prefix);
		free(p.xmlns[i].urn);
	}
	free(p.xmlns);
	return(p.p);
}

struct caldav *
caldav_parsefile(const char *file)
{
	int		 fd;
	char		*map;
	size_t		 sz;
	struct stat	 st;
	struct caldav	*p;

	if (-1 == (fd = open(file, O_RDONLY, 0))) 
		return(NULL);

	if (-1 == fstat(fd, &st)) {
		close(fd);
		return(NULL);
	} 

	sz = st.st_size;
	map = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (MAP_FAILED == map) 
		return(NULL);

	p = caldav_parse(map, sz);
	munmap(map, sz);
	return(p);
}

