#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <expat.h>

#include "extern.h"

enum	calelem {
	CALELEM_MKCALENDAR,
	CALELEM_PROPFIND,
	CALELEM_DISPLAYNAME,
	CALELEM_CALDESC,
	CALELEM_CALQUERY,
	CALELEM_CALTIMEZONE,
	CALELEM_CREATIONDATE,
	CALELEM_GETCONTENTLANGUAGE,
	CALELEM_GETCONTENTLENGTH,
	CALELEM_GETCONTENTTYPE,
	CALELEM_GETETAG,
	CALELEM_GETLASTMODIFIED,
	CALELEM_GETLOCKDISCOVERY,
	CALELEM_RESOURCETYPE,
	CALELEM_SOURCE,
	CALELEM_SUPPORTLOCK,
	CALELEM__MAX
};

struct	parse {
	struct caldav	*p;
	XML_Parser	 xp;
	struct buf	 buf;
};

static	const enum proptype __calprops[CALELEM__MAX] = {
	PROP__MAX, /* CALELEM_MKCALENDAR */
	PROP__MAX, /* CALELEM_PROPFIND */
	PROP_DISPLAYNAME, /* CALELEM_DISPLAYNAME */
	PROP_CALDESC, /* CALELEM_CALDESC */
	PROP__MAX, /* CALELEM_CALQUERY */
	PROP_CALTIMEZONE, /* CALELEM_CALTIMEZONE */
	PROP_CREATIONDATE, /* CALELEM_CREATIONDATE */
	PROP_GETCONTENTLANGUAGE, /* CALELEM_GETCONTENTLANGUAGE */
	PROP_GETCONTENTLENGTH, /* CALELEM_GETCONTENTLENGTH */
	PROP_GETCONTENTTYPE, /* CALELEM_GETCONTENTTYPE */
	PROP_GETETAG, /* CALELEM_GETETAG */
	PROP_GETLASTMODIFIED, /* CALELEM_GETLASTMODIFIED */
	PROP_GETLOCKDISCOVERY, /* CALELEM_GETLOCKDISCOVERY */
	PROP_RESOURCETYPE, /* CALELEM_RESOURCETYPE */
	PROP_SOURCE, /* CALELEM_SOURCE */
	PROP_SUPPORTLOCK, /* CALELEM_SUPPORTLOCK */
};

const enum proptype *calprops = __calprops;

const char *const __calelems[CALELEM__MAX] = {
	"mkcalendar", /* CALELEM_MKCALENDAR */
	"propfind", /* CALELEM_PROPFIND */
	"displayname", /* CALELEM_DISPLAYNAME */
	"calendar-description", /* CALELEM_CALDESC */
	"calendar-query", /* CALELEM_CALQUERY */
	"calendar-timezone", /* CALELEM_CALTIMEZONE */
	"creationdate", /* CALELEM_CREATIONDATE */
	"getcontentlanguage", /* CALELEM_GETCONTENTLANGUAGE */
	"getcontentlength", /* CALELEM_GETCONTENTLENGTH */
	"getcontenttype", /* CALELEM_GETCONTENTTYPE */
	"getetag", /* CALELEM_GETETAG */
	"getlastmodified", /* CALELEM_GETLASTMODIFIED */
	"getlockdiscovery", /* CALELEM_GETLOCKDISCOVERY */
	"getresourcetype", /* CALELEM_RESOURCETYPE */
	"source", /* CALELEM_SOURCE */
	"supportlock", /* CALELEM_SUPPORTLOCK */
};

const char *const *calelems = __calelems;

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
propadd(struct parse *p, enum proptype prop, const char *cp)
{
	struct prop	**ps;
	size_t		 *psz;

	switch (p->p->type) {
	case (TYPE_PROPFIND):
		ps = &p->p->d.propfind.props;
		psz = &p->p->d.propfind.propsz;
		break;
	case (TYPE_MKCALENDAR):
		ps = &p->p->d.mkcal.props;
		psz = &p->p->d.mkcal.propsz;
		break;
	default:
		abort();
	}

	*ps = realloc(*ps, 
		sizeof(struct prop) * (*psz + 1));

	if (NULL == *ps) {
		caldav_err(p, "memory exhausted");
		return;
	}

	(*ps)[(*psz)].key = prop;
	(*ps)[(*psz)++].value = strdup(cp);
}

static void
prop_free(struct prop *p)
{

	free(p->value);
}

static void
propfind_free(struct propfind *p)
{
	size_t	 i;

	for (i = 0; i < p->propsz; i++)
		prop_free(&p->props[i]);

	free(p->props);
}

static void
mkcal_free(struct mkcal *p)
{
	size_t	 i;

	for (i = 0; i < p->propsz; i++)
		prop_free(&p->props[i]);

	free(p->props);
}

static const XML_Char *
strip(const XML_Char *s)
{
	const XML_Char	*cp;

	/* Ignore our XML prefix. */
	for (cp = s; '\0' != *cp; cp++)
		if (':' == *cp)
			return(cp + 1);

	return(s);
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

	if (NULL == p)
		return;

	switch (p->type) {
	case (TYPE_MKCALENDAR):
		mkcal_free(&p->d.mkcal);
		break;
	case (TYPE_PROPFIND):
		propfind_free(&p->d.propfind);
		break;
	default:
		abort();
	}

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
	enum calelem	 elem;

	s = strip(s);

	switch ((elem = calelem_find(s))) {
	case (CALELEM_MKCALENDAR):
	case (CALELEM_PROPFIND):
		/* Clear our parsing context. */
		XML_SetDefaultHandler(p->xp, NULL);
		XML_SetElementHandler(p->xp, NULL, NULL);
		break;
	case (CALELEM_CALDESC):
	case (CALELEM_DISPLAYNAME):
	case (CALELEM_CALTIMEZONE):
	case (CALELEM_CREATIONDATE):
	case (CALELEM_GETCONTENTLANGUAGE):
	case (CALELEM_GETCONTENTLENGTH):
	case (CALELEM_GETCONTENTTYPE):
	case (CALELEM_GETETAG):
	case (CALELEM_GETLASTMODIFIED):
	case (CALELEM_GETLOCKDISCOVERY):
	case (CALELEM_RESOURCETYPE):
	case (CALELEM_SOURCE):
	case (CALELEM_SUPPORTLOCK):
		propadd(p, calprops[elem], p->buf.buf);
		XML_SetDefaultHandler(p->xp, NULL);
		break;
	default:
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
parseopen(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	*p = dat;

	s = strip(s);

	switch (calelem_find(s)) {
	case (CALELEM_MKCALENDAR):
		if ( ! caldav_alloc(p, TYPE_MKCALENDAR))
			return;
		break;
	case (CALELEM_PROPFIND):
		if ( ! caldav_alloc(p, TYPE_PROPFIND))
			return;
		break;
	case (CALELEM_CALQUERY):
		if ( ! caldav_alloc(p, TYPE_CALQUERY))
			return;
		break;
	case (CALELEM_DISPLAYNAME):
	case (CALELEM_CALDESC):
	case (CALELEM_CALTIMEZONE):
	case (CALELEM_CREATIONDATE):
	case (CALELEM_GETCONTENTLANGUAGE):
	case (CALELEM_GETCONTENTLENGTH):
	case (CALELEM_GETCONTENTTYPE):
	case (CALELEM_GETETAG):
	case (CALELEM_GETLASTMODIFIED):
	case (CALELEM_GETLOCKDISCOVERY):
	case (CALELEM_RESOURCETYPE):
	case (CALELEM_SOURCE):
	case (CALELEM_SUPPORTLOCK):
		p->buf.sz = 0;
		XML_SetDefaultHandler(p->xp, parsebuffer);
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

	if (NULL == (p.xp = XML_ParserCreate(NULL))) {
		perror(NULL);
		return(NULL);
	}

	bufappend(&p.buf, "", 0);

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
