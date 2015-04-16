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

static void
icalnode_free(struct icalnode *p, int first)
{
	struct icalnode	*np;

	if (NULL == p)
		return;
	np = p->next;
	icalnode_free(p->first, 0);
	free(p->name);
	free(p->param);
	free(p->val);
	free(p);
	if (0 == first)
		icalnode_free(np, 0);
}

void
ical_free(struct ical *p)
{
	struct icalcomp	*c;
	size_t		 i;

	if (NULL == p)
		return;

	for (i = 0; i < ICALTYPE__MAX; i++) {
		while (NULL != (c = p->comps[i])) {
			p->comps[i] = c->next;
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
ical_datetime(time_t *tm, const char *cp)
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
		kerrx("invalid date string length");
		return(0);
	} 
	for (i = 0; i < 8; i++)
		if ( ! isdigit(cp[i])) {
			kerrx("non-digits in date string");
			return(0);
		}

	/* Sanitise the year. */
	year += 1000 * (cp[0] - 48);
	year += 100 * (cp[1] - 48);
	year += 10 * (cp[2] - 48);
	year += 1 * (cp[3] - 48);
	if (year < 1970) {
		kerrx("invalid year: %u", year);
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
		kerrx("invalid month: %u", mon);
		return(0);
	}

	/* Sanitise the day of month. */
	mday += 10 * (cp[6] - 48);
	mday += 1 * (cp[7] - 48);
	if (0 == mday || mday > mdayss[mday - 1]) {
		kerrx("invalid day: %u", mday);
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
			kerrx("invalid time string length");
			return(0);
		}
		for (i = 0; i < 6; i++)
			if ( ! isdigit(cp[9 + i])) {
				kerrx("non-digits in time string");
				return(0);
			}

		/* Sanitise the hour. */
		hour += 10 * (cp[9] - 48);
		hour += 1 * (cp[10] - 48);
		if (hour > 24) {
			kerrx("invalid hour: %u", hour);
			return(0);
		}

		/* Sanitise the minute. */
		min += 10 * (cp[11] - 48);
		min += 1 * (cp[12] - 48);
		if (min > 60) {
			kerrx("invalid minute: %u", min);
			return(0);
		}

		/* Sanitise the second. */
		sec += 10 * (cp[13] - 48);
		sec += 1 * (cp[14] - 48);
		if (sec > 60) {
			kerrx("invalid second: %u", sec);
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

static int
ical_utc(time_t *v, const char *cp)
{
	size_t	 sz;

	*v = 0;

	if (NULL == cp || 0 == (sz = strlen(cp))) {
		kerrx("zero-length UTC");
		return(0);
	} else if (16 != sz) {
		kerrx("invalid UTC size, have %zu, want 16", sz);
		return(0);
	} else if ('Z' != cp[sz - 1]) {
		kerrx("no UTC signature");
		return(0);
	}
	return(ical_datetime(v, cp));
}

static int
ical_utc_offs(int *v, const char *cp)
{
	size_t	 i, sz;
	int	 min, hour, sec;

	min = hour = sec = 0;

	*v = 0;
	/* Must be (+-)HHMM[SS]. */
	if (5 != (sz = strlen(cp)) || 7 != sz) {
		kerrx("bad UTC offset length");
		return(0);
	}
	for (i = 1; i < sz; i++)
		if ( ! isdigit((int)cp[i])) {
			kerrx("non-digit UTC character");
			return(0);
		}

	/* Check sign extension. */
	if ('-' != cp[0] && '+' != cp[0]) {
		kerrx("non-signed UTC");
		return(0);
	}

	/* Sanitise hour. */
	hour += 10 * (cp[1] - 48);
	hour += 1 * (cp[2] - 48);
	if (hour >= 24) {
		kerrx("invalid hour: %d", hour);
		return(0);
	}

	/* Sanitise minute. */
	min += 10 * (cp[3] - 48);
	min += 1 * (cp[4] - 48);
	if (min >= 60) {
		kerrx("invalid minute: %d", min);
		return(0);
	}

	/* See if we should have a second component. */
	if ('\0' != cp[5]) {
		/* Sanitise second. */
		sec += 10 * (cp[5] - 48);
		sec += 1 * (cp[6] - 48);
		if (sec >= 60) {
			kerrx("invalid second: %d", sec);
			return(0);
		}
	}

	*v = sec + min * 60 + hour * 3600;
	/* Sign-extend. */
	if ('-' == cp[0])
		*v *= -1;

	return(1);
}

static int
ical_string(struct icalnode **v, struct icalnode *n)
{

	*v = NULL;

	if (NULL == n->val || '\0' == *n->val) {
		kerrx("zero-length string");
		return(0);
	}
	*v = n;
	return(1);
}


/*
 * Parse a CRLF-terminated line out of it the iCalendar file.
 * We ignore the last line, so non-CRLF-terminated EOFs are bad.
 * Returns 0 when no CRLF is left, 1 on more lines.
 */
static int
ical_line(const char *cp, struct buf *buf, size_t *pos, size_t sz)
{
	const char	*end;
	size_t		 len;

	buf->sz = 0;
	while (*pos < sz) {
		end = memmem(&cp[*pos], sz - *pos, "\r\n", 2);
		if (NULL == end)
			return(0);
		len = end - &cp[*pos];
		bufappend(buf, &cp[*pos], len);
		if (' ' != end[2] && '\t' != end[2]) 
			break;
		*pos += len + 2;
		while (' ' == cp[*pos] || '\t' == cp[*pos])
			(*pos)++;
	}

	if (*pos < sz)
		*pos += len + 2;
	assert(*pos <= sz);
	return(1);
}

/*
 * We're at a BEGIN statement.
 * See if this statement matchines one of our known components, e.g.,
 * VEVENT.
 * Then set that the iCalendar contains a VEVENT (bit-wise) and also
 * append to a linked list of known VEVENTs.
 * While here, mark that the node is a given type, too.
 */
static int
icalcomp_alloc(struct icalcomp **comps, 
	struct ical *p, struct icalnode *np)
{
	size_t	 i;

	np->type = ICALTYPE__MAX;
	for (i = 0; i < ICALTYPE__MAX; i++) {
		if (strcmp(icaltypes[i], np->val))
			continue;
		p->bits |= 1u << i;
		np->type = i;
		if (NULL != comps[i]) {
			assert(NULL != p->comps[i]);
			comps[i]->next = calloc
				(1, sizeof(struct icalcomp));
			comps[i] = comps[i]->next;
		} else {
			assert(NULL == p->comps[i]);
			comps[i] = calloc
				(1, sizeof(struct icalcomp));
		}

		if (NULL == comps[i]) {
			kerr(NULL);
			return(0);
		} else if (NULL == p->comps[i])
			p->comps[i] = comps[i];

		comps[i]->type = i;
		break;
	}

	return(1);
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
 * This function is very straightforward.
 * Given a buffer "cp" (not necessarily nil-terminated, but CRLF
 * terminated in the way of iCalendar) of size "sz", parse the contained
 * iCalendar and return it.
 * Returns NULL on parse failure or memory exhaustion.
 * Returns the well-formed iCalendar structure otherwise.
 */
struct ical *
ical_parse(const char *file, const char *cp, size_t sz)
{
	struct ical	*p;
	struct icalnode	*cur, *np;
	char		*name, *val, *param;
	struct buf	 buf;
	MD5_CTX		 ctx;
	size_t		 i, pos, line;
	unsigned char	 digest[16];
	const char	*fp;
	struct icalcomp	*comps[ICALTYPE__MAX];
	enum icaltype	 type;
	int		 rc;

	memset(&buf, 0, sizeof(struct buf));
	memset(comps, 0, sizeof(comps));

	if (NULL == (p = calloc(1, sizeof(struct ical)))) {
		kerr(NULL);
		return(NULL);
	}

	/* Take an MD5 digest of the whole buffer. */
	MD5Init(&ctx);
	MD5Update(&ctx, cp, sz);
	MD5Final(digest, &ctx);
	for (i = 0; i < sizeof(digest); i++) 
	         snprintf(&p->digest[i * 2], 3, "%02x", digest[i]);

	fp = NULL == file ? "<buffer>" : file;
	cur = NULL;

	for (line = pos = 0; pos < sz; ) {
		line++;
		/* iCalendar is line-based: get the next line. */
		if (0 == ical_line(cp, &buf, &pos, sz)) {
			kerrx("%s:%zu: no CRLF", fp, sz);
			break;
		}

		/* Parse the name/value pair from the line. */
		if (NULL == (name = buf.buf)) {
			kerrx("%s:%zu: no line pair", fp, sz);
			break;
		}

		if (NULL != (val = strchr(name, ':')))
			*val++ = '\0';
		if (NULL == val) {
			kerrx("%s:%zu: no value", fp, sz);
			break;
		}

		if (NULL != (param = strchr(name, ';'))) 
			*param++ = '\0';

		/* Allocate the line record. */
		if (NULL == (np = calloc(1, sizeof(struct icalnode)))) {
			kerr(NULL);
			break;
		} else if (NULL == (np->name = strdup(name))) {
			kerr(NULL);
			icalnode_free(np, 0);
			break;
		} else if (val && NULL == (np->val = strdup(val))) {
			kerr(NULL);
			icalnode_free(np, 0);
			break;
		} else if (param && NULL == (np->param = strdup(param))) {
			kerr(NULL);
			icalnode_free(np, 0);
			break;
		}

		/*
		 * If this line defines a component, then scan it and
		 * set the appropriate field on our calendar.
		 * For example, if we're a VTODO, then set the bit on
		 * the calendar object that we contain the VTODO.
		 */
		if (NULL != val && 0 == strcmp("BEGIN", np->name)) {
			if ( ! icalcomp_alloc(comps, p, np)) {
				icalnode_free(np, 0);
				break;
			}
		}

		/* Handle the first entry and bad nesting. */
		if (NULL == p->first) {
			p->first = cur = np;
			continue;
		} else if (NULL == cur) {
			kerrx("%s:%zu: bad nest", fp, sz);
			break;
		} 

		assert(NULL != cur);
		assert(NULL != np);

		/* Handle empty blocks. */
		if (0 == strcmp("BEGIN", cur->name)) {
			if (0 == strcmp("END", np->name)) {
				np->parent = cur->parent;
				cur->next = np;
				cur = np;
			} else {
				cur->first = np;
				np->parent = cur;
				cur = np;
			}
		} else {
			if (0 == strcmp("END", np->name))
				if (NULL == (cur = cur->parent)) {
					kerrx("%s:%zu: bad nest", fp, sz);
					break;
				}
			np->parent = cur->parent;
			cur->next = np;
			cur = np;
		}

		/*
		 * Here we set specific component properties such as the
		 * UID or DTSTART.
		 * First make sure that we can do so.
		 */
		if (NULL == val)
			continue;
		if (NULL == np->parent)
			continue;
		if (ICALTYPE__MAX == np->parent->type)
			continue;

		type = np->parent->type;
		assert(NULL != comps[type]);

		if (0 == strcasecmp(np->name, "uid"))
			rc = ical_string(&comps[type]->uid, np);
		else if (0 == strcasecmp(np->name, "created"))
			rc = ical_utc(&comps[type]->created, np->val);
		else if (0 == strcasecmp(np->name, "last-modified"))
			rc = ical_utc(&comps[type]->lastmod, np->val);
		else if (0 == strcasecmp(np->name, "dtstamp"))
			rc = ical_utc(&comps[type]->dtstamp, np->val);
		else if (0 == strcasecmp(np->name, "dtstart"))
			rc = ical_string(&comps[type]->start, np);
		else if (0 == strcasecmp(np->name, "dtend"))
			rc = ical_string(&comps[type]->end, np);
		else if (0 == strcasecmp(np->name, "tzoffsetfrom"))
			rc = ical_utc_offs(&comps[type]->tzfrom, np->val);
		else if (0 == strcasecmp(np->name, "tzoffsetto"))
			rc = ical_utc_offs(&comps[type]->tzto, np->val);
		else
			rc = 1;

		if ( ! rc) {
			kerrx("%s:%zu: bad parse", fp, sz);
			break;
		}
	}

	free(buf.buf);

	/* Handle all sorts of error conditions. */
	if (NULL == p->first)
		kerrx("%s: empty", fp);
	else if (strcmp(p->first->name, "BEGIN") ||
		 strcmp(p->first->val, "VCALENDAR"))
		kerrx("%s: bad root", fp);
	else if (NULL != cur && NULL != cur->parent)
		kerrx("%s: bad nest", fp);
	else if (pos < sz)
		kerrx("%s: bad parse", fp);
	else
		return(p);

	ical_free(p);
	return(NULL);
}

static int
icalnode_putc(int c, void *arg)
{
	int	 fd = *(int *)arg;

	return(write(fd, &c, 1));
}

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
	if (icalnode_putchar(':', &col, fp, arg) < 0)
		return(0);
	for (cp = p->val; '\0' != *cp; cp++) 
		if (icalnode_putchar(*cp, &col, fp, arg) < 0)
			return(0);
	if ((*fp)('\r', arg) < 0)
		return(0);
	if ((*fp)('\n', arg) < 0)
		return(0);
	if (icalnode_print(p->first, fp, arg) < 0)
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

#if 0
int
ical_parsedatetime(time_t *tp, const struct icalnode *n)
{
	struct tm	 tm;

	assert(NULL != n);
	if (NULL == n->val || ! ical_parsedatenumeric(&tm, n->val))
		return(0);

	/* Accept all-day as being the start of the day GMT. */
	if (NULL != n->param && strcasecmp(n->param, "VALUE=DATE"))  {
		*tp = timegm(&tm);
		return(1);
	}

	/* Try to convert into UTC.  Ignores any TZID. */
	if ('Z' == n->val[strlen(n->val) - 1]) {
		*tp = timegm(&tm);
		return(1);
	} else if (NULL == n->param) {
		*tp = mktime(&tm);
		return(1);
	} else {
		setenv("TZ", n->param, 1);
		*tp = mktime(&tm);
		unsetenv("TZ");
		return(1);
	}

	return(0);
}
#endif
