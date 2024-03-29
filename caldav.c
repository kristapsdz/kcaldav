/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <expat.h>

#include "libkcaldav.h"

#define APPLENS		"http://apple.com/ns/ical/:"
#define CALDAVNS	"urn:ietf:params:xml:ns:caldav:"
#define	DAVNS		"DAV::"
#define	CALSERVNS	"http://calendarserver.org/ns/:"

struct	buf {
	char		*buf;
	size_t		 sz;
	size_t		 max;
};

/*
 * Parse sequence.
 */
struct	parse {
	struct caldav	 *p; /* resulting structure */
	XML_Parser	  xp;
	struct buf	  buf;
	char		**er;
};

typedef	int (*propvalid)(const char *);

/*
 * This maps from the calendar XML element to a property.
 * Of course, not all elements have associated properties!
 */
const enum calproptype calprops[CALELEM__MAX] = {
	CALPROP_CALENDAR_COLOR, /* CALELEM_CALENDAR_COLOR */
	CALPROP_CALENDAR_DATA, /* CALELEM_CALENDAR_DATA */
	CALPROP_CALENDAR_DESCRIPTION, /* CALELEM_CALENDAR_DESCRIPTION */
	CALPROP_CALENDAR_HOME_SET, /* CALELEM_CALENDAR_HOME_SET */
	CALPROP_MIN_DATE_TIME, /* CALELEM_MIN_DATE_TIME */
	CALPROP__MAX, /* CALELEM_CALENDAR_MULTIGET */
	CALPROP_CALENDAR_PROXY_READ_FOR, /* CALELEM_CALENDAR_PROXY_RE... */
	CALPROP_CALENDAR_PROXY_WRITE_FOR, /* CALELEM_CALENDAR_PROXY_W... */
	CALPROP__MAX, /* CALELEM_CALENDAR_QUERY */
	CALPROP_CALENDAR_TIMEZONE, /* CALELEM_CALENDAR_TIMEZONE */
	CALPROP_CALENDAR_USER_ADDRESS_SET, /* CALELEM_CALENDAR_USER_A... */
	CALPROP_CURRENT_USER_PRINCIPAL, /* CALELEM_CURRENT_USER_PRINC... */
	CALPROP_CURRENT_USER_PRIVILEGE_SET, /* CALELEM_CURRENT_USER_P... */
	CALPROP_DISPLAYNAME, /* CALELEM_DISPLAYNAME */
	CALPROP_GETCONTENTTYPE, /* CALELEM_GETCONTENTTYPE */
	CALPROP_GETCTAG, /* CALELEM_GETCTAG */
	CALPROP_GETETAG, /* CALELEM_GETETAG */
	CALPROP_GROUP_MEMBER_SET, /* CALELEM_GROUP_MEMBER_SET */
	CALPROP_GROUP_MEMBERSHIP, /* CALELEM_GROUP_MEMBERSHIP */
	CALPROP__MAX, /* CALELEM_HREF */
	CALPROP_OWNER, /* CALELEM_OWNER */
	CALPROP_PRINCIPAL_URL, /* CALELEM_PRINCIPAL_URL */
	CALPROP__MAX, /* CALELEM_PROP */
	CALPROP__MAX, /* CALELEM_PROPERTYUPDATE */
	CALPROP__MAX, /* CALELEM_PROPFIND */
	CALPROP_QUOTA_AVAILABLE_BYTES, /* CALELEM_PROP_AVAILABLE_BYTES */
	CALPROP_QUOTA_USED_BYTES, /* CALELEM_PROP_USED_BYTES */
	CALPROP_RESOURCETYPE, /* CALELEM_RESOURCETYPE */
	CALPROP_SCHEDULE_CALENDAR_TRANSP, /* CALELEM_SCHEDULE_CALENDA... */
	CALPROP_SUPPORTED_CALENDAR_COMPONENT_SET, /* CALELEM_SUPPORTE... */
	CALPROP_SUPPORTED_CALENDAR_DATA, /* CALELEM_SUPPORTED_CALENDA... */
};

const enum calelem calpropelems[CALPROP__MAX] = {
	CALELEM_CALENDAR_COLOR, /* CALPROP_CALENDAR_COLOR */
	CALELEM_CALENDAR_DATA, /* CALPROP_CALENDAR_DATA */
	CALELEM_CALENDAR_DESCRIPTION, /* CALPROP_CALENDAR_DESCRIPTION */
	CALELEM_CALENDAR_HOME_SET, /* CALPROP_CALENDAR_HOME_SET */
	CALELEM_MIN_DATE_TIME, /* CALPROP_MIN_DATE_TIME */
	CALELEM_CALENDAR_PROXY_READ_FOR, /* CALPROP_CALENDAR_PROXY_RE... */
	CALELEM_CALENDAR_PROXY_WRITE_FOR, /* CALPROP_CALENDAR_PROXY_W... */
	CALELEM_CALENDAR_TIMEZONE, /* CALPROP_CALENDAR_TIMEZONE */
	CALELEM_CALENDAR_USER_ADDRESS_SET, /* CALPROP_CALENDAR_USER_A... */
	CALELEM_CURRENT_USER_PRINCIPAL, /* CALPROP_CURRENT_USER_PRINC... */
	CALELEM_CURRENT_USER_PRIVILEGE_SET, /* CALPROP_CURRENT_USER_P... */
	CALELEM_DISPLAYNAME, /* CALPROP_DISPLAYNAME */
	CALELEM_GETCONTENTTYPE, /* CALPROP_GETCONTENTTYPE */
	CALELEM_GETCTAG, /* CALPROP_GETCTAG */
	CALELEM_GETETAG, /* CALPROP_GETETAG */
	CALELEM_GROUP_MEMBER_SET, /* CALPROP_GROUP_MEMBER_SET */
	CALELEM_GROUP_MEMBERSHIP, /* CALPROP_GROUP_MEMBERSHIP */
	CALELEM_OWNER, /* CALPROP_OWNER */
	CALELEM_PRINCIPAL_URL,/* CALPROP_PRINCIPAL_URL */
	CALELEM_QUOTA_AVAILABLE_BYTES, /* CALPROP_QUOTA_AVAILABLE_BYTES */
	CALELEM_QUOTA_USED_BYTES, /* CALPROP_QUOTA_USED_BYTES */
	CALELEM_RESOURCETYPE,/* CALPROP_RESOURCETYPE */
	CALELEM_SCHEDULE_CALENDAR_TRANSP,/* CALPROP_SCHEDULE_CALENDAR... */
	CALELEM_SUPPORTED_CALENDAR_COMPONENT_SET,/* CALPROP_SUPPORTED... */
	CALELEM_SUPPORTED_CALENDAR_DATA,/* CALPROP_SUPPORTED_CALENDAR... */
};

const char *const calelems[CALELEM__MAX] = {
	APPLENS "calendar-color", /* CALELEM_CALENDAR_COLOR */
	CALDAVNS "calendar-data", /* CALELEM_CALENDAR_DATA */
	CALDAVNS "calendar-description", /* CALELEM_CALENDAR_DESCR... */
	CALDAVNS "calendar-home-set", /* CALELEM_CALENDAR_HOME_SET */
	CALDAVNS "min-date-time", /* CALELEM_MIN_DATE_TIME */
	CALDAVNS "calendar-multiget", /* CALELEM_CALENDAR_MULTIGET */
	CALSERVNS "calendar-proxy-read-for", /* CALELEM_CALENDAR_P... */
	CALSERVNS "calendar-proxy-write-for", /* CALELEM_CALENDAR_... */
	CALDAVNS "calendar-query", /* CALELEM_CALENDAR_QUERY */
	CALDAVNS "calendar-timezone", /* CALELEM_CALENDAR_TIMEZONE */
	CALDAVNS "calendar-user-address-set", /* CALELEM_CALENDAR_... */
	DAVNS "current-user-principal", /* CALELEM_CURRENT_USER_PR... */
	DAVNS "current-user-privilege-set", /* CALELEM_CURRENT_USE... */
	DAVNS "displayname", /* CALELEM_DISPLAYNAME */
	DAVNS "getcontenttype", /* CALELEM_GETCONTENTTYPE */
	CALSERVNS "getctag", /* CALELEM_GETCTAG */
	DAVNS "getetag", /* CALELEM_GETETAG */
	DAVNS "group-member-set", /* CALELEM_GROUP_MEMBER_SET */
	DAVNS "group-membership", /* CALELEM_GROUP_MEMBERSHIP */
	DAVNS "href", /* CALELEM_HREF */
	DAVNS "owner", /* CALELEM_OWNER */
	DAVNS "principal-URL", /* CALELEM_PRINCIPAL_URL */
	DAVNS "prop", /* CALELEM_PROP */
	DAVNS "propertyupdate", /* CALELEM_PROPERTYUPDATE */
	DAVNS "propfind", /* CALELEM_PROPFIND */
	DAVNS "quota-available-bytes", /* CALELEM_QUOTA_AVAILABLE_... */
	DAVNS "quota-used-bytes", /* CALELEM_QUOTA_USED_BYTES */
	DAVNS "resourcetype", /* CALELEM_RESOURCETYPE */
	CALDAVNS "schedule-calendar-transp", /* CALELEM_SCHEDULE_C... */
	CALDAVNS "supported-calendar-component-set", /* CALELEM_SU... */
	CALDAVNS "supported-calendar-data", /* CALELEM_SUPPORTED_C... */
};

static int	 propvalid_rgb(const char *);

static const propvalid propvalids[CALPROP__MAX] = {
	propvalid_rgb, /* CALPROP_CALENDAR_COLOR */
	NULL, /* CALPROP_CALENDAR_DATA */
	NULL, /* CALPROP_CALENDAR_HOME_SET */
	NULL, /* CALPROP_MIN_DATE_TIME */
	NULL, /* CALPROP_CALENDAR_PROXY_READ_FOR */
	NULL, /* CALPROP_CALENDAR_PROXY_WRITE_FOR */
	NULL, /* CALPROP_CALENDAR_TIMEZONE */
	NULL, /* CALPROP_CALENDAR_USER_ADDRESS_SET */
	NULL, /* CALPROP_CURRENT_USER_PRINCIPAL */
	NULL, /* CALPROP_CURRENT_USER_PRIVILEGE_SET */
	NULL, /* CALPROP_DISPLAYNAME */
	NULL, /* CALPROP_GETCONTENTTYPE */
	NULL, /* CALPROP_GETCTAG */
	NULL, /* CALPROP_GETETAG */
	NULL, /* CALPROP_GROUP_MEMBER_SET */
	NULL, /* CALPROP_GROUP_MEMBERSHIP */
	NULL, /* CALPROP_OWNER */
	NULL, /* CALPROP_PRINCIPAL_URL */
	NULL, /* CALPROP_QUOTA_AVAILABLE_BYTES */
	NULL, /* CALPROP_QUOTA_USED_BYTES */
	NULL, /* CALPROP_RESOURCETYPE */
	NULL, /* CALPROP_SCHEDULE_CALENDAR_TRANSP */
	NULL, /* CALPROP_SUPPORTED_CALENDAR_COMPONENT_SET */
	NULL, /* PROP_SUPPORTED_CALENDAR_DATA */
};

static void	parseclose(void *, const XML_Char *);
static void	propclose(void *, const XML_Char *);
static void	propopen(void *, const XML_Char *, const XML_Char **);
static void	parseopen(void *, const XML_Char *, const XML_Char **);

static int
bufappend(struct buf *p, const char *s, size_t len)
{
	void	*pp;

	if (0 == len)
		return 1;

	if (p->sz + len + 1 > p->max) {
		p->max = p->sz + len + 1024;
		if ((pp = realloc(p->buf, p->max)) == NULL)
			return 0;
		p->buf = pp;
	}

	memcpy(p->buf + p->sz, s, len);
	p->sz += len;
	p->buf[p->sz] = '\0';
	return 1;
}

static int
propvalid_rgb(const char *cp)
{
	size_t	 i, sz;

	if ((7 != (sz = strlen(cp))) && 9 != sz)
		return(0);
	if ('#' != cp[0])
		return(0);
	for (i = 1; i < sz; i++) {
		if (isdigit((unsigned char)cp[i]))
			continue;
		if (isalpha((unsigned char)cp[i]) && 
			((cp[i] >= 'a' && 
			  cp[i] <= 'f') ||
			 (cp[i] >= 'A' &&
			  cp[i] <= 'F')))
			continue;
		return(0);
	}
	return(1);
}

static enum calelem
calelem_find(const XML_Char *name)
{
	enum calelem	 i;

	for (i = 0; i < CALELEM__MAX; i++)
		if (0 == strcmp(calelems[i], name))
			return(i);

	return CALELEM__MAX;
}

static void
caldav_err(struct parse *p, const char *fmt, ...)
{
	va_list	 ap;
	char	*buf;
	int	 c;

	XML_StopParser(p->xp, 0);

	if (p->er == NULL || *p->er != NULL)
		return;

	*p->er = NULL;

	assert(fmt != NULL);
	va_start(ap, fmt);
	c = vasprintf(&buf, fmt, ap);
	va_end(ap);

	if (c < 0)
		return;

	c = asprintf(p->er, "%zu:%zu: %s", 
		XML_GetCurrentLineNumber(p->xp),
		XML_GetCurrentColumnNumber(p->xp), buf);
	
	free(buf);
}

/*
 * Add a property we've parsed from our input.
 * Properties appear in nearly all CalDAV XML requests, either as
 * requests for a property (e.g, CALREQTYPE_PROPFIND) or for setting
 * properties (e.g., CALREQTYPE_PROPERTYUPDATE).
 * If we know about a property, it's type isn't CALPROP__MAX.
 * If we're setting a property, we also validate the value to make sure
 * it's ok.
 */
static void
propadd(struct parse *p, const XML_Char *name, 
	enum calproptype prop, const char *cp)
{
	const XML_Char	*ns;
	size_t		 namesz, nssz;
	void		*pp;

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

	pp = reallocarray(p->p->props,
		p->p->propsz + 1, sizeof(struct calprop));
	if (NULL == pp) {
		caldav_err(p, "memory exhausted");
		return;
	}
	p->p->props = pp;
	memset(&p->p->props[p->p->propsz], 0, sizeof(struct calprop));

	/*
	 * Copy over the name and XML namespace as the parser gives it
	 * to us: with the colon as a separator.
	 */
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
	p->p->propsz++;
	
	/* Now prep for settable properties... */
	if (CALPROP__MAX == prop)
		return;
	if (CALREQTYPE_PROPERTYUPDATE != p->p->type) 
		return;

	/*
	 * Copy over any settable value.
	 * Make sure that we validate anything that we could set.
	 */
	assert(NULL != cp);
	p->p->props[p->p->propsz - 1].val = strdup(cp);
	if (NULL == p->p->props[p->p->propsz - 1].val) {
		caldav_err(p, "memory exhausted");
		return;
	} else if (NULL == propvalids[prop])
		return;

	/* Validation... */
	p->p->props[p->p->propsz - 1].valid = 
		propvalids[prop](cp) ? 1 : -1;

	if (p->p->props[p->p->propsz - 1].valid >= 0) 
		return;
}

static void
prop_free(struct calprop *p)
{

	free(p->xmlns);
	free(p->val);
	free(p->name);
}

static int
caldav_alloc(struct parse *p, enum calreqtype type)
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
	for (i = 0; i < p->hrefsz; i++)
		free(p->hrefs[i]);
	free(p->props);
	free(p->hrefs);
	free(p);
}

static void
cdata(void *dat)
{

	/* Intentionally left blank. */
}

static char
parsehex(char ch)
{

	return isdigit((unsigned char)ch) ? 
		ch - '0' : tolower((unsigned char)ch) - 'a' + 10;
}

static void
parseclose(void *dat, const XML_Char *s)
{
	struct parse	 *p = dat;
	size_t		  i, j, sz, len;
	int		  c;
	char		**array;

	switch (calelem_find(s)) {
	case CALELEM_CALENDAR_MULTIGET:
	case CALELEM_CALENDAR_QUERY:
	case CALELEM_PROPERTYUPDATE:
	case CALELEM_PROPFIND:
		/* Clear our parsing context. */
		XML_SetDefaultHandler(p->xp, NULL);
		XML_SetElementHandler(p->xp, NULL, NULL);
		break;
	case CALELEM_HREF:
		if (0 == p->buf.sz)
			break;
		/*
		 * According to the WebDAV RFC 4918, we need to URL
		 * decode this.
		 */
		array = reallocarray(p->p->hrefs, 
			p->p->hrefsz + 1, sizeof(char *));
		if (NULL == array) {
			caldav_err(p, "memory exhausted");
			break;
		}
		p->p->hrefs = array;
		sz = strlen(p->buf.buf);
		len = sz + 1;
		p->p->hrefs[p->p->hrefsz] = calloc(len, 1);
		if (NULL == p->p->hrefs[p->p->hrefsz]) {
			caldav_err(p, "memory exhausted");
			break;
		}
		p->p->hrefsz++;
		for (i = j = 0; i < sz; i++, j++) {
			c = p->buf.buf[i];
			if ('+' == c) {
				p->p->hrefs[p->p->hrefsz - 1][j] = ' ';
				continue;
			} else if ('%' != c) {
				p->p->hrefs[p->p->hrefsz - 1][j] = c;
				continue;
			}

			if ('\0' == p->buf.buf[i + 1] ||
			    '\0' == p->buf.buf[i + 2] ||
			    !isalnum((unsigned char)p->buf.buf[i + 1]) ||
			    !isalnum((unsigned char)p->buf.buf[i + 2])) {
				caldav_err(p, "bad percent-encoding");
				break;
			}

			p->p->hrefs[p->p->hrefsz - 1][j] = 
				parsehex(p->buf.buf[i + 1]) << 4 |
				parsehex(p->buf.buf[i + 2]);
			i += 2;
		}

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
	case CALELEM__MAX:
		break;
	case CALELEM_PROP:
		XML_SetElementHandler(p->xp, parseopen, parseclose);
		break;
	default:
		if (CALPROP__MAX == calprops[elem])
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
	if (!bufappend(&p->buf, s, (size_t)len))
		caldav_err(p, "memory exhausted");
}

static void
propopen(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	*p = dat;
	enum calelem	 elem;

	switch ((elem = calelem_find(s))) {
	case CALELEM__MAX:
		propadd(p, s, CALPROP__MAX, NULL);
		break;
	default:
		if (CALPROP__MAX == calprops[elem]) {
			propadd(p, s, CALPROP__MAX, NULL);
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

	switch (calelem_find(s)) {
	case CALELEM_CALENDAR_QUERY:
		caldav_alloc(p, CALREQTYPE_CALQUERY);
		break;
	case CALELEM_CALENDAR_MULTIGET:
		caldav_alloc(p, CALREQTYPE_CALMULTIGET);
		break;
	case CALELEM_PROPERTYUPDATE:
		caldav_alloc(p, CALREQTYPE_PROPERTYUPDATE);
		break;
	case CALELEM_PROPFIND:
		caldav_alloc(p, CALREQTYPE_PROPFIND);
		break;
	case CALELEM_HREF:
		p->buf.sz = 0;
		XML_SetDefaultHandler(p->xp, parsebuffer);
		break;
	case CALELEM_PROP:
		XML_SetElementHandler(p->xp, propopen, propclose);
		break;
	default:
		break;
	}
}

struct caldav *
caldav_parse(const char *buf, size_t sz, char **er)
{
	struct parse	 p;

	if (er != NULL)
		*er = NULL;

	memset(&p, 0, sizeof(struct parse));

	p.er = er;
	if ((p.xp = XML_ParserCreateNS(NULL, ':')) == NULL)
		return NULL;

	XML_SetDefaultHandler(p.xp, NULL);
	XML_SetElementHandler(p.xp, parseopen, parseclose);
	XML_SetCdataSectionHandler(p.xp, cdata, cdata);
	XML_SetUserData(p.xp, &p);

	if (!bufappend(&p.buf, " ", 1)) {
		caldav_err(&p, "memory exhausted");
		caldav_free(p.p);
		p.p = NULL;
	} else if (XML_Parse(p.xp, buf, (int)sz, 1) != XML_STATUS_OK) {
		caldav_err(&p, "%s", 
			XML_ErrorString
			(XML_GetErrorCode(p.xp)));
		caldav_free(p.p);
		p.p = NULL;
	}

	XML_ParserFree(p.xp);
	free(p.buf.buf);
	return p.p;
}

