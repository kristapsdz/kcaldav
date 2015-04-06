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

#define CALDAVNS	"urn:ietf:params:xml:ns:caldav:"
#define	DAVNS		"DAV::"
#define	CALSERVNS	"http://calendarserver.org/ns/:"

struct	parse {
	struct caldav	 *p;
	XML_Parser	  xp;
	struct buf	  buf;
};

const enum proptype calprops[CALELEM__MAX] = {
	PROP_CALENDAR_DATA, /* CALELEM_CALENDAR_DATA */
	PROP_CALENDAR_HOME_SET, /* CALELEM_CALENDAR_HOME_SET */
	PROP__MAX, /* CALELEM_CALENDAR_MULTIGET */
	PROP__MAX, /* CALELEM_CALENDAR_QUERY */
	PROP_CALENDAR_USER_ADDRESS_SET, /* CALELEM_CALENDAR_USER_ADDRESS_SET */
	PROP_CURRENT_USER_PRINCIPAL, /* CALELEM_CURRENT_USER_PRINCIPAL */
	PROP_CURRENT_USER_PRIVILEGE_SET, /* CALELEM_CURRENT_USER_PRIVILEGE_SET */
	PROP_DISPLAYNAME, /* CALELEM_DISPLAYNAME */
	PROP_GETCONTENTTYPE, /* CALELEM_GETCONTENTTYPE */
	PROP_GETCTAG, /* CALELEM_GETCTAG */
	PROP_GETETAG, /* CALELEM_GETETAG */
	PROP__MAX, /* CALELEM_HREF */
	PROP_OWNER, /* CALELEM_OWNER */
	PROP_PRINCIPAL_URL, /* CALELEM_PRINCIPAL_URL */
	PROP__MAX, /* CALELEM_PROP */
	PROP__MAX, /* CALELEM_PROPERTYUPDATE */
	PROP__MAX, /* CALELEM_PROPFIND */
	PROP_QUOTA_AVAILABLE_BYTES, /* CALELEM_PROP_AVAILABLE_BYTES */
	PROP_QUOTA_USED_BYTES, /* CALELEM_PROP_USED_BYTES */
	PROP_RESOURCETYPE, /* CALELEM_RESOURCETYPE */
	PROP_SCHEDULE_CALENDAR_TRANSP, /* CALELEM_SCHEDULE_CALENDA... */
	PROP_SUPPORTED_CALENDAR_COMPONENT_SET, /* CALELEM_SUPPORTE... */
};

const enum calelem calpropelems[PROP__MAX] = {
	CALELEM_CALENDAR_DATA, /* PROP_CALENDAR_DATA */
	CALELEM_CALENDAR_HOME_SET, /* PROP_CALENDAR_HOME_SET */
	CALELEM_CALENDAR_USER_ADDRESS_SET, /* PROP_CALENDAR_USER_A... */
	CALELEM_CURRENT_USER_PRINCIPAL, /* PROP_CURRENT_USER_PRINC... */
	CALELEM_CURRENT_USER_PRIVILEGE_SET, /* PROP_CURRENT_USER_P... */
	CALELEM_DISPLAYNAME, /* PROP_DISPLAYNAME */
	CALELEM_GETCONTENTTYPE, /* PROP_GETCONTENTTYPE */
	CALELEM_GETCTAG, /* PROP_GETCTAG */
	CALELEM_GETETAG, /* PROP_GETETAG */
	CALELEM_OWNER, /* PROP_OWNER */
	CALELEM_PRINCIPAL_URL,/* PROP_PRINCIPAL_URL */
	CALELEM_QUOTA_AVAILABLE_BYTES, /* PROP_QUOTA_AVAILABLE_BYTES */
	CALELEM_QUOTA_USED_BYTES, /* PROP_QUOTA_USED_BYTES */
	CALELEM_RESOURCETYPE,/* PROP_RESOURCETYPE */
	CALELEM_SCHEDULE_CALENDAR_TRANSP,/* PROP_SCHEDULE_CALENDAR... */
	CALELEM_SUPPORTED_CALENDAR_COMPONENT_SET,/* PROP_SUPPORTED... */
};

const char *const calelems[CALELEM__MAX] = {
	CALDAVNS "calendar-data", /* CALELEM_CALENDAR_DATA */
	CALDAVNS "calendar-home-set", /* CALELEM_CALENDAR_HOME_SET */
	CALDAVNS "calendar-multiget", /* CALELEM_CALENDAR_MULTIGET */
	CALDAVNS "calendar-query", /* CALELEM_CALENDAR_QUERY */
	CALDAVNS "calendar-user-address-set", /* CALELEM_CALENDAR_USER_ADDR... */
	DAVNS "current-user-principal", /* CALELEM_CURRENT_USER_PRINCIPAL */
	DAVNS "current-user-privilege-set", /* CALELEM_CURRENT_USER_PRIV... */
	DAVNS "displayname", /* CALELEM_DISPLAYNAME */
	DAVNS "getcontenttype", /* CALELEM_GETCONTENTTYPE */
	CALSERVNS "getctag", /* CALELEM_GETCTAG */
	DAVNS "getetag", /* CALELEM_GETETAG */
	DAVNS "href", /* CALELEM_HREF */
	DAVNS "owner", /* CALELEM_OWNER */
	DAVNS "principal-URL", /* CALELEM_PRINCIPAL_URL */
	DAVNS "prop", /* CALELEM_PROP */
	DAVNS "propertyupdate", /* CALELEM_PROPERTYUPDATE */
	DAVNS "propfind", /* CALELEM_PROPFIND */
	DAVNS "quota-available-bytes", /* CALELEM_QUOTA_AVAILABLE_BYTES */
	DAVNS "quota-used-bytes", /* CALELEM_QUOTA_USED_BYTES */
	DAVNS "resourcetype", /* CALELEM_RESOURCETYPE */
	CALDAVNS "schedule-calendar-transp", /* CALELEM_SCHEDULE_CALENDAR_T... */
	CALDAVNS "supported-calendar-component-set", /* CALELEM_SUPPORTED_C... */
};

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
			return(i);

	return(CALELEM__MAX);
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
	fprintf(stderr, "\n");
	XML_StopParser(p->xp, 0);
}

static void
propadd(struct parse *p, const XML_Char *name, 
	enum proptype prop, const char *cp)
{
	const XML_Char	*ns;
	size_t		 namesz, nssz;

	if (NULL == p->p) {
		caldav_err(p, "property list in unknown request");
		return;
	} else if (NULL != (ns = strrchr(name, ':'))) {
		namesz = ns - name;
		nssz = strlen(++ns);
	} else {
		namesz = strlen(name);
		nssz = 0;
	}

	p->p->props = reallocarray(p->p->props,
		p->p->propsz + 1, sizeof(struct prop));
	if (NULL == p->p->props) {
		caldav_err(p, "memory exhausted");
		return;
	}

	p->p->props[p->p->propsz].key = prop;
	p->p->props[p->p->propsz].xmlns = malloc(namesz + 1);
	if (NULL == p->p->props[p->p->propsz].xmlns) {
		caldav_err(p, "memory exhausted");
		return;
	}
	memcpy(p->p->props[p->p->propsz].xmlns, name, namesz);
	p->p->props[p->p->propsz].xmlns[namesz] = '\0';

	p->p->props[p->p->propsz].name = malloc(nssz + 1);
	if (NULL == p->p->props[p->p->propsz].name) {
		caldav_err(p, "memory exhausted");
		return;
	}
	memcpy(p->p->props[p->p->propsz].name, ns, nssz);
	p->p->props[p->p->propsz].name[nssz] = '\0';
	kdbg("Property: %s, %s", 
		p->p->props[p->p->propsz].name,
		p->p->props[p->p->propsz].xmlns);
	p->p->propsz++;
}

static void
prop_free(struct prop *p)
{

	free(p->xmlns);
	free(p->name);
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

static void
parseclose(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;

	switch (calelem_find(s)) {
	case (CALELEM_CALENDAR_MULTIGET):
	case (CALELEM_CALENDAR_QUERY):
	case (CALELEM_PROPERTYUPDATE):
	case (CALELEM_PROPFIND):
		/* Clear our parsing context. */
		XML_SetDefaultHandler(p->xp, NULL);
		XML_SetElementHandler(p->xp, NULL, NULL);
		break;
	case (CALELEM_HREF):
		if (0 == p->buf.sz)
			break;
		p->p->hrefs = reallocarray(p->p->hrefs, 
			p->p->hrefsz + 1, sizeof(char *));
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

	switch ((elem = calelem_find(s))) {
	case (CALELEM__MAX):
		break;
	case (CALELEM_PROP):
		XML_SetElementHandler(p->xp, parseopen, parseclose);
		break;
	default:
		if (PROP__MAX == calprops[elem])
			break;
		propadd(p, s, calprops[elem], p->buf.buf);
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

	if (CALELEM__MAX == (elem = calelem_find(s))) 
		kdbg("Unknown property: %s", s);
	else
		kdbg("Known property: %s", s);

	switch (elem) {
	case (CALELEM__MAX):
		propadd(p, s, PROP__MAX, NULL);
		break;
	default:
		if (PROP__MAX == calprops[elem]) {
			propadd(p, s, PROP__MAX, NULL);
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
	enum calelem	 elem;

	if (CALELEM__MAX != (elem = calelem_find(s)))
		kdbg("Known element: %s", s);
	else
		kdbg("Unknown element: %s", s);

	switch (elem) {
	case (CALELEM_CALENDAR_QUERY):
		caldav_alloc(p, TYPE_CALQUERY);
		break;
	case (CALELEM_CALENDAR_MULTIGET):
		caldav_alloc(p, TYPE_CALMULTIGET);
		break;
	case (CALELEM_PROPERTYUPDATE):
		caldav_alloc(p, TYPE_PROPERTYUPDATE);
		break;
	case (CALELEM_PROPFIND):
		caldav_alloc(p, TYPE_PROPFIND);
		break;
	case (CALELEM_HREF):
		p->buf.sz = 0;
		XML_SetDefaultHandler(p->xp, parsebuffer);
		break;
	case (CALELEM_PROP):
		XML_SetElementHandler(p->xp, propopen, propclose);
		break;
	default:
		break;
	}
}

struct caldav *
caldav_parse(const char *buf, size_t sz)
{
	struct parse	 p;

	memset(&p, 0, sizeof(struct parse));

	if (NULL == (p.xp = XML_ParserCreateNS(NULL, ':'))) {
		kerr(NULL);
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
	return(p.p);
}

