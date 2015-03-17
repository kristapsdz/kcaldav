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

struct	parse {
	struct caldav	*p;
	XML_Parser	 xp;
	struct buf	 buf;
};

static	const enum proptype __calprops[CALELEM__MAX] = {
	PROP__MAX, /* CALELEM_MKCALENDAR */
	PROP__MAX, /* CALELEM_PROPFIND */
	PROP__MAX, /* CALELEM_PROPLIST */
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
	PROP_LOCKDISCOVERY, /* CALELEM_LOCKDISCOVERY */
	PROP_RESOURCETYPE, /* CALELEM_RESOURCETYPE */
	PROP_SOURCE, /* CALELEM_SOURCE */
	PROP_SUPPORTLOCK, /* CALELEM_SUPPORTLOCK */
	PROP_EXECUTABLE, /* CALELEM_EXECUTABLE */
	PROP_CHECKEDIN, /* CALELEM_CHECKEDIN */
	PROP_CHECKEDOUT, /* CALELEM_CHECKEDOUT */
};

static	const enum calelem __calpropelems[PROP__MAX] = {
	CALELEM_CALDESC, /* PROP_CALDESC */
	CALELEM_CALTIMEZONE,/* PROP_CALTIMEZONE */
	CALELEM_CREATIONDATE,/* PROP_CREATIONDATE */
	CALELEM_DISPLAYNAME,/* PROP_DISPLAYNAME */
	CALELEM_GETCONTENTLANGUAGE,/* PROP_GETCONTENTLANGUAGE */
	CALELEM_GETCONTENTLENGTH,/* PROP_GETCONTENTLENGTH */
	CALELEM_GETCONTENTTYPE,/* PROP_GETCONTENTTYPE */
	CALELEM_GETETAG, /* PROP_GETETAG */
	CALELEM_GETLASTMODIFIED,/* PROP_GETLASTMODIFIED */
	CALELEM_LOCKDISCOVERY,/* PROP_LOCKDISCOVERY */
	CALELEM_RESOURCETYPE,/* PROP_RESOURCETYPE */
	CALELEM_SOURCE, /* PROP_SOURCE */
	CALELEM_SUPPORTLOCK, /* PROP_SUPPORTLOCK */
	CALELEM_EXECUTABLE, /* PROP_EXECUTABLE */
	CALELEM_CHECKEDIN, /* CALELEM_CHECKEDIN */
	CALELEM_CHECKEDOUT, /* CALELEM_CHECKEDOUT */
};

const enum proptype *calprops = __calprops;
const enum calelem *calpropelems = __calpropelems;

const char *const __calelems[CALELEM__MAX] = {
	"mkcalendar", /* CALELEM_MKCALENDAR */
	"propfind", /* CALELEM_PROPFIND */
	"proplist", /* CALELEM_PROPLIST */
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
	"lockdiscovery", /* CALELEM_LOCKDISCOVERY */
	"resourcetype", /* CALELEM_RESOURCETYPE */
	"source", /* CALELEM_SOURCE */
	"supportlock", /* CALELEM_SUPPORTLOCK */
	"executable", /* CALELEM_EXECUTABLE */
	"checked-in", /* CALELEM_CHECKEDIN */
	"checked-out", /* CALELEM_CHECKEDOUT */
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

	p->p->props = realloc(p->p->props,
		sizeof(struct prop) * (p->p->propsz + 1));

	if (NULL == p->p->props) {
		caldav_err(p, "memory exhausted");
		return;
	}

	p->p->props[p->p->propsz].key = prop;
	p->p->props[p->p->propsz].value = strdup(cp);
	p->p->propsz++;
}

static void
prop_free(struct prop *p)
{

	free(p->value);
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
	enum calelem	 elem;

	fprintf(stderr, "End parsing: </%s>\n", s);
	s = strip(s);

	switch ((elem = calelem_find(s))) {
	case (CALELEM_MKCALENDAR):
	case (CALELEM_CALQUERY):
	case (CALELEM_PROPFIND):
	case (CALELEM_PROPLIST):
		/* Clear our parsing context. */
		XML_SetDefaultHandler(p->xp, NULL);
		XML_SetElementHandler(p->xp, NULL, NULL);
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
	case (CALELEM_LOCKDISCOVERY):
	case (CALELEM_RESOURCETYPE):
	case (CALELEM_SOURCE):
	case (CALELEM_SUPPORTLOCK):
	case (CALELEM_EXECUTABLE):
	case (CALELEM_CHECKEDIN):
	case (CALELEM_CHECKEDOUT):
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

	fprintf(stderr, "Parsing: <%s>\n", s);
	s = strip(s);

	switch (calelem_find(s)) {
	case (CALELEM_MKCALENDAR):
		caldav_alloc(p, TYPE_MKCALENDAR);
		break;
	case (CALELEM_PROPFIND):
		caldav_alloc(p, TYPE_PROPFIND);
		break;
	case (CALELEM_CALQUERY):
		caldav_alloc(p, TYPE_CALQUERY);
		break;
	case (CALELEM_PROPLIST):
		caldav_alloc(p, TYPE_PROPLIST);
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
	case (CALELEM_LOCKDISCOVERY):
	case (CALELEM_RESOURCETYPE):
	case (CALELEM_SOURCE):
	case (CALELEM_SUPPORTLOCK):
	case (CALELEM_EXECUTABLE):
	case (CALELEM_CHECKEDIN):
	case (CALELEM_CHECKEDOUT):
		p->buf.sz = 0;
		XML_SetDefaultHandler(p->xp, parsebuffer);
		break;
	default:
		fprintf(stderr, "Unknown: <%s>\n", s);
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

