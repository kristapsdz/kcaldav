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

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "md5.h"

const char *const icaltypes[ICALTYPE__MAX] = {
	"VCALENDAR", /* ICALTYPE_VCALENDAR */
	"VEVENT", /* ICALTYPE_VEVENT */
	"VTODO", /* ICALTYPE_VTODO */
	"VJOURNAL", /* ICALTYPE_VJOURNAL */
	"VFREEBUSY", /* ICALTYPE_FVREEBUSY */
	"VTIMEZONE", /* ICALTYPE_VTIMEZONE */
	"VALARM", /* ICALTYPE_VALARM */
};

const char *const icaltztypes[ICALTZ__MAX] = {
	"DAYLIGHT", /* ICALTZ_DAYLIGHT */
	"STANDARD", /* ICALTZ_STANDARD */
};

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

/*
 * Free the icalnode "p" and, if "first" is zero, all of its siblings in
 * the chain of nodes.
 */
static void
icalnode_free(struct icalnode *p, int first)
{
	struct icalnode	*np;

	if (NULL == p)
		return;

	np = p->next;
	free(p->name);
	free(p->param);
	free(p->val);
	free(p);

	if (0 == first)
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

	if (NULL == p)
		return;

	for (i = 0; i < ICALTYPE__MAX; i++) {
		while (NULL != (c = p->comps[i])) {
			p->comps[i] = c->next;
			icalrrule_free(&c->rrule);
			for (j = 0; j < c->tzsz; j++)
				icalrrule_free(&c->tzs[j].rrule);
			free(c->tzs);
			free(c);
		}
	}
	
	icalnode_free(p->first, 0);
	free(p);
}

/*
 * Try to parse the numeric date and time (assumed UTC) into the "tm"
 * value, seconds since epoch.
 * Return zero on failure, non-zero on success.
 */
static int
ical_datetime(const struct icalparse *p, time_t *tm, const char *cp)
{
	size_t		 sz, i;
	unsigned 	 year, mon, mday, hour, min, sec, day;
	int		 ly;
	unsigned	 mdaysa[12] = {  0,  31,  59,  90, 
				       120, 151, 181, 212, 
				       243, 273, 304, 334 };
	unsigned	 mdayss[12] = { 31,  28,  31,  30, 
				        31,  30,  31,  31,
					30,  31,  30,  31 };

	year = mon = mday = hour = min = sec = 0;

	/* Sanity-check the date component length and ctype. */
	if ((sz = strlen(cp)) < 8) {
		kerrx("%s:%zu: invalid date size", p->file, p->line);
		return(0);
	} 
	for (i = 0; i < 8; i++)
		if ( ! isdigit(cp[i])) {
			kerrx("%s:%zu: non-digits in date "
				"string", p->file, p->line);
			return(0);
		}

	/* Sanitise the year. */
	year += 1000 * (cp[0] - 48);
	year += 100 * (cp[1] - 48);
	year += 10 * (cp[2] - 48);
	year += 1 * (cp[3] - 48);
	if (year < 1970) {
		kerrx("%s:%zu: bad year: %u", 
			p->file, p->line, year);
		return(0);
	}

	/* 
	 * Compute whether we're in a leap year.
	 * If we are, adjust our monthly entry for February.
	 */
	ly = ((year & 3) == 0 && 
		((year % 25) != 0 || (year & 15) == 0));

	if (ly) {
		mdayss[1] = 29;
		mdaysa[2] = 60;
	}

	/* Sanitise the month. */
	mon += 10 * (cp[4] - 48);
	mon += 1 * (cp[5] - 48);
	if (0 == mon || mon > 12) {
		kerrx("%s:%zu: bad month: %u", 
			p->file, p->line, mon);
		return(0);
	}

	/* Sanitise the day of month. */
	mday += 10 * (cp[6] - 48);
	mday += 1 * (cp[7] - 48);
	if (0 == mday || mday > mdayss[mon - 1]) {
		kerrx("%s:%zu: bad day: %u", 
			p->file, p->line, mday);
		return(0);
	}

	/*
	 * If the 'T' follows the date, then we also have a time string,
	 * so try to parse that as well.
	 * (Otherwise, the time component is just zero.)
	 */
	if ('T' == cp[8]) {
		/* Sanitise the length and ctype. */
		if (sz < 8 + 1 + 6) {
			kerrx("%s:%zu: bad time size", 
				p->file, p->line);
			return(0);
		}
		for (i = 0; i < 6; i++)
			if ( ! isdigit(cp[9 + i])) {
				kerrx("%s:%zu: non-digits in time "
					"string", p->file, p->line);
				return(0);
			}

		/* Sanitise the hour. */
		hour += 10 * (cp[9] - 48);
		hour += 1 * (cp[10] - 48);
		if (hour > 24) {
			kerrx("%s:%zu: bad hour: %u", 
				p->file, p->line, hour);
			return(0);
		}

		/* Sanitise the minute. */
		min += 10 * (cp[11] - 48);
		min += 1 * (cp[12] - 48);
		if (min > 60) {
			kerrx("%s:%zu: bad minute: %u", 
				p->file, p->line, min);
			return(0);
		}

		/* Sanitise the second. */
		sec += 10 * (cp[13] - 48);
		sec += 1 * (cp[14] - 48);
		if (sec > 60) {
			kerrx("%s:%zu: bad second: %u", 
				p->file, p->line, sec);
			return(0);
		}
	}

	/* The year for "struct tm" is from 1900. */
	year -= 1900;

	/* 
	 * Compute the day in the year.
	 * Note that the month and day in month both begin at one, as
	 * they were parsed that way.
	 */
	day = mdaysa[mon - 1] + (mday - 1);

	/*
	 * Now use the formula from the The Open Group, "Single Unix
	 * Specification", Base definitions (4: General concepts), part
	 * 15: Seconds since epoch.
	 */
	*tm = sec + min * 60 + hour * 3600 + 
		day * 86400 + (year - 70) * 31536000 + 
		((year - 69) / 4) * 86400 -
		((year - 1) / 100) * 86400 + 
		((year + 299) / 400) * 86400;
	return(1);
}

/*
 * Parse a date and time, possibly requiring us to dig through the
 * time-zone database and adjust the time.
 * Sets "tm" to be an epoch time.
 * Returns 0 on failure and 1 on success.
 */
static int
ical_tzdatetime(struct icalparse *p, 
	struct icaltime *tm, const struct icalnode *np)
{

	memset(tm, 0, sizeof(struct icaltime));

	/* First, let's parse the raw date and time. */

	if ( ! ical_datetime(p, &tm->time, np->val))
		return(0);

	/* 
	 * Next, see if we're UTC.
	 * Note: the "free floating" time is interpreted as UTC.
	 */
	if (NULL == np->param)
		return(1);
	else if (0 == strcasecmp(np->param, "VALUE=DATE")) 
		return(1);
	else if (0 == strcasecmp(np->param, "VALUE=DATE-TIME"))
		return(1);
	
	if (strncasecmp(np->param, "TZID=", 5)) {
		kerrx("%s:%zu: unrecognised param", p->file, p->line);
		return(0);
	}

	/* Try looking up the timezone in our database. */
	tm->tz = p->ical->comps[ICALTYPE_VTIMEZONE];
	for ( ; NULL != tm->tz; tm->tz = tm->tz->next)
		if (0 == strcasecmp(tm->tz->tzid, np->param + 5))
			break;

	if (NULL == tm->tz) {
		kerrx("%s:%zu: timezone not found", p->file, p->line);
		return(0);
	}

	return(1);
}

/*
 * Convert the string in "cp" into an epoch value.
 * Return zero on failure, non-zero on success.
 */
static int
ical_utcdatetime(const struct icalparse *p, time_t *v, const char *cp)
{
	size_t	 sz;

	*v = 0;

	if (NULL == cp || 0 == (sz = strlen(cp))) {
		kerrx("%s:%zu: zero-length UTC", p->file, p->line);
		return(0);
	} else if (16 != sz) {
		kerrx("%s:%zu: bad UTC size", p->file, p->line);
		return(0);
	} else if ('Z' != cp[sz - 1]) {
		kerrx("%s:%zu: no UTC signature", p->file, p->line);
		return(0);
	}
	return(ical_datetime(p, v, cp));
}

/*
 * Parse a single signed integer.
 * Returns zero on success, non-zero on failure.
 */
static int
ical_long(const struct icalparse *p, long *v, const char *cp)
{
	char	*ep;

	*v = strtol(cp, &ep, 10);

	if (cp == ep || *ep != '\0')
		kerrx("%s:%zu: bad long\n", p->file, p->line);
	else if ((*v == LONG_MAX && errno == ERANGE))
		kerrx("%s:%zu: bad long\n", p->file, p->line);
	else if ((*v == LONG_MIN && errno == ERANGE))
		kerrx("%s:%zu: bad long\n", p->file, p->line);
	else
		return(1);

	*v = 0;
	return(0);
}

static int
ical_wkday(const struct icalparse *p, 
	enum icalwkday *v, const char *cp)
{

	for (*v = 1; *v < ICALWKDAY__MAX; (*v)++) 
		if (0 == strcmp(icalwkdays[*v], cp))
			break;

	if (ICALWKDAY__MAX == *v) 
		kerrx("%s:%zu: unknown weekday", p->file, p->line);

	return(ICALWKDAY__MAX != *v);
}

/*
 * Convert a week/day designation (RFC2445, 4.3.10, weekdaynum).
 * This is a signed integer followed by a weekday.
 * Returns zero on failure, non-zero on success.
 */
static int
ical_wk(const struct icalparse *p, 
	struct icalwk *v, const char *cp)
{
	int	 sign;

	memset(v, 0, sizeof(struct icalwk));

	sign = 0;
	if ('-' == cp[0]) {
		sign = 1;
		cp++;
	} else if ('+' == cp[0])
		cp++;

	if (isdigit(cp[0]) && isdigit(cp[1])) {
		v->wk += 10 * (cp[0] - 48);
		v->wk += 1 * (cp[1] - 48);
		v->wk *= sign ? -1 : 1;
		cp += 2;
	} else if (isdigit(cp[0])) {
		v->wk += 1 * (cp[0] - 48);
		v->wk *= sign ? -1 : 1;
		cp += 1;
	} 

	return(ical_wkday(p, &v->wkday, cp));
}

/*
 * Convert a week/day list (RFC2445, 4.3.10, bywdaylist).
 * Returns zero on failure, non-zero on success.
 */
static int
ical_wklist(const struct icalparse *p,
	struct icalwk **v, size_t *vsz, char *cp)
{
	char		*string, *tok;
	struct icalwk	 wk;
	void		*pp;

	string = cp;
	while (NULL != (tok = strsep(&string, ","))) {
		if ( ! ical_wk(p, &wk, tok))
			return(0);
		pp = reallocarray
			(*v, *vsz + 1, sizeof(struct icalwk));
		if (NULL == p) {
			kerr(NULL);
			return(0);
		}
		*v = pp;
		(*v)[*vsz] = wk;
		(*vsz)++;
	}
	
	return(1);
}

/*
 * Parse a comma-separated list of signed integers.
 * Returns zero on success, non-zero on failure.
 */
static int
ical_longlist(const struct icalparse *p, 
	long **v, size_t *vsz, char *cp)
{
	char	*string, *tok;
	long	 lval;
	void	*pp;

	string = cp;
	while (NULL != (tok = strsep(&string, ","))) {
		if ( ! ical_long(p, &lval, tok))
			return(0);
		pp = reallocarray
			(*v, *vsz + 1, sizeof(long));
		if (NULL == p) {
			kerr(NULL);
			return(0);
		}
		*v = pp;
		(*v)[*vsz] = lval;
		(*vsz)++;
	}
	
	return(1);
}

/*
 * Parse a single unsigned integer.
 * Return zero on failure, non-zero on success.
 */
static int
ical_ulong(const struct icalparse *p, unsigned long *v, const char *cp)
{
	char	*ep;

	*v = strtoul(cp, &ep, 10);

	if (cp == ep || *ep != '\0')
		kerrx("%s:%zu: bad ulong\n", p->file, p->line);
	else if ((*v == ULONG_MAX && errno == ERANGE))
		kerrx("%s:%zu: bad ulong\n", p->file, p->line);
	else
		return(1);

	*v = 0;
	return(0);
}

/*
 * Parse a comma-separated list of unsigned integers.
 * Return zero on failure, non-zero on success.
 */
static int
ical_ulonglist(const struct icalparse *p, 
	unsigned long **v, size_t *vsz, char *cp)
{
	char		*string, *tok;
	unsigned long	 lval;
	void		*pp;

	string = cp;
	while (NULL != (tok = strsep(&string, ","))) {
		if ( ! ical_ulong(p, &lval, tok))
			return(0);
		pp = reallocarray
			(*v, *vsz + 1, sizeof(unsigned long));
		if (NULL == p) {
			kerr(NULL);
			return(0);
		}
		*v = pp;
		(*v)[*vsz] = lval;
		(*vsz)++;
	}
	
	return(1);
}

/*
 * Parse a full repeat-rule.
 * Note that this re-allocates the "cp" string for internal usage.
 * This returns zero on failure and non-zero on success.
 */
static int
ical_rrule(const struct icalparse *p, 
	struct icalrrule *vp, const char *cp)
{
	char	 *tofree, *string, *key, *v;

	if (NULL == (tofree = strdup(cp))) {
		kerr(NULL);
		return(0);
	}

	vp->set = 1;

	string = tofree;
	while (NULL != (key = strsep(&string, ";"))) {
		if (NULL == (v = strchr(key, '='))) {
			kerrx("%s:%zu: bad RRULE parameter",
				p->file, p->line);
			break;
		}
		*v++ = '\0';
		if (0 == strcmp(key, "FREQ")) {
			vp->freq = ICALFREQ_NONE + 1; 
			for ( ; vp->freq < ICALFREQ__MAX; vp->freq++)
				if (0 == strcmp(icalfreqs[vp->freq], v))
					break;
			if (ICALFREQ__MAX == vp->freq) {
				kerrx("%s:%zu: bad RRULE FREQ",
					p->file, p->line);
				break;
			}
		} else if (0 == strcmp(key, "UNTIL")) {
			if ( ! ical_utcdatetime(p, &vp->until, v))
				break;
		} else if (0 == strcmp(key, "COUNT")) {
			if ( ! ical_ulong(p, &vp->count, v))
				break;
		} else if (0 == strcmp(key, "INTERVAL")) {
			if ( ! ical_ulong(p, &vp->interval, v))
				break;
		} else if (0 == strcmp(key, "BYDAY")) {
			if ( ! ical_wklist(p, &vp->bwkd, &vp->bwkdsz, v))
				break;
		} else if (0 == strcmp(key, "BYHOUR")) {
			if ( ! ical_ulonglist(p, &vp->bhr, &vp->bhrsz, v))
				break;
		} else if (0 == strcmp(key, "BYMINUTE")) {
			if ( ! ical_longlist(p, &vp->bmin, &vp->bminsz, v))
				break;
		} else if (0 == strcmp(key, "BYMONTH")) {
			if ( ! ical_longlist(p, &vp->bmon, &vp->bmonsz, v))
				break;
		} else if (0 == strcmp(key, "BYSECOND")) {
			if ( ! ical_ulonglist(p, &vp->bsec, &vp->bsecsz, v))
				break;
		} else if (0 == strcmp(key, "BYSETPOS")) {
			if ( ! ical_longlist(p, &vp->bsp, &vp->bspsz, v))
				break;
		} else if (0 == strcmp(key, "BYWEEKNO")) {
			if ( ! ical_longlist(p, &vp->bwkn, &vp->bwknsz, v))
				break;
		} else if (0 == strcmp(key, "BYYEARDAY")) {
			if ( ! ical_longlist(p, &vp->byrd, &vp->byrdsz, v))
				break;
		} else if (0 == strcmp(key, "WKST")) {
			if ( ! ical_wkday(p, &vp->wkst, v))
				break;
		} else
			kerrx("%s:%zu: unknown RRULE "
				"parameters", p->file, p->line);
	}

	free(tofree);
	return(NULL == key);
}

/*
 * Try to convert "cp" into a UTC-offset string.
 * Return zero on failure, non-zero on success.
 */
static int
ical_utc_offs(const struct icalparse *p, int *v, const char *cp)
{
	size_t	 i, sz;
	int	 min, hour, sec;

	min = hour = sec = 0;

	*v = 0;
	/* Must be (+-)HHMM[SS]. */
	if (5 != (sz = strlen(cp)) && 7 != sz) {
		kerrx("%s:%zu: bad UTC-offset size", p->file, p->line);
		return(0);
	}

	for (i = 1; i < sz; i++)
		if ( ! isdigit((int)cp[i])) {
			kerrx("%s:%zu: non-digit UTC-offset "
				"character", p->file, p->line);
			return(0);
		}

	/* Check sign extension. */
	if ('-' != cp[0] && '+' != cp[0]) {
		kerrx("%s:%zu: bad UTC-offset sign "
			"extension", p->file, p->line);
		return(0);
	}

	/* Sanitise hour. */
	hour += 10 * (cp[1] - 48);
	hour += 1 * (cp[2] - 48);
	if (hour >= 24) {
		kerrx("%s:%zu: bad hour: %d", 
			p->file, p->line, hour);
		return(0);
	}

	/* Sanitise minute. */
	min += 10 * (cp[3] - 48);
	min += 1 * (cp[4] - 48);
	if (min >= 60) {
		kerrx("%s:%zu: bad minute: %d", 
			p->file, p->line, min);
		return(0);
	}

	/* See if we should have a second component. */
	if ('\0' != cp[5]) {
		/* Sanitise second. */
		sec += 10 * (cp[5] - 48);
		sec += 1 * (cp[6] - 48);
		if (sec >= 60) {
			kerrx("%s:%zu: bad second: %d", 
				p->file, p->line, sec);
			return(0);
		}
	}

	*v = sec + min * 60 + hour * 3600;
	/* Sign-extend. */
	if ('-' == cp[0])
		*v *= -1;

	return(1);
}

/*
 * Parse a non zero-length string from the iCalendar.
 * The string can have arbitrary characters.
 */
static int
ical_string(const struct icalparse *p, 
	const char **v, const char *cp)
{

	*v = NULL;
	if (NULL == cp || '\0' == *cp) {
		kerrx("%s:%zu: zero-length string", p->file, p->line);
		return(0);
	}
	*v = cp;
	return(1);
}

/*
 * Parse a CRLF-terminated line out of it the iCalendar file.
 * We ignore the last line, so non-CRLF-terminated EOFs are bad.
 * Returns 0 when no lines left, -1 on failure, 1 on success.
 */
static int
ical_line(struct icalparse *p, 
	char **name, char **param, char **value)
{
	const char	*end;
	size_t		 len;

	p->buf.sz = 0;
	while (p->pos < p->sz) {
		end = memmem(&p->cp[p->pos], 
			p->sz - p->pos, "\r\n", 2);
		if (NULL == end) {
			kerrx("%s:%zu: no CRLF", p->file, p->line);
			return(-1);
		}

		p->line++;
		len = end - &p->cp[p->pos];

		bufappend(&p->buf, &p->cp[p->pos], len);

		if (' ' != end[2] && '\t' != end[2]) 
			break;
		p->pos += len + 2;
		while (' ' == p->cp[p->pos] || '\t' == p->cp[p->pos])
			p->pos++;
	}

	/* Trailing \r\n. */
	if (p->pos < p->sz)
		p->pos += len + 2;

	assert(p->pos <= p->sz);

	if (NULL == (*name = p->buf.buf)) {
		kerrx("%s:%zu: empty line", p->file, p->line);
		return(-1);
	} else if (NULL == (*value = strchr(*name, ':'))) { 
		kerrx("%s:%zu: no value", p->file, p->line);
		return(-1);
	} else
		*(*value)++ = '\0';

	if (NULL != (*param = strchr(*name, ';'))) 
		*(*param)++ = '\0';

	return(p->pos < p->sz);
}

/*
 * Allocate a node onto the queue of currently-allocated nodes for this
 * parse.
 * Return the newly-created node or NULL on allocation failure.
 */
static struct icalnode *
icalnode_alloc(struct icalparse *p,
	const char *name, const char *val, const char *param)
{
	struct icalnode	*np;

	assert(NULL != name);
	assert(NULL != val);

	if (NULL == (np = calloc(1, sizeof(struct icalnode))))
		kerr(NULL);
	else if (NULL == (np->name = strdup(name)))
		kerr(NULL);
	else if (NULL == (np->val = strdup(val)))
		kerr(NULL);
	else if (param && NULL == (np->param = strdup(param)))
		kerr(NULL);
	else {
		/* Enqueue the iCalendar node. */
		if (NULL == p->cur) {
			assert(NULL == p->ical->first);
			p->cur = p->ical->first = np;
		} else {
			assert(NULL != p->ical->first);
			p->cur->next = np;
			p->cur = np;
		}
		return(np);
	}

	icalnode_free(np, 0);
	return(NULL);
}

/*
 * Parses an entire file "file" into an iCalendar buffer after
 * establishing a shared advisory lock on "file".
 */
struct ical *
ical_parsefile(const char *file)
{

	return(ical_parsefile_open(file, NULL));
}

/*
 * Close out an ical_parsefile_open() context where "keep" was not NULL,
 * i.e., the file descriptor "fd" is valid.
 * (If "fd" is -1, this function does nothing.)
 * Returns 0 on unlock or close failure.
 * Returns 1 otherwise.
 */
int
ical_parsefile_close(const char *file, int fd)
{

	if (-1 == fd)
		return(1);
	return(close_unlock(file, fd) >= 0);
}

/*
 * Open "file" and dump its contents into an iCalendar object.
 * The type of lock we take out on "file" depends on "keep": if "keep"
 * is non-NULL, we keep the file descriptor open with an exclusive lock
 * (it must be closed with ical_parsefile_close()); otherwise, we use a
 * shared lock and free it when the routine closes.
 * Returns NULL on parse failure, read, memory, or lock failure.
 * Returns the object, otherwise.
 */
struct ical *
ical_parsefile_open(const char *file, int *keep)
{
	int	 	 fd;
	char		*map;
	struct stat	 st;
	struct ical	*p;

	if (NULL != keep)
		*keep = fd = open_lock_ex(file, O_RDONLY, 06000);
	else
		fd = open_lock_sh(file, O_RDONLY, 06000);

	if (-1 == fd)
		return(NULL);

	if (-1 == fstat(fd, &st)) {
		kerr("%s: fstat", file);
		ical_parsefile_close(file, fd);
		return(NULL);
	}

	map = mmap(NULL, st.st_size, 
		PROT_READ, MAP_SHARED, fd, 0);

	if (MAP_FAILED == map) {
		kerr("%s: mmap", file);
		ical_parsefile_close(file, fd);
		return(NULL);
	} 

	p = ical_parse(file, map, st.st_size);
	if (-1 == munmap(map, st.st_size))
		kerr("%s: munmap", file);

	if (NULL == keep && ! ical_parsefile_close(file, fd)) {
		ical_free(p);
		p = NULL;
	}
	return(p);
}

/*
 * Fully parse a DAYLIGHT or STANDARD time component as specified by the
 * input "type" into the current calendar.
 * This requires that a timezone be set!
 */
static int
ical_parsetz(struct icalparse *p, enum icaltztype type)
{
	struct icaltz	*c;
	char		*name, *val, *param;
	int		 rc;
	struct icalnode	*np;
	void		*pp;

	if (NULL == p->comps[ICALTYPE_VTIMEZONE]) {
		kerrx("%s:%zu: unexpected timezone?", p->file, p->line);
		return(0);
	}

	assert(NULL != p->ical->comps[ICALTYPE_VTIMEZONE]);

	/*
	 * Re-allocate the per-timezone list of daytime and standard
	 * time objects (not really components).
	 */
	pp = reallocarray
		(p->comps[ICALTYPE_VTIMEZONE]->tzs,
		 p->comps[ICALTYPE_VTIMEZONE]->tzsz + 1,
		 sizeof(struct icaltz));
	if (NULL == pp) {
		kerr(NULL);
		return(0);
	}
	p->comps[ICALTYPE_VTIMEZONE]->tzs = pp;
	c = &p->comps[ICALTYPE_VTIMEZONE]->tzs
		[p->comps[ICALTYPE_VTIMEZONE]->tzsz];
	p->comps[ICALTYPE_VTIMEZONE]->tzsz++;
	memset(c, 0, sizeof(struct icaltz));
	c->type = type;

	while (p->pos < p->sz) {
		/* Try to parse the current line. */
		if ((rc = ical_line(p, &name, &param, &val)) < 0)
			return(0);
		if (NULL == (np = icalnode_alloc(p, name, val, param)))
			return(0);

		if (0 == strcasecmp("END", name)) {
			if (0 == strcasecmp(icaltztypes[type], val))
				break;
			continue;
		}

		if (0 == strcasecmp(name, "dtstart"))
			rc = ical_datetime(p, &c->dtstart, np->val);
		else if (0 == strcasecmp(name, "tzoffsetfrom"))
			rc = ical_utc_offs(p, &c->tzfrom, np->val);
		else if (0 == strcasecmp(name, "tzoffsetto"))
			rc = ical_utc_offs(p, &c->tzto, np->val);
		else if (0 == strcasecmp(name, "rrule"))
			rc = ical_rrule(p, &c->rrule, np->val);
		else
			rc = 1;

		if ( ! rc)
			return(0);
	}

	return(1);
}

/*
 * Fully parse an individual component, such as a VCALENDAR, VEVENT,
 * or VTIMEZONE, and any components it may contain.
 * Return when all of the component parts have been consumed.
 */
static int
ical_parsecomp(struct icalparse *p, enum icaltype type)
{
	struct icalcomp	*c;
	char		*name, *val, *param;
	int		 rc;
	struct icalnode	*np;
	enum icaltype	 tt;
	enum icaltztype	 tz;
	size_t		 line;

	if (NULL == (c = calloc(1, sizeof(struct icalcomp)))) {
		kerr(NULL);
		return(0);
	}
	c->type = type;

	if (NULL == p->comps[type]) {
		assert(NULL == p->ical->comps[type]);
		p->comps[type] = p->ical->comps[type] = c;
	} else {
		assert(NULL != p->ical->comps[type]);
		p->comps[type]->next = c;
		p->comps[type] = c;
	}

	p->ical->bits |= 1u << (unsigned int)type;

	line = p->line;
	while (p->pos < p->sz) {
		/* Try to parse the current line. */
		if ((rc = ical_line(p, &name, &param, &val)) < 0)
			return(0);
		if (NULL == (np = icalnode_alloc(p, name, val, param)))
			return(0);

		if (0 == strcasecmp("BEGIN", name)) {
			/* Look up in other nested component. */
			for (tt = 0; tt < ICALTYPE__MAX; tt++) 
				if (0 == strcasecmp(icaltypes[tt], val))
					break;
			if (tt < ICALTYPE__MAX && ! ical_parsecomp(p, tt))
				return(0);
			else if (tt < ICALTYPE__MAX)
				continue;
			/* Look up in time-zone components. */
			for (tz = 0; tz < ICALTZ__MAX; tz++) 
				if (0 == strcasecmp(icaltztypes[tz], val))
					break;
			if (tz < ICALTZ__MAX && ! ical_parsetz(p, tz))
				return(0);
			continue;
		} else if (0 == strcasecmp("END", name)) {
			if (0 == strcasecmp(icaltypes[type], val))
				break;
			continue;
		}

		if (0 == strcasecmp(name, "uid"))
			rc = ical_string(p, &c->uid, np->val);
		else if (0 == strcasecmp(name, "created"))
			rc = ical_utcdatetime(p, &c->created, np->val);
		else if (0 == strcasecmp(name, "last-modified"))
			rc = ical_utcdatetime(p, &c->lastmod, np->val);
		else if (0 == strcasecmp(name, "dtstamp"))
			rc = ical_utcdatetime(p, &c->dtstamp, np->val);
		else if (0 == strcasecmp(name, "dtstart"))
			rc = ical_tzdatetime(p, &c->dtstart, np);
		else if (0 == strcasecmp(name, "tzid"))
			rc = ical_string(p, &c->tzid, np->val);
		else if (0 == strcasecmp(name, "rrule"))
			rc = ical_rrule(p, &c->rrule, np->val);
		else
			rc = 1;

		if ( ! rc)
			return(0);
	}

	/*
	 * We can now run some post-processing queries.
	 */
	switch (c->type) {
	case (ICALTYPE_VEVENT):
		if (NULL == c->uid) {
			kerrx("%s:%zu: missing UID", p->file, line);
			return(0);
		}
		break;
	case (ICALTYPE_VTIMEZONE):
		if (NULL == c->tzid) {
			kerrx("%s:%zu: missing TZID", p->file, line);
			return(0);
		}
		break;
	default:
		break;
	}

	return(1);
}

/*
 * Parse a binary buffer "cp" of size "sz" from a file (which may be
 * NULL if there's no file--it's for reporting purposes only) into a
 * well-formed iCalendar structure.
 * Returns NULL if the parse failed in any way.
 */
struct ical *
ical_parse(const char *file, const char *cp, size_t sz)
{
	struct icalparse pp;
	unsigned char	 digest[MD5_DIGEST_LENGTH];
	char		*name, *val, *param;
	int		 rc;
	MD5_CTX		 ctx;
	struct ical	*p;
	size_t		 i;

	memset(&pp, 0, sizeof(struct icalparse));
	pp.file = NULL == file ? "<buffer>" : file;
	pp.cp = cp;
	pp.sz = sz;

	if (NULL == (pp.ical = p = calloc(1, sizeof(struct ical)))) {
		kerr(NULL);
		return(NULL);
	}

	MD5Init(&ctx);
	MD5Update(&ctx, cp, sz);
	MD5Final(digest, &ctx);
	for (i = 0; i < sizeof(digest); i++) 
	         snprintf(&p->digest[i * 2], 3, "%02x", digest[i]);

	while (pp.pos < pp.sz) {
		if ((rc = ical_line(&pp, &name, &param, &val)) < 0)
			break;
		if (NULL == icalnode_alloc(&pp, name, val, param))
			break;

		if (strcasecmp(name, "BEGIN")) {
			kerrx("%s:%zu: not BEGIN", pp.file, pp.line);
			break;
		} else if (strcasecmp(val, "VCALENDAR")) {
			kerrx("%s:%zu: not VCALENDAR", pp.file, pp.line);
			break;
		} else if ( ! ical_parsecomp(&pp, ICALTYPE_VCALENDAR))
			break;
	}

	free(pp.buf.buf);

	if (pp.pos == pp.sz)
		return(p);

	kerrx("%s: parse failed", pp.file);
	ical_free(p);
	return(NULL);
}

static int
icalnode_putc(int c, void *arg)
{
	int	 fd = *(int *)arg;

	return(write(fd, &c, 1));
}

/*
 * Correctly wrap iCalendar output lines.
 * We use (arbitrarily?) 74 written characters til wrapping.
 */
static int
icalnode_putchar(char c, size_t *col, ical_putchar fp, void *arg)
{

	if (74 == *col) {
		if ((*fp)('\r', arg) < 0)
			return(-1);
		if ((*fp)('\n', arg) < 0)
			return(-1);
		if ((*fp)(' ', arg) < 0)
			return(-1);
		*col = 1;
	}
	if ((*fp)(c, arg) < 0)
		return(-1);
	(*col)++;
	return(1);
}

/*
 * Print an iCalendar using the given callback "fp".
 * We only return zero when "fp" returns <0.
 * Otherwise return non-zero.
 */
static int
icalnode_print(const struct icalnode *p, ical_putchar fp, void *arg)
{
	const char	*cp;
	size_t		 col;

	if (NULL == p) 
		return(1);

	col = 0;
	for (cp = p->name; '\0' != *cp; cp++) 
		if (icalnode_putchar(*cp, &col, fp, arg) < 0)
			return(0);

	if (NULL != p->param) {
		if (icalnode_putchar(';', &col, fp, arg) < 0)
			return(0);
		for (cp = p->param; '\0' != *cp; cp++) 
			if (icalnode_putchar(*cp, &col, fp, arg) < 0)
				return(0);
	}

	if (icalnode_putchar(':', &col, fp, arg) < 0)
		return(0);
	for (cp = p->val; '\0' != *cp; cp++) 
		if (icalnode_putchar(*cp, &col, fp, arg) < 0)
			return(0);
	if ((*fp)('\r', arg) < 0)
		return(0);
	if ((*fp)('\n', arg) < 0)
		return(0);
	if (icalnode_print(p->next, fp, arg) < 0)
		return(0);
	return(1);
}

/*
 * Print (write) an iCalendar using ical_putchar as a write callback.
 */
int
ical_print(const struct ical *p, ical_putchar fp, void *arg)
{

	return(icalnode_print(p->first, fp, arg));
}

/*
 * Print an iCalendar directly to the given file descriptor.
 */
int
ical_printfile(int fd, const struct ical *p)
{

	return(icalnode_print(p->first, icalnode_putc, &fd));
}
