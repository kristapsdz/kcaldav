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

#include <sys/time.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

struct	icaltmcmp {
	unsigned long	 year; /* x-1900, x>=1970 */
	unsigned long	 mon; /* 1--12 */
	unsigned long	 mday; /* 1--31 (depending) */
	unsigned long	 hour; /* 0--23 */
	unsigned long	 min; /* 0--59 */
	unsigned long	 sec; /* 0--59 */
};

static void
ical_datetime2cmp(struct icaltmcmp *cmp, const struct icaltm *tm)
{

	cmp->year = tm->year;
	cmp->mon = tm->mon;
	cmp->mday = tm->mday;
	cmp->hour = tm->hour;
	cmp->min = tm->min;
	cmp->sec = tm->sec;
}

/*
 * See ical_datetime() in ical.c for a description of the algorithm.
 * This set the date from a set of broken-down data components.
 * Note that "year" is -1900 (so 1970 is just 70).
 * If the components are invalid (e.g., 30 February), this returns zero;
 * else, it returns non-ezro.
 */
static int
ical_datetimecmp(struct icaltm *tm, const struct icaltmcmp *cmp)
{
	size_t		 i;
	unsigned long	 m, K, J;
	unsigned	 mdaysa[12] = {  0,  31,  59,  90, 
				       120, 151, 181, 212, 
				       243, 273, 304, 334 };
	unsigned	 mdayss[12] = { 31,  28,  31,  30, 
				        31,  30,  31,  31,
					30,  31,  30,  31 };

	memset(tm, 0, sizeof(struct icaltm));

	if (cmp->year < 70)
		return(0);
	tm->year = cmp->year;
	tm->ly = ((tm->year & 3) == 0 && 
		((tm->year % 25) != 0 || (tm->year & 15) == 0));

	if (tm->ly) {
		mdayss[1] = 29;
		for (i = 2; i < 12; i++)
			mdaysa[i]++;
	}

	if (0 == cmp->mon || cmp->mon > 12)
		return(0);
	tm->mon = cmp->mon;
	if (0 == cmp->mday || cmp->mday > mdayss[tm->mon - 1])
		return(0);
	tm->mday = cmp->mday;
	if (cmp->hour > 23)
		return(0);
	tm->hour = cmp->hour;
	if (cmp->min > 59)
		return(0);
	tm->min = cmp->min;
	if (cmp->sec > 59)
		return(0);
	tm->sec = cmp->sec;
	tm->day = mdaysa[tm->mon - 1] + (tm->mday - 1);
	tm->tm = tm->sec + tm->min * 60 + tm->hour * 3600 + 
		tm->day * 86400 + (tm->year - 70) * 31536000 + 
		((tm->year - 69) / 4) * 86400 -
		((tm->year - 1) / 100) * 86400 + 
		((tm->year + 299) / 400) * 86400;

	m = tm->mon < 3 ? tm->mon + 12 : tm->mon;
	K = (tm->year + 1900) % 100;
	J = floor((tm->year + 1900) / 100.0);
	tm->wday = 
		(tm->mday + 
		 (unsigned long)floor((13 * (m + 1)) / 5.0) +
		 K + 
		 (unsigned long)floor(K / 4.0) + 
		 (unsigned long)floor(J / 4.0) + 
		 5 * J) % 7;
	return(1);
}


static size_t
ical_rrule_produce(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	const char *const weeks[7] = {
		"Sat",
		"Sun",
		"Mon",
		"Tues",
		"Wed",
		"Thurs",
		"Fri",
	};

	/* Check the UNTIL clause. */
	/* 
	 * FIXME: UNTIL is in UTC, so we need to convert the "cur->tm"
	 * value into UTC in order for it to be consistent.
	 */
	if (0 != rrule->until.tm)
		if (cur->tm > rrule->until.tm)
			return(0);

	fprintf(stderr, "%04lu%02lu%02luT%02lu%02lu%02lu (%s)\n",
		cur->year + 1900,
		cur->mon,
		cur->mday,
		cur->hour,
		cur->min,
		cur->sec,
		cur->wday < 7 ? weeks[cur->wday] : "none");
	return(1);
}

static size_t
ical_rrule_bysetpos(const struct icaltm *cur,
	const struct icalrrule *rrule)
{

	return(ical_rrule_produce(cur, rrule));
}

static size_t
ical_rrule_bysecond(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i, found;
	int		 have;

	ical_datetime2cmp(&cmp, cur);
	for (found = i = 0, have = 0; i < rrule->bminsz; i++) {
		cmp.min = rrule->bmin[i];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		found += ical_rrule_bysetpos(&tm, rrule);
		have = 1;
	}

	/* If no entries, route directly through */
	return(have ? found : ical_rrule_bysetpos(cur, rrule));
}

static size_t
ical_rrule_byminute(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i, found;
	int		 have;

	ical_datetime2cmp(&cmp, cur);
	for (found = i = 0, have = 0; i < rrule->bminsz; i++) {
		cmp.min = rrule->bmin[i];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		found += ical_rrule_bysecond(&tm, rrule);
		have = 1;
	}

	/* If no entries, route directly through */
	return(have ? found : ical_rrule_bysecond(cur, rrule));
}

static size_t
ical_rrule_byhour(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i, found;
	int		 have;

	ical_datetime2cmp(&cmp, cur);
	for (found = i = 0, have = 0; i < rrule->bhrsz; i++) {
		cmp.hour = rrule->bhr[i];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		found += ical_rrule_byminute(&tm, rrule);
		have = 1;
	}

	/* If no entries, route directly through */
	return(have ? found : ical_rrule_byminute(cur, rrule));
}

/*
 * Recursively daily (day-in-week) component.
 */
static size_t
ical_rrule_byday(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i, found;
	unsigned long	 j, wday;
	unsigned long	 wdays[7][5];
	size_t		 wdaysz[7];
	int		 rc, have;

	memset(wdays, 0, sizeof(wdays));
	memset(wdaysz, 0, sizeof(wdaysz));

	/* Direct recursion: nothing to do here. */
	if (0 == rrule->bwkdsz) 
		return(ical_rrule_byhour(cur, rrule));

	/* 
	 * In the current month, compute the days that correspond to a
	 * given weekday.
	 * To do so, first compute the weekday given the date, then map
	 * an array of that weekday's days back into the day of month.
	 */
	ical_datetime2cmp(&cmp, cur);
	for (i = 0; i < 32; i++) {
		cmp.mday = i;
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		assert(wdaysz[tm.wday] < sizeof(wdays[tm.wday]));
		wdays[tm.wday][wdaysz[tm.wday]++] = cmp.mday;
	}

	for (found = i = 0, have = 0; i < rrule->bwkdsz; i++) {
		assert(rrule->bwkd[i].wkday > ICALWKDAY_NONE);
		assert(rrule->bwkd[i].wkday < ICALWKDAY__MAX);
		wday = rrule->bwkd[i].wkday % 7;

		/* All weekdays in month. */
		if (0 == rrule->bwkd[i].wk) {
			for (j = 0; j < wdaysz[wday]; j++) {
				cmp.mday = wdays[wday][j];
				rc = ical_datetimecmp(&tm, &cmp);
				assert(rc);
				found += ical_rrule_byhour(&tm, rrule);
				have = 1;
			}
			continue;
		}

		/* 
		 * Specific weekday in month. 
		 * First, see if the weekday requested doesn't exist
		 * (e.g., the ninth Sunday).
		 * Second, see if we're counting from the end of the
		 * month or from the beginning.
		 */
		if (abs(rrule->bwkd[i].wk) - 1 >= (long)wdaysz[wday])
			continue;
		cmp.mday = rrule->bwkd[i].wk > 0 ?
			wdays[wday][rrule->bwkd[i].wk] :
			wdays[wday][wdaysz[wday] + rrule->bwkd[i].wk];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		found += ical_rrule_byhour(&tm, rrule);
		have = 1;
	}

	return(have ? found : ical_rrule_byhour(cur, rrule));
}

/*
 * Recursive day (day-in-month) component.
 */
static size_t
ical_rrule_bymonthday(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i, found;
	int		 have;
	unsigned	 mdayss[12] = { 31,  28,  31,  30, 
				        31,  30,  31,  31,
					30,  31,  30,  31 };

	/* Leap-year February. */
	if (cur->ly) 
		mdayss[1] = 29;
	ical_datetime2cmp(&cmp, cur);
	for (found = i = 0, have = 0; i < rrule->bmndsz; i++) {
		assert(0 != rrule->bmnd[i]);
		assert(cmp.mon > 0 && cmp.mon < 13);
		/*
		 * If we're going backwards, then make sure we don't
		 * specify a day before the beginning of the month,
		 * e.g., -31 for February.
		 */
		if (rrule->bmnd[i] < 0 && mdayss[cmp.mon - 1] < 
			(unsigned long)abs(rrule->bmnd[i]))
			continue;

		cmp.mday = rrule->bmnd[i] < 0 ?
			(unsigned long)(mdayss[cmp.mon - 1] + (rrule->bmnd[i] + 1)) :
			(unsigned long)rrule->bmnd[i];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		found += ical_rrule_byday(&tm, rrule);
		have = 1;
	}

	/* If no entries, route directly through */
	return(have ? found : ical_rrule_byday(cur, rrule));
}

/*
 * Recrusive weekly (number in year) component.
 */
static size_t
ical_rrule_byweekno(const struct icaltm *cur,
	const struct icalrrule *rrule)
{

	return(ical_rrule_bymonthday(cur, rrule));
}

/*
 * Recursive monthly component.
 */
static size_t
ical_rrule_bymonth(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i, found;
	int		 have;

	ical_datetime2cmp(&cmp, cur);
	for (found = i = 0, have = 0; i < rrule->bmonsz; i++) {
		cmp.mon = rrule->bmon[i];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		found += ical_rrule_byweekno(&tm, rrule);
		have = 1;
	}

	/* If no entries, route directly through */
	return(have ? found : ical_rrule_byweekno(cur, rrule));
}

/*
 * Taking the first instance of a RRULE in "first", enumerate all
 * possible instances following that.
 */
void
ical_rrule_generate(const struct icaltm *first, 
	const struct icalrrule *rrule)
{
	unsigned long 	 ivl, i, count;
	struct icaltm	 tm;
	struct icaltmcmp cmp;

	ivl = 0 == rrule->interval ? 1 : rrule->interval;
	count = 0 == rrule->count ? 10 : rrule->count;
	tm = *first;

	/* Create our initial instance. */
	ical_datetime2cmp(&cmp, first);

	switch (rrule->freq) {
	case (ICALFREQ_YEARLY):
		for (i = 0; i < count; i++) {
			/* Increment over each year. */
			cmp.year += ivl;
			if ( ! ical_datetimecmp(&tm, &cmp))
				continue;
			/* Continue til no valid occurrences. */
			if (0 == ical_rrule_bymonth(&tm, rrule))
				break;
		}
		break;
	default:
		break;
		
	}
}

#if 0
time_t
ical_time2utc(const struct icaltime *time)
{
	size_t			 i;
	time_t			 minres, res, diff, mindiff;
	const struct icaltz	*tz;

	if (NULL == time->tz)
		return(time->time.tm);

	/*
	 * Initialise this to something because we're not guaranteed
	 * that the timezone entry will be sane.
	 * For example, each one of the RRULE components could have
	 * expired, leaving us nothing to translate into.
	 */
	mindiff = LONG_MAX;
	minres = time->time.tm;

	for (i = 0; i < time->tz->tzsz; i++) {
		tz = &time->tz->tzs[i];
		/* Check if we the rule applies to us. */
		if (time->time.tm < tz->dtstart.tm)
			continue;
		/* 
		 * If there's no RRULE, then we automatically choose
		 * this subcomponent as the correct one.
		 */
		res = time->time.tm + tz->tzto;
		if (0 == tz->rrule.set)
			break;

		/* FIXME: check the RDATE. */

		/*
		 * We compute the instance we'll be checking in the
		 * RRULE as described in the spec: using the tzto.
		 */
		res = time->time.tm + tz->tzto;

		/* See if we fall after the RRULE end date. */
		if (0 != tz->rrule.until.tm)
			if (res >= tz->rrule.until.tm)
				continue;

		/*
		 * Compute the nearest date produced by the RRULE to the
		 * current date.
		 * Store the minimum difference, which will be the
		 * correct timezone component to use.
		 */
		diff = /*ical_rrule_after(&time->time, &tz->rrule) - 
			time->time.tm;*/ 0;

		if (diff < mindiff) {
			minres = res;
			mindiff = diff;
		}
	}

	return(minres);
}
#endif
