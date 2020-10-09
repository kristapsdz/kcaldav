/*	$Id$ */
/*
 * Copyright (c) 2015, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libkcaldav.h"

enum	icaldatet {
	ICAL_DT_DATETIMEUTC,
	ICAL_DT_DATETIME,
	ICAL_DT_DATE,
	ICAL_DT__MAX
};

/*
 * This is a misnomer: it should be "icalcomponents".
 * These are all possible iCalendar components (see RFC 2445, 4.6).
 */
const char *const icaltypes[ICALTYPE__MAX] = {
	"VCALENDAR", /* ICALTYPE_VCALENDAR */
	"VEVENT", /* ICALTYPE_VEVENT */
	"VTODO", /* ICALTYPE_VTODO */
	"VJOURNAL", /* ICALTYPE_VJOURNAL */
	"VFREEBUSY", /* ICALTYPE_FVREEBUSY */
	"VTIMEZONE", /* ICALTYPE_VTIMEZONE */
	"VALARM", /* ICALTYPE_VALARM */
};

/*
 * These are the subcomponents of a VTIMEZONE (RFC 2445, 4.6.5).
 */
const char *const icaltztypes[ICALTZ__MAX] = {
	"DAYLIGHT", /* ICALTZ_DAYLIGHT */
	"STANDARD", /* ICALTZ_STANDARD */
};

/*
 * List of weekdays (RFC 2445, 4.3.10, "weekday").
 */
const char *const icalwkdays[ICALWKDAY__MAX] = {
	NULL, /* ICALWKDAY_NONE */
	"SU", /* ICALWKDAY_SUN */
	"MO", /* ICALWKDAY_MON */
	"TU", /* ICALWKDAY_TUES */
	"WE", /* ICALWKDAY_WED */
	"TH", /* ICALWKDAY_THUR */
	"FR", /* ICALWKDAY_FRI */
	"SA", /* ICALWKDAY_SAT */
};

/*
 * List of recurrence frequencies (RFC 2445, 4.3.10).
 */
const char *const icalfreqs[ICALFREQ__MAX] = {
	NULL, /* ICALFREQ_NONE */
	"SECONDLY", /* ICALFREQ_SECONDLY */
	"MINUTELY", /* ICALFREQ_MINUTELY */
	"HOURLY", /* ICALFREQ_HOURLY */
	"DAILY", /* ICALFREQ_DAILY */
	"WEEKLY", /* ICALFREQ_WEEKLY */
	"MONTHLY", /* ICALFREQ_MONTHLY */
	"YEARLY", /* ICALFREQ_YEARLY */
};

struct	buf {
	char		*buf;
	size_t		 sz;
	size_t		 max;
};

/*
 * This structure manages the parse sequence, either from a real file or
 * just an anonymous input buffer.
 * The (binary) buffer itself is in "cp", of length "sz".
 */
struct	icalparse {
	const char	*file; /* the filename or <buffer> */
	size_t		 sz; /* length of input */
	const char	*cp; /* CRLF input itself */
	struct buf	 buf; /* current parse buffer */
	size_t		 pos; /* position in "cp" */
	size_t		 line; /* line in "file" */
	struct ical	*ical; /* result structure */
	struct icalnode	*cur; /* current node parse */
	struct icalcomp	*comps[ICALTYPE__MAX]; /* current comps */
};

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

static void
ical_err(char **er, const char *file, size_t line, const char *fmt, ...)
{
	int	 c;
	char	*buf;
	va_list	 ap;

	if (er == NULL || *er != NULL)
		return;

	*er = NULL;

	va_start(ap, fmt);
	c = vasprintf(&buf, fmt, ap);
	va_end(ap);

	if (c < 0)
		return;

	c = line > 0 ?
		asprintf(er, "%s:%zu: %s", file, line, buf) :
		asprintf(er, "%s: %s", file, buf);

	free(buf);
	if (c < 0)
		*er = NULL;
}

/*
 * Free the icalnode "p" and, if "first" is zero, all of its siblings in
 * the chain of nodes.
 */
static void
icalnode_free(struct icalnode *p, int first)
{
	struct icalnode	*np;

	if (p == NULL)
		return;

	np = p->next;
	free(p->name);
	free(p->param);
	free(p->val);
	free(p);

	if (first == 0)
		icalnode_free(np, 0);
}

static void
icalrrule_free(struct icalrrule *p)
{

	free(p->bsec);
	free(p->byrd);
	free(p->bmon);
	free(p->bmnd);
	free(p->bmin);
	free(p->bhr);
	free(p->bwkn);
	free(p->bwkd);
	free(p->bsp);
}

/*
 * Free the entire iCalendar parsed structure.
 */
void
ical_free(struct ical *p)
{
	struct icalcomp	*c;
	size_t		 i, j;

	if (p == NULL)
		return;

	for (i = 0; i < ICALTYPE__MAX; i++) {
		while ((c = p->comps[i]) != NULL) {
			p->comps[i] = c->next;
			icalrrule_free(&c->rrule);
			for (j = 0; j < c->tzsz; j++)
				icalrrule_free(&c->tzs[j].rrule);
			free(c->dtstart.tzstr);
			free(c->dtend.tzstr);
			free(c->tzs);
			free(c);
		}
	}
	
	icalnode_free(p->first, 0);
	free(p);
}

static enum icaldatet
ical_datetime_date(const struct icalparse *p,
	struct tm *tm, const char *cp)
{
	unsigned int	day, mon, yr;

	if (sscanf(cp, "%4u%2u%2u", &yr, &mon, &day) != 3)
		return ICAL_DT__MAX;

	tm->tm_year = yr - 1900;
	tm->tm_mon = mon - 1;
	tm->tm_mday = day;
	return ICAL_DT_DATE;
}

static enum icaldatet
ical_datetime_datetime_utc(const struct icalparse *p,
	struct tm *tm, const char *cp)
{
	unsigned int	 day, mon, yr, hr, min, sec;
	char		 t, z;

	if (sscanf(cp, "%4u%2u%2u%c%2u%2u%2u%c",
	    &yr, &mon, &day, &t, &hr, &min, &sec, &z) != 8)
		return ICAL_DT__MAX;
	else if (t != 'T' || z != 'Z')
		return ICAL_DT__MAX;

	tm->tm_year = yr - 1900;
	tm->tm_mon = mon - 1;
	tm->tm_mday = day;
	tm->tm_hour = hr;
	tm->tm_min = min;
	tm->tm_sec = sec;
	return ICAL_DT_DATETIMEUTC;
}

static enum icaldatet
ical_datetime_datetime(const struct icalparse *p,
	struct tm *tm, const char *cp)
{
	unsigned int	 day, mon, yr, hr, min, sec;
	char		 t;

	if (sscanf(cp, "%4u%2u%2u%c%2u%2u%2u",
	    &yr, &mon, &day, &t, &hr, &min, &sec) != 7)
		return ICAL_DT__MAX;
	else if (t != 'T')
		return ICAL_DT__MAX;

	tm->tm_year = yr - 1900;
	tm->tm_mon = mon - 1;
	tm->tm_mday = day;
	tm->tm_hour = hr;
	tm->tm_min = min;
	tm->tm_sec = sec;
	return ICAL_DT_DATETIME;
}

/*
 * Try to parse the numeric date and time (assumed UTC) into the "tm"
 * value, seconds since epoch.
 * The date and time must be in one of the following formats:
 *  YYYYMMDD
 *  YYYYMMDD 'T' HHMMSS
 *  YYYYMMDD 'T' HHMMSS 'Z'
 * Return the icaldatet or ICAL_DT__MAX on failure.
 */
static enum icaldatet
ical_datetime(const struct icalparse *p,
	struct icaltm *tm, const char *cp, char **er)
{
	struct tm	 tmm;
	size_t 		 cplen = strlen(cp);
	enum icaldatet	 dt = ICAL_DT__MAX;

	memset(&tmm, 0, sizeof(struct tm));

	/*
	 * Don't use strptime(3) here because some systems (e.g., some
	 * Solaris derivatives) don't properly handle dates prior to
	 * 1900.  Since many time zones started prior to that, manually
	 * convert here to a "struct tm" and use mktime(3) to convert.
	 */

	if (cplen == 16)
		dt = ical_datetime_datetime_utc(p, &tmm, cp);
	else if (cplen == 15)
		dt = ical_datetime_datetime(p, &tmm, cp);
	else if (cplen == 8)
		dt = ical_datetime_date(p, &tmm, cp);

	if (dt == ICAL_DT__MAX) {
		ical_err(er, p->file, p->line, "bad date/date-time");
		return dt;
	}

	tm->set = 1;
	tm->tm = mktime(&tmm);
	return dt;
}

/*
 * Parse a date and time, possibly requiring us to dig through the
 * time-zone database and adjust the time.
 * The only important features here are the TZID, as we'll interpret the
 * date and/or time based upon the time format, not the VALUE statement.
 * Sets "tm" to be an epoch time, if applicable.
 * Returns 0 on failure and 1 on success.
 */
static int
ical_tzdatetime(struct icalparse *p, struct icaltime *tm,
	const struct icalnode *np, char **er)
{
	const char	*start, *end, *nstart;
	size_t		 len;
	enum icaldatet	 dtret;

	memset(tm, 0, sizeof(struct icaltime));

	/* First, let's parse the raw date and time. */

	dtret = ical_datetime(p, &tm->time, np->val, er);
	if (dtret == ICAL_DT__MAX)
		return 0;

	/* No timezone. */

	if ((start = np->param) == NULL)
		return 1;

	/* 
	 * Scan through the parameters looking for a timezone.
	 * Make sure that the value matches the value type.
	 * Ignore other parameters, which are technically allowed by the
	 * RFC but we don't recognise.
	 */

	while (*start != '\0') {
		end = start;
		while (*end != '\0' && *end != ';')
			end++;

		nstart = *end == '\0' ? end : end + 1;

		/* Empty statement. */

		if (end == start) {
			assert(*end != '\0');
			start = nstart;
			continue;
		}

		assert(end > start);
		len = (size_t)(end - start);

		/* Let these pass through unmolested. */

		if (len == 15 &&
		    strncasecmp(start, "VALUE=DATE-TIME", 15) == 0) {
			if (dtret == ICAL_DT_DATE) {
				ical_err(er, p->file, p->line,
					"expected date-time "
					"but found date");
				return 0;
			}
			start = nstart;
			continue;
		}
		if (len == 10 &&
		    strncasecmp(start, "VALUE=DATE", 10) == 0) {
			if (dtret != ICAL_DT_DATE) {
				ical_err(er, p->file, p->line,
					"expected date"
					"but found date-time");
				return 0;
			}
			start = nstart;
			continue;
		}

		/* Ignore unexpected parameters. */

		if (len < 6 ||
		    strncasecmp(start, "TZID=", 5)) {
			start = nstart;
			continue;
		}
		if (dtret == ICAL_DT_DATETIMEUTC) {
			ical_err(er, p->file, p->line,
				"TZID is incompatible with "
				"UTC designator in date-time");
			return 0;
		}

		/*
		 * The RFC doesn't impose any ordering on components, so
		 * we may parse a time with a timezone before we parse
		 * the timezone.  Save the timezone and we'll look it up
		 * later.
		 */

		if (tm->tzstr != NULL) {
			ical_err(er, p->file, p->line, "duplicate TZID");
			return 0;
		}

		/* Unquote strings. */

		if (len && start[5] == '"' && start[len - 1] == '"') {
			start++;
			len -= 2;
		}

		if ((tm->tzstr = strndup(start + 5, len - 5)) == NULL)
			return 0;

		start = nstart;
	}

	return 1;
}

/*
 * Wrapper ensuring ical_datetime() is ICAL_DT_DATETIME.
 * Return zero on failure, non-zero on success.
 */
static int
ical_localdatetime(const struct icalparse *p,
	struct icaltm *v, const char *cp, char **er)
{
	enum icaldatet	 dt;

	memset(v, 0, sizeof(struct icaltm));

	dt = ical_datetime(p, v, cp, er);
	if (dt != ICAL_DT__MAX && dt != ICAL_DT_DATETIME)
		ical_err(er, p->file, p->line, "bad local date-time");

	return dt == ICAL_DT_DATETIME;
}

/*
 * Wrapper ensuring ical_datetime() is ICAL_DT_DATETIMEUTC.
 * Return zero on failure, non-zero on success.
 */
static int
ical_utcdatetime(const struct icalparse *p,
	struct icaltm *v, const char *cp, char **er)
{
	enum icaldatet	 dt;

	memset(v, 0, sizeof(struct icaltm));

	dt = ical_datetime(p, v, cp, er);
	if (dt != ICAL_DT__MAX && dt != ICAL_DT_DATETIMEUTC)
		ical_err(er, p->file, p->line, "bad UTC date-time");

	return dt == ICAL_DT_DATETIMEUTC;
}

/*
 * Parse a single signed, bounded integer.
 * Returns zero on success, non-zero on failure.
 */
static int
ical_long(const struct icalparse *p, long *v, 
	const char *cp, long min, long max, char **er)
{
	char	*ep;

	*v = strtol(cp, &ep, 10);

	if (cp == ep || *ep != '\0')
		ical_err(er, p->file, p->line, "bad long");
	else if ((*v == LONG_MAX && errno == ERANGE))
		ical_err(er, p->file, p->line, "bad long");
	else if ((*v == LONG_MIN && errno == ERANGE))
		ical_err(er, p->file, p->line, "bad long");
	else if (*v < min || *v > max) 
		ical_err(er, p->file, p->line, "bad long");
	else
		return 1;

	*v = 0;
	return 0;
}

static int
ical_wkday(const struct icalparse *p, 
	enum icalwkday *v, const char *cp, char **er)
{

	for (*v = 1; *v < ICALWKDAY__MAX; (*v)++) 
		if (strcmp(icalwkdays[*v], cp) == 0)
			break;

	if (*v == ICALWKDAY__MAX)
		ical_err(er, p->file, p->line, "unknown weekday");

	return *v != ICALWKDAY__MAX;
}

/*
 * Convert a week/day designation (RFC2445, 4.3.10, weekdaynum).
 * This is a signed integer followed by a weekday.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_wk(const struct icalparse *p, 
	struct icalwk *v, const char *cp, char **er)
{
	int	 sign;

	memset(v, 0, sizeof(struct icalwk));

	sign = 0;
	if (cp[0] == '-') {
		sign = 1;
		cp++;
	} else if (cp[0] == '+')
		cp++;

	if (isdigit((unsigned char)cp[0]) &&
	    isdigit((unsigned char)cp[1])) {
		v->wk += 10 * (cp[0] - 48);
		v->wk += 1 * (cp[1] - 48);
		v->wk *= sign ? -1 : 1;
		cp += 2;
	} else if (isdigit((unsigned char)cp[0])) {
		v->wk += 1 * (cp[0] - 48);
		v->wk *= sign ? -1 : 1;
		cp += 1;
	} 

	return ical_wkday(p, &v->wkday, cp, er);
}

/*
 * Convert a week/day list (RFC2445, 4.3.10, bywdaylist).
 * Returns zero on failure, non-zero on success.
 */
static int
ical_wklist(const struct icalparse *p, struct icalwk **v,
	size_t *vsz, char *cp, char **er)
{
	char		*string = cp, *tok;
	struct icalwk	 wk;
	void		*pp;

	while ((tok = strsep(&string, ",")) != NULL) {
		if (!ical_wk(p, &wk, tok, er))
			return 0;
		pp = reallocarray(*v, 
			*vsz + 1, sizeof(struct icalwk));
		if (pp == NULL)
			return 0;
		*v = pp;
		(*v)[*vsz] = wk;
		(*vsz)++;
	}
	
	return 1;
}

/*
 * Parse a comma-separated list of bounded signed integers.
 * Returns zero on success, non-zero on failure.
 */
static int
ical_llong(const struct icalparse *p, long **v, 
	size_t *vsz, char *cp, long min, long max, char **er)
{
	char	*string = cp, *tok;
	long	 lval;
	void	*pp;

	while ((tok = strsep(&string, ",")) != NULL) {
		if (!ical_long(p, &lval, tok, min, max, er))
			return 0;
		pp = reallocarray(*v, 
			*vsz + 1, sizeof(long));
		if (pp == NULL)
			return 0;
		*v = pp;
		(*v)[*vsz] = lval;
		(*vsz)++;
	}
	
	return 1;
}

/*
 * Parse a single unsigned integer.
 * Return zero on failure, non-zero on success.
 */
static int
ical_ulong(const struct icalparse *p, unsigned long *v, const char *cp,
	unsigned long min, unsigned long max, char **er)
{
	char	*ep;

	*v = strtoul(cp, &ep, 10);

	if (cp == ep || *ep != '\0')
		ical_err(er, p->file, p->line, "bad unsigned long");
	else if ((*v == ULONG_MAX && errno == ERANGE))
		ical_err(er, p->file, p->line, "bad unsigned long");
	else if (*v < min || *v > max) 
		ical_err(er, p->file, p->line, "bad unsigned long");
	else
		return 1;

	*v = 0;
	return 0;
}

/*
 * Parse a comma-separated list of unsigned integers.
 * Return zero on failure, non-zero on success.
 */
static int
ical_lulong(const struct icalparse *p, unsigned long **v, 
	size_t *vsz, char *cp, unsigned long min, unsigned long max,
	char **er)
{
	char		*string = cp, *tok;
	unsigned long	 lval;
	void		*pp;

	while ((tok = strsep(&string, ",")) != NULL) {
		if (!ical_ulong(p, &lval, tok, min, max, er))
			return 0;
		pp = reallocarray(*v, 
			*vsz + 1, sizeof(unsigned long));
		if (pp == NULL)
			return 0;
		*v = pp;
		(*v)[*vsz] = lval;
		(*vsz)++;
	}
	
	return 1;
}

/*
 * Parse a single parameter of an RRULE, RFC 2445, 4.3.10.
 * If "in_tz" is set, this is being called within a DAYLIGHT or STANDARD
 * time-zone specification, which has slightly different syntax.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_rrule_param(const struct icalparse *p, struct icalrrule *vp,
	const char *key, char *v, int in_tz, char **er)
{
	enum icaldatet	 rv;

	if (strcmp(key, "FREQ") == 0) {
		vp->freq = ICALFREQ_NONE + 1; 
		for ( ; vp->freq < ICALFREQ__MAX; vp->freq++)
			if (strcmp(icalfreqs[vp->freq], v) == 0)
				return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"FREQ\"");
	} else if (strcmp(key, "UNTIL") == 0) {
		rv = ical_datetime(p, &vp->until, v, er);
		/* See RFC 5545, p. 66. */
		if (rv != ICAL_DT__MAX && 
		    (!in_tz || rv == ICAL_DT_DATETIMEUTC))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"UNTIL\"");
	} else if (strcmp(key, "COUNT") == 0) {
		if (ical_ulong(p, &vp->count, v, 0, ULONG_MAX, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"COUNT\"");
	} else if (strcmp(key, "INTERVAL") == 0) {
		if (ical_ulong(p, &vp->interval, v, 0, ULONG_MAX, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"INTERVAL\"");
	} else if (strcmp(key, "BYDAY") == 0) {
		if (ical_wklist(p, &vp->bwkd, &vp->bwkdsz, v, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYDAY\"");
	} else if (strcmp(key, "BYHOUR") == 0) {
		if (ical_lulong(p, &vp->bhr, &vp->bhrsz, v, 0, 23, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYHOUR\"");
	} else if (strcmp(key, "BYMINUTE") == 0) {
		if (ical_lulong(p, &vp->bmin, &vp->bminsz, v, 0, 59, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYMINUTE\"");
	} else if (strcmp(key, "BYMONTHDAY") == 0) {
		if (ical_llong(p, &vp->bmnd, &vp->bmndsz, v, 1, 31, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYMONTHDAY\"");
	} else if (strcmp(key, "BYMONTH") == 0) {
		if (ical_lulong(p, &vp->bmon, &vp->bmonsz, v, 1, 12, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYMONTH\"");
	} else if (strcmp(key, "BYSECOND") == 0) {
		if (ical_lulong(p, &vp->bsec, &vp->bsecsz, v, 1, 59, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYSECOND\"");
	} else if (strcmp(key, "BYSETPOS") == 0) {
		if (ical_llong(p, &vp->bsp, &vp->bspsz, v, -366, 366, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYSETPOS\"");
	} else if (strcmp(key, "BYWEEKNO") == 0) {
		if (ical_llong(p, &vp->bwkn, &vp->bwknsz, v, 1, 53, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYWEEKNO\"");
	} else if (strcmp(key, "BYYEARDAY") == 0) {
		if (ical_llong(p, &vp->byrd, &vp->byrdsz, v, 1, 366, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"BYYEARDAY\"");
	} else if (strcmp(key, "WKST") == 0) {
		if (ical_wkday(p, &vp->wkst, v, er))
			return 1;
		ical_err(er, p->file, p->line, 
			"malformed \"WKST\"");
	} else
		ical_err(er, p->file, p->line, "unknown property");

	ical_err(er, p->file, p->line, "malformed \"RRULE\"");
	return 0;
}

/*
 * Parse a full repeat-rule.
 * If "in_tz" is set, this is being called within a DAYLIGHT or STANDARD
 * time-zone specification, which has slightly different syntax.
 * This returns zero on failure and non-zero on success.
 */
static int
ical_rrule(const struct icalparse *p, struct icalrrule *vp,
	const char *cp, int in_tz, char **er)
{
	char	 *tofree, *string, *key, *v;

	if ((tofree = strdup(cp)) == NULL)
		return 0;

	vp->set = 1;

	string = tofree;
	while ((key = strsep(&string, ";")) != NULL) {
		if ((v = strchr(key, '=')) == NULL) {
			ical_err(er, p->file, p->line,
				"bad \"RRULE\" syntax");
			break;
		}
		*v++ = '\0';
		if (!ical_rrule_param(p, vp, key, v, in_tz, er))
			break;
	}

	free(tofree);

	/* We need only a frequency. */

	if (key == NULL &&
	    (vp->freq == ICALFREQ_NONE || vp->freq == ICALFREQ__MAX)) {
		ical_err(er, p->file, p->line, "missing \"FREQ\"");
		return 0;
	}

	return key == NULL;
}

/*
 * Parse a DURATION (RFC 2445, 4.3.6).
 * Note that we're careful to only parse non-empty durations.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_duration(const struct icalparse *p,
	struct icaldur *v, char *cp, char **er)
{
	char	*start;
	char	 type;

	memset(v, 0, sizeof(struct icaldur));

	if ('P' != cp[0]) {
		ical_err(er, p->file, p->line, "bad duration");
		return 0;
	}
	cp++;

	/* Process the sign. */

	v->sign = 1;

	if ('-' == cp[0]) {
		v->sign = -1;
		cp++;
	} else if ('+' == cp[0])
		cp++;

	if ('\0' == *cp) {
		v->sign = 0;
		ical_err(er, p->file, p->line, "empty duration");
		return 0;
	}

	while ('\0' != *cp) {
		if ('T' == *cp) {
			if ('\0' == cp[1])
				break;
			cp++;
			continue;
		}
		start = cp;
		while (isdigit((unsigned char)*cp)) 
			cp++;
		if ('\0' == *cp)
			break;

		type = *cp;
		*cp++ = '\0';
		switch (type) {
		case ('D'):
			if (ical_ulong(p, &v->day, start, 0, ULONG_MAX, er))
				continue;
			break;
		case ('W'):
			if (ical_ulong(p, &v->week, start, 0, ULONG_MAX, er))
				continue;
			break;
		case ('H'):
			if (ical_ulong(p, &v->hour, start, 0, ULONG_MAX, er))
				continue;
			break;
		case ('M'):
			if (ical_ulong(p, &v->min, start, 0, ULONG_MAX, er))
				continue;
			break;
		case ('S'):
			if (ical_ulong(p, &v->sec, start, 0, ULONG_MAX, er))
				continue;
			break;
		default:
			break;
		}
		break;
	}

	if ('\0' == *cp)
		return 1;

	ical_err(er, p->file, p->line, "bad duration");
	return 0;
}

/*
 * Try to convert "cp" into a UTC-offset string.
 * Return zero on failure, non-zero on success.
 */
static int
ical_utc_offs(const struct icalparse *p,
	int *v, const char *cp, char **er)
{
	size_t	 i, sz;
	int	 min = 0, hour = 0, sec = 0;

	*v = 0;

	/* Must be (+-)HHMM[SS]. */

	if (5 != (sz = strlen(cp)) && 7 != sz) {
		ical_err(er, p->file, p->line, "bad UTC-offset size");
		return 0;
	}

	for (i = 1; i < sz; i++)
		if (!isdigit((unsigned char)cp[i])) {
			ical_err(er, p->file, p->line, 
				"non-digit UTC-offset character");
			return 0;
		}

	/* Check sign extension. */

	if ('-' != cp[0] && '+' != cp[0]) {
		ical_err(er, p->file, p->line, 
			"bad UTC-offset sign extension");
		return 0;
	}

	/* Sanitise hour. */

	hour += 10 * (cp[1] - 48);
	hour += 1 * (cp[2] - 48);

	if (hour >= 24) {
		ical_err(er, p->file, p->line, "bad hour: %d", hour);
		return 0;
	}

	/* Sanitise minute. */

	min += 10 * (cp[3] - 48);
	min += 1 * (cp[4] - 48);

	if (min >= 60) {
		ical_err(er, p->file, p->line, "bad minute: %d", min);
		return 0;
	}

	/* See if we should have a second component and sanitise. */

	if ('\0' != cp[5]) {
		sec += 10 * (cp[5] - 48);
		sec += 1 * (cp[6] - 48);
		if (sec >= 60) {
			ical_err(er, p->file, p->line, 
				"bad second: %d", sec);
			return 0;
		}
	}

	/* Sign-extend. */

	*v = sec + min * 60 + hour * 3600;

	if ('-' == cp[0])
		*v *= -1;

	return 1;
}

/*
 * Parse a non zero-length string from the iCalendar.
 * The string can have arbitrary characters.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_string(const struct icalparse *p, 
	const char **v, const char *cp, char **er)
{

	*v = NULL;
	if (cp == NULL || *cp == '\0') {
		ical_err(er, p->file, p->line, "zero-length string");
		return 0;
	}
	*v = cp;
	return 1;
}

/*
 * Parse a line out of it the iCalendar file into its name (key), value,
 * and optional (so NULL) subsequent parameter parts.
 * This handles CRLF lines, LF lines, and continuations.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_line(struct icalparse *p, char **name,
	char **param, char **value, char **er)
{
	const char	*end;
	size_t		 len, skip;

	p->buf.sz = len = 0;

	/*
	 * Scan til the end of the line (or EOF) and copy those bytes
	 * into our line buffer (p->buf).
	 * We want to handle both CRLF, which is standards-compliant,
	 * and regular LF, which isn't.
	 * We also need to handle continuation lines where the
	 * subsequent line begins with whitespace, in which case we need
	 * to join it to the current line.
	 */

	while (p->pos < p->sz) {
		end = memchr(&p->cp[p->pos], '\n', p->sz - p->pos);
		p->line++;

		/* If we're at the EOF, copy everything remaining. */

		if (end == NULL) {
			if (!bufappend(&p->buf, 
		            &p->cp[p->pos], p->sz - p->pos))
				return 0;
			p->pos = p->sz;
			break;
		}

		/* Switch on whether we have CRLF/LF. */

		len = end - &p->cp[p->pos];
		if (len && end[-1] == '\r') {
			len--;
			end--;
			skip = 2;
		} else
			skip = 1;

		if (!bufappend(&p->buf, &p->cp[p->pos], len))
			return 0;
		p->pos += len + skip;

		/* Next line does not start with a continuation. */

		if (p->pos < p->sz &&
		    p->cp[p->pos] != ' ' &&
		    p->cp[p->pos] != '\t')
			break;

		/* Skip after the continuation. */

		if (p->pos < p->sz)
			p->pos++;
	}

	assert(p->pos <= p->sz);

	if ((*name = p->buf.buf) == NULL) {
		ical_err(er, p->file, p->line, "empty line");
		return 0;
	} else if ((*value = strchr(*name, ':')) == NULL) { 
		ical_err(er, p->file, p->line, "no value for line");
		return 0;
	} else
		*(*value)++ = '\0';

	if ((*param = strchr(*name, ';')) != NULL) 
		*(*param)++ = '\0';

	return 1;
}

/*
 * Allocate a node onto the queue of currently-allocated nodes for this
 * parse.
 * Return the newly-created node or NULL on allocation failure.
 */
static struct icalnode *
icalnode_alloc(struct icalparse *p, const char *name, 
	const char *val, const char *param, char **er)
{
	struct icalnode	*np;

	assert(NULL != name);
	assert(NULL != val);

	if ((np = calloc(1, sizeof(struct icalnode))) == NULL)
		goto err;
	if ((np->name = strdup(name)) == NULL)
		goto err;
	if ((np->val = strdup(val)) == NULL)
		goto err;
	if (param != NULL && (np->param = strdup(param)) == NULL)
		goto err;

	/* Enqueue the iCalendar node. */

	if (p->cur == NULL) {
		assert(p->ical->first == NULL);
		p->cur = p->ical->first = np;
	} else {
		assert(p->ical->first != NULL);
		p->cur->next = np;
		p->cur = np;
	}

	return np;
err:
	if (*er != NULL)
		*er = NULL;
	icalnode_free(np, 0);
	return NULL;
}

/*
 * Fully parse a DAYLIGHT or STANDARD time component as specified by the
 * input "type" into the current calendar.
 * This requires that a timezone be set!
 * Returns zero on failure, non-zero on success.
 */
static int
ical_parsetz(struct icalparse *p, enum icaltztype type, char **er)
{
	struct icaltz	*c;
	char		*name, *val, *param;
	int		 rc;
	struct icalnode	*np;
	void		*pp;

	if (p->comps[ICALTYPE_VTIMEZONE] == NULL) {
		ical_err(er, p->file, p->line, "repeat \"TIMEZONE\"");
		return 0;
	}

	/*
	 * Re-allocate the per-timezone list of daytime and standard
	 * time objects (not really components).
	 */

	pp = reallocarray
		(p->comps[ICALTYPE_VTIMEZONE]->tzs,
		 p->comps[ICALTYPE_VTIMEZONE]->tzsz + 1,
		 sizeof(struct icaltz));
	if (NULL == pp)
		return 0;

	p->comps[ICALTYPE_VTIMEZONE]->tzs = pp;
	c = &p->comps[ICALTYPE_VTIMEZONE]->tzs
		[p->comps[ICALTYPE_VTIMEZONE]->tzsz];
	p->comps[ICALTYPE_VTIMEZONE]->tzsz++;

	memset(c, 0, sizeof(struct icaltz));
	c->type = type;

	while (p->pos < p->sz) {
		if (!ical_line(p, &name, &param, &val, er))
			return 0;
		np = icalnode_alloc(p, name, val, param, er);
		if (np == NULL)
			return 0;

		if (strcasecmp("END", name) == 0) {
			if (strcasecmp(icaltztypes[type], val) == 0)
				break;
			continue;
		}

		/* The "dtstart" must be local: RFC 5545, p. 65. */

		rc = 1;
		if (strcasecmp(name, "dtstart") == 0)
			rc = ical_localdatetime(p, &c->dtstart, np->val, er);
		else if (strcasecmp(name, "tzoffsetfrom") == 0)
			rc = ical_utc_offs(p, &c->tzfrom, np->val, er);
		else if (strcasecmp(name, "tzoffsetto") == 0)
			rc = ical_utc_offs(p, &c->tzto, np->val, er);
		else if (strcasecmp(name, "rrule") == 0)
			rc = ical_rrule(p, &c->rrule, np->val, 1, er);

		if (!rc)
			return 0;
	}

	return 1;
}

/*
 * Fully parse an individual component, such as a VCALENDAR, VEVENT,
 * or VTIMEZONE, and any components it may contain.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_parsecomp(struct icalparse *p, enum icaltype type, char **er)
{
	struct icalcomp	*c;
	char		*name, *val, *param;
	int		 rc;
	struct icalnode	*np;
	enum icaltype	 tt;
	enum icaltztype	 tz;
	size_t		 line;

	if ((c = calloc(1, sizeof(struct icalcomp))) == NULL)
		return 0;

	/* Fill in the component bucket in prefix order. */

	c->type = type;

	if (p->comps[type] == NULL) {
		assert(p->ical->comps[type] == NULL);
		p->comps[type] = p->ical->comps[type] = c;
	} else {
		assert(p->ical->comps[type] != NULL);
		p->comps[type]->next = c;
		p->comps[type] = c;
	}

	p->ical->bits |= 1u << (unsigned int)type;

	line = p->line;

	while (p->pos < p->sz) {
		if (!ical_line(p, &name, &param, &val, er))
			return 0;
		np = icalnode_alloc(p, name, val, param, er);
		if (np == NULL)
			return 0;

		/* Look up in nested component. */

		if (strcasecmp("BEGIN", name) == 0) {
			for (tt = 0; tt < ICALTYPE__MAX; tt++) 
				if (strcasecmp(icaltypes[tt], val) == 0)
					break;

			if (tt < ICALTYPE__MAX && 
			    !ical_parsecomp(p, tt, er))
				return 0;
			else if (tt < ICALTYPE__MAX)
				continue;

			/* Look up in time-zone components. */

			for (tz = 0; tz < ICALTZ__MAX; tz++) 
				if (strcasecmp(icaltztypes[tz], val) == 0)
					break;
			if (tz < ICALTZ__MAX && !ical_parsetz(p, tz, er))
				return 0;

			continue;
		} else if (strcasecmp("END", name) == 0) {
			if (strcasecmp(icaltypes[type], val) == 0)
				break;
			continue;
		}

		rc = 1;
		if (strcasecmp(name, "uid") == 0)
			rc = ical_string(p, &c->uid, np->val, er);
		else if (strcasecmp(name, "created") == 0)
			rc = ical_utcdatetime(p, &c->created, np->val, er);
		else if (strcasecmp(name, "last-modified") == 0)
			rc = ical_utcdatetime(p, &c->lastmod, np->val, er);
		else if (strcasecmp(name, "dtstamp") == 0)
			rc = ical_utcdatetime(p, &c->dtstamp, np->val, er);
		else if (strcasecmp(name, "dtstart") == 0)
			rc = ical_tzdatetime(p, &c->dtstart, np, er);
		else if (strcasecmp(name, "dtend") == 0)
			rc = ical_tzdatetime(p, &c->dtend, np, er);
		else if (strcasecmp(name, "duration") == 0)
			rc = ical_duration(p, &c->duration, np->val, er);
		else if (strcasecmp(name, "tzid") == 0)
			rc = ical_string(p, &c->tzid, np->val, er);
		else if (strcasecmp(name, "rrule") == 0)
			rc = ical_rrule(p, &c->rrule, np->val, 0, er);

		if (!rc)
			return 0;
	}

	/* We can now run some post-processing queries. */

	switch (c->type) {
	case ICALTYPE_VEVENT:
		if (c->uid == NULL) {
			ical_err(er, p->file, line, 
				"missing \"UID\"");
			return 0;
		} else if (c->dtstart.time.tm == 0) {
			ical_err(er, p->file, line, 
				"missing \"DTSTART\"");
			return 0;
		}
		break;
	case ICALTYPE_VTIMEZONE:
		if (c->tzid == NULL) {
			ical_err(er, p->file, line, 
				"missing \"TZID\"");
			return 0;
		}
		break;
	default:
		break;
	}

	return 1;
}

/*
 * Try to look up the timezone string in "tm".
 * Returns zero on failure (timezone not found), nonzero on success.
 */
static int
ical_postparse_tz(struct icalparse *pp, struct icaltime *tm, char **er)
{

	if (tm->tzstr == NULL)
		return 1;

	for (tm->tz = pp->ical->comps[ICALTYPE_VTIMEZONE];
	     tm->tz != NULL; tm->tz = tm->tz->next)
		if (strcasecmp(tm->tz->tzid, tm->tzstr) == 0)
			return 1;

	assert(tm->tz == NULL);
	ical_err(er, pp->file, 0, 
		"timezone \"%s\" not found", tm->tzstr);
	return 0;
}

/*
 * RFC 5545 does not impose ordering constraints.
 * Link them together here.
 * At this point this is just the timezones in DTSTART and DTEND.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_postparse(struct icalparse *pp, char **er)
{
	struct icalcomp	*c;
	enum icaltype	 i;

	for (i = 0; i < ICALTYPE__MAX; i++)
		for (c = pp->ical->comps[i]; c != NULL; c = c->next) {
			if (!ical_postparse_tz(pp, &c->dtstart, er))
				return 0;
			if (!ical_postparse_tz(pp, &c->dtend, er))
				return 0;
		}

	return 1;
}

/*
 * Parse a binary buffer "cp" of size "sz" from a file (which may be
 * NULL if there's no file--it's for reporting purposes only) into a
 * well-formed iCalendar structure.
 * Returns NULL if the parse failed in any way, in which case "errstr",
 * if not NULL, is set to the error message (which must be freed).
 * If the return value is NULL and errstr is NULL, the error is memory
 * allocation failure.
 */
struct ical *
ical_parse(const char *file, const char *cp, size_t sz, char **er)
{
	struct icalparse	 pp;
	char			*name, *val, *param;
	struct ical		*p;

	if (er != NULL)
		*er = NULL;

	memset(&pp, 0, sizeof(struct icalparse));
	pp.file = file == NULL ? "<buffer>" : file;
	pp.cp = cp;
	pp.sz = sz;

	if ((pp.ical = p = calloc(1, sizeof(struct ical))) == NULL)
		return NULL;

	if (!ical_line(&pp, &name, &param, &val, er))
		goto err;
	if (icalnode_alloc(&pp, name, val, param, er) == NULL)
		goto err;

	/* RFC 5545, 3.4. */

	if (strcasecmp(name, "BEGIN")) {
		ical_err(er, pp.file, pp.line, 
			"first statement not \"BEGIN\"");
		goto err;
	} else if (strcasecmp(val, "VCALENDAR")) {
		ical_err(er, pp.file, pp.line,
			"first component not \"VCALENDAR\"");
		goto err;
	} 

	/* RFC 5545, 3.6. */
	
	if (!ical_parsecomp(&pp, ICALTYPE_VCALENDAR, er)) 
		goto err;

	/* On success, link out-of-order components. */

	if (!ical_postparse(&pp, er))
		goto err;

	free(pp.buf.buf);
	return p;
err:
	free(pp.buf.buf);
	ical_free(p);
	return NULL;
}

static int
icalnode_putc(int c, void *arg)
{
	int	 fd = *(int *)arg;
	char	 ch = c;

	return write(fd, &ch, 1) != -1;
}

/*
 * Correctly wrap iCalendar output lines.
 * We use (arbitrarily?) 74 written characters til wrapping.
 * Return zero on failure, non-zero on success.
 */
static int
icalnode_putchar(char c, size_t *col, ical_putchar fp, void *arg)
{

	if (*col == 74) {
		if (!(*fp)('\r', arg))
			return 0;
		if (!(*fp)('\n', arg))
			return 0;
		if (!(*fp)(' ', arg))
			return 0;
		*col = 1;
	}
	if (!(*fp)(c, arg))
		return 0;
	(*col)++;
	return 1;
}

/*
 * This is tricky: we need to accept multi-byte (assume UTF-8)
 * characters and not break them up.
 * To do so, safely check the number of bytes.
 * If the sequence isn't UTF-8, just print it as binary.
 * The bit-patterns are from the W3C recommendation on form validation.
 * Returns returns zero on failure, non-zero on success.
 */
static int
icalnode_puts(const unsigned char *c, 
	size_t *col, ical_putchar fp, void *arg)
{

	while (*c != '\0') {
		/* Start with printable ASCII. */

		if (c[0] == 0x09 || c[0] == 0x0A || c[0] == 0x0D || 
		    (0x20 <= c[0] && c[0] <= 0x7E)) {
			if (*col + 1 >= 74) {
				if (!(*fp)('\r', arg) ||
				    !(*fp)('\n', arg) ||
				    !(*fp)(' ', arg))
					return 0;
				*col = 1;
			}
			if (!(*fp)(c[0], arg))
				return 0;
			*col += 1;
			c += 1;
			continue;
		}

		/* Overlong 2-bytes. */

		if ((0xC2 <= c[0] && c[0] <= 0xDF) && 
		    (0x80 <= c[1] && c[1] <= 0xBF)) {
			if (*col + 2 >= 74) {
				if (!(*fp)('\r', arg) ||
				    !(*fp)('\n', arg) ||
				    !(*fp)(' ', arg))
					return 0;
				*col = 1;
			}
			if (!(*fp)(c[0], arg) ||
			    !(*fp)(c[1], arg))
				return 0;
			*col += 2;
			c += 2;
			continue;
		}

		/* No overlongs, straight 3-byte, no surrogate. */

		if ((c[0] == 0xE0 && (0xA0 <= c[1] && c[1] <= 0xBF) && 
		     (0x80 <= c[2] && c[2] <= 0xBF)) ||
		    (((0xE1 <= c[0] && c[0] <= 0xEC) || 
		      c[0] == 0xEE || c[0] == 0xEF) && 
		     (0x80 <= c[1] && c[1] <= 0xBF) && 
		     (0x80 <= c[2] && c[2] <= 0xBF)) ||
		    (c[0] == 0xED && (0x80 <= c[1] && c[1] <= 0x9F) && 
		     (0x80 <= c[2] && c[2] <= 0xBF))) {
			if (*col + 3 >= 74) {
				if (!(*fp)('\r', arg) ||
				    !(*fp)('\n', arg) ||
				    !(*fp)(' ', arg))
					return 0;
				*col = 1;
			}
			if (!(*fp)(c[0], arg) ||
			    !(*fp)(c[1], arg) ||
			    !(*fp)(c[2], arg))
				return 0;
			*col += 3;
			c += 3;
			continue;
		}

		/* All planes. */

		if ((c[0] == 0xF0 &&
		     (0x90 <= c[1] && c[1] <= 0xBF) &&
		     (0x80 <= c[2] && c[2] <= 0xBF) &&
		     (0x80 <= c[3] && c[3] <= 0xBF)) ||
		    ((0xF1 <= c[0] && c[0] <= 0xF3) &&
		     (0x80 <= c[1] && c[1] <= 0xBF) &&
		     (0x80 <= c[2] && c[2] <= 0xBF) &&
		     (0x80 <= c[3] && c[3] <= 0xBF)) ||
                    (c[0] == 0xF4 && (0x80 <= c[1] && c[1] <= 0x8F) &&
		     (0x80 <= c[2] && c[2] <= 0xBF) &&
		     (0x80 <= c[3] && c[3] <= 0xBF))) {
			if (*col + 4 >= 74) {
				if (!(*fp)('\r', arg) ||
				    !(*fp)('\n', arg) ||
				    !(*fp)(' ', arg))
					return 0;
				*col = 1;
			}
			if (!(*fp)(c[0], arg) ||
			    !(*fp)(c[1], arg) ||
			    !(*fp)(c[2], arg) ||
			    !(*fp)(c[3], arg))
				return 0;
			*col += 4;
			c += 4;
			continue;
		}

		/* Fall-through: invalid bytes. */

		if (*col + 1 >= 74) {
			if (!(*fp)('\r', arg) ||
			    !(*fp)('\n', arg) ||
			    !(*fp)(' ', arg))
				return 0;
			*col = 1;
		}
		if (!(*fp)(c[0], arg))
			return 0;
		*col += 1;
		c += 1;
	}

	return 1;
}

/*
 * Print an iCalendar using the given callback "fp".
 * Return zero on failure, non-zero on succes.
 */
static int
icalnode_print(const struct icalnode *p, ical_putchar fp, void *arg)
{
	size_t			 col = 0;
	const unsigned char	*cp;

	if (p == NULL) 
		return 1;

	cp = (const unsigned char *)p->name;
	if (!icalnode_puts(cp, &col, fp, arg))
		return 0;

	if (p->param != NULL) {
		if (!icalnode_putchar(';', &col, fp, arg))
			return 0;
		cp = (const unsigned char *)p->param;
		if (!icalnode_puts(cp, &col, fp, arg))
			return 0;
	}

	if (!icalnode_putchar(':', &col, fp, arg))
		return 0;

	cp = (const unsigned char *)p->val;
	if (!icalnode_puts(cp, &col, fp, arg))
		return 0;

	if (!(*fp)('\r', arg))
		return 0;
	if (!(*fp)('\n', arg))
		return 0;

	return icalnode_print(p->next, fp, arg);
}

/*
 * Print (write) an iCalendar using ical_putchar as a write callback.
 * The callback must return zero on failure, non-zero on success.
 * Returns zero on failure, non-zero on success.
 */
int
ical_print(const struct ical *p, ical_putchar fp, void *arg)
{

	return icalnode_print(p->first, fp, arg);
}

/*
 * Print an iCalendar directly to the given file descriptor.
 * Returns zero on failure, non-zero on success.
 */
int
ical_printfile(int fd, const struct ical *p)
{

	return icalnode_print(p->first, icalnode_putc, &fd);
}
