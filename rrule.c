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
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "md5.h"

/* XXX: debug */
static	const char *const weeks[7] = {
	"Sat",
	"Sun",
	"Mon",
	"Tues",
	"Wed",
	"Thurs",
	"Fri",
};

struct	icaltmcmp {
	unsigned long	 year;
	unsigned long	 mon;
	unsigned long	 mday;
	unsigned long	 hour;
	unsigned long	 min;
	unsigned long	 sec;
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


static void
ical_rrule_produce(const struct icaltm *cur)
{

	fprintf(stderr, "%04lu%02lu%02luT%02lu%02lu%02lu (%s)\n",
		cur->year + 1900,
		cur->mon,
		cur->mday,
		cur->hour,
		cur->min,
		cur->sec,
		cur->wday < 7 ? weeks[cur->wday] : "none");
}

/*
 * Recursively daily (day-in-month) component.
 */
static void
ical_rrule_byday(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i;
	unsigned long	 j, wday;
	unsigned long	 wdays[7][5];
	size_t		 wdaysz[7];
	int		 rc, found;

	memset(wdays, 0, sizeof(wdays));
	memset(wdaysz, 0, sizeof(wdaysz));

	fprintf(stderr, "%s\n", __func__);

	/* Direct recursion: nothing to do here. */
	if (0 == rrule->bwkdsz) {
		fprintf(stderr, "(No entries: pre.)\n");
		ical_rrule_produce(cur);
		return;
	}

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

	for (found = 0, i = 0; i < rrule->bwkdsz; i++) {
		assert(rrule->bwkd[i].wkday > ICALWKDAY_NONE);
		assert(rrule->bwkd[i].wkday < ICALWKDAY__MAX);
		wday = rrule->bwkd[i].wkday % 7;

		/* All weekdays in month. */
		if (0 == rrule->bwkd[i].wk) {
			fprintf(stderr, "All weekdays: %s\n", weeks[wday]);
			for (j = 0; j < wdaysz[wday]; j++) {
				cmp.mday = wdays[wday][j];
				rc = ical_datetimecmp(&tm, &cmp);
				assert(rc);
				ical_rrule_produce(&tm);
				found = 1;
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
		fprintf(stderr, "Given weekday: %ld%s (%lu stored)\n", 
			rrule->bwkd[i].wk, weeks[wday], wdaysz[wday]);
		if (abs(rrule->bwkd[i].wk) - 1 >= (long)wdaysz[wday])
			continue;
		cmp.mday = rrule->bwkd[i].wk > 0 ?
			wdays[wday][rrule->bwkd[i].wk] :
			wdays[wday][wdaysz[wday] + rrule->bwkd[i].wk];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		ical_rrule_produce(&tm);
		found = 1;
	}

	/* We did nothing: recurse without setting. */
	if ( ! found) {
		fprintf(stderr, "(No entries: post.)\n");
		ical_rrule_produce(cur);
	}
}

/*
 * Recursive monthly component.
 */
static void
ical_rrule_bymonth(const struct icaltm *cur,
	const struct icalrrule *rrule)
{
	struct icaltmcmp cmp;
	struct icaltm	 tm;
	size_t		 i;
	int		 found;

	fprintf(stderr, "%s\n", __func__);

	/* Direct recursion: nothing to do here. */
	if (0 == rrule->bmonsz) {
		fprintf(stderr, "(No entries: pre.)\n");
		ical_rrule_byday(cur, rrule);
		return;
	}

	ical_datetime2cmp(&cmp, cur);
	for (found = 0, i = 0; i < rrule->bmonsz; i++) {
		cmp.mon = rrule->bmon[i];
		if ( ! ical_datetimecmp(&tm, &cmp))
			continue;
		ical_rrule_byday(&tm, rrule);
		found = 1;
	}

	if ( ! found) {
		fprintf(stderr, "(No entries: post.)\n");
		ical_rrule_byday(cur, rrule);
		return;
	}
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
			if (0 != rrule->until.tm)
				if (tm.tm > rrule->until.tm)
					break;
			ical_rrule_bymonth(&tm, rrule);
		}
		break;
	default:
		break;
		
	}
}

/*
 * Check whether the time "tm" (UTC) falls within the time described by
 * componet "c".
 */
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
