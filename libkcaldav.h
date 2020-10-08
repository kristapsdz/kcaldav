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
#ifndef LIBKCALDAV_H
#define LIBKCALDAV_H

#if !defined(__BEGIN_DECLS)
#  ifdef __cplusplus
#  define       __BEGIN_DECLS           extern "C" {
#  else
#  define       __BEGIN_DECLS
#  endif
#endif
#if !defined(__END_DECLS)
#  ifdef __cplusplus
#  define       __END_DECLS             }
#  else
#  define       __END_DECLS
#  endif
#endif

/*
 * I only handle certain types of XML CalDAV documents (corresponding to
 * the outer XML element).  This defines the request type per method.
 */
enum	type {
	TYPE_CALMULTIGET,
	TYPE_CALQUERY,
	TYPE_PROPERTYUPDATE,
	TYPE_PROPFIND,
};

/*
 * Each of the XML elements we support.
 */
enum	calelem {
	CALELEM_CALENDAR_COLOR,
	CALELEM_CALENDAR_DATA,
	CALELEM_CALENDAR_DESCRIPTION,
	CALELEM_CALENDAR_HOME_SET,
	CALELEM_MIN_DATE_TIME,
	CALELEM_CALENDAR_MULTIGET,
	CALELEM_CALENDAR_PROXY_READ_FOR,
	CALELEM_CALENDAR_PROXY_WRITE_FOR,
	CALELEM_CALENDAR_QUERY,
	CALELEM_CALENDAR_TIMEZONE,
	CALELEM_CALENDAR_USER_ADDRESS_SET,
	CALELEM_CURRENT_USER_PRINCIPAL,
	CALELEM_CURRENT_USER_PRIVILEGE_SET,
	CALELEM_DISPLAYNAME,
	CALELEM_GETCONTENTTYPE,
	CALELEM_GETCTAG,
	CALELEM_GETETAG,
	CALELEM_GROUP_MEMBER_SET,
	CALELEM_GROUP_MEMBERSHIP,
	CALELEM_HREF,
	CALELEM_OWNER,
	CALELEM_PRINCIPAL_URL,
	CALELEM_PROP,
	CALELEM_PROPERTYUPDATE,
	CALELEM_PROPFIND,
	CALELEM_QUOTA_AVAILABLE_BYTES,
	CALELEM_QUOTA_USED_BYTES,
	CALELEM_RESOURCETYPE,
	CALELEM_SCHEDULE_CALENDAR_TRANSP,
	CALELEM_SUPPORTED_CALENDAR_COMPONENT_SET,
	CALELEM_SUPPORTED_CALENDAR_DATA,
	CALELEM__MAX
};

/*
 * Each of the property types within <DAV::prop> elements that we
 * support.
 */
enum	proptype {
	PROP_CALENDAR_COLOR,
	PROP_CALENDAR_DATA,
	PROP_CALENDAR_DESCRIPTION,
	PROP_CALENDAR_HOME_SET,
	PROP_MIN_DATE_TIME,
	PROP_CALENDAR_PROXY_READ_FOR,
	PROP_CALENDAR_PROXY_WRITE_FOR,
	PROP_CALENDAR_TIMEZONE,
	PROP_CALENDAR_USER_ADDRESS_SET,
	PROP_CURRENT_USER_PRINCIPAL,
	PROP_CURRENT_USER_PRIVILEGE_SET,
	PROP_DISPLAYNAME,
	PROP_GETCONTENTTYPE,
	PROP_GETCTAG,
	PROP_GETETAG,
	PROP_GROUP_MEMBER_SET,
	PROP_GROUP_MEMBERSHIP,
	PROP_OWNER,
	PROP_PRINCIPAL_URL,
	PROP_QUOTA_AVAILABLE_BYTES,
	PROP_QUOTA_USED_BYTES,
	PROP_RESOURCETYPE,
	PROP_SCHEDULE_CALENDAR_TRANSP,
	PROP_SUPPORTED_CALENDAR_COMPONENT_SET,
	PROP_SUPPORTED_CALENDAR_DATA,
	PROP__MAX
};

/*
 * Buffers are just dynamic nil-terminated strings that we constantly
 * resize upward, then size to zero when we're done.
 * I use these in several different places for growable strings.
 */
struct	buf {
	char		*buf;
	size_t		 sz;
	size_t		 max;
};

enum	icaltype {
	ICALTYPE_VCALENDAR,
	ICALTYPE_VEVENT,
	ICALTYPE_VTODO,
	ICALTYPE_VJOURNAL,
	ICALTYPE_FVREEBUSY,
	ICALTYPE_VTIMEZONE,
	ICALTYPE_VALARM,
	ICALTYPE__MAX
};

struct	icalnode {
	char		*name;
	char		*param;
	char		*val;
	struct icalnode	*next;
};

enum	icaltztype {
	ICALTZ_DAYLIGHT,
	ICALTZ_STANDARD,
	ICALTZ__MAX
};

enum	icalfreq {
	ICALFREQ_NONE = 0,
	ICALFREQ_SECONDLY,
	ICALFREQ_MINUTELY,
	ICALFREQ_HOURLY,
	ICALFREQ_DAILY,
	ICALFREQ_WEEKLY,
	ICALFREQ_MONTHLY,
	ICALFREQ_YEARLY,
	ICALFREQ__MAX
};

enum	icalwkday {
	ICALWKDAY_NONE = 0,
	ICALWKDAY_SUN,
	ICALWKDAY_MON,
	ICALWKDAY_TUES,
	ICALWKDAY_WED,
	ICALWKDAY_THUR,
	ICALWKDAY_FRI,
	ICALWKDAY_SAT,
	ICALWKDAY__MAX
};

struct	icalwk {
	long	 	 wk;
	enum icalwkday	 wkday;
};

struct	icaltm {
	time_t		 tm;
	int		 set;
};

struct	icaldur {
	int		 sign; /* >0 pos, <0 neg */
	unsigned long	 day;
	unsigned long	 week;
	unsigned long	 hour;
	unsigned long	 min;
	unsigned long	 sec;
};

#if 0
/*
 * An iCalendar period of time (RFC 2445, 4.3.9).
 * This can be either explicit (bracketed time) or computed (starting
 * bracket and a duration).
 */
struct	icalper {
	struct icaltm	 tm1;
	struct icaldur	 dur; /* if unset, then... */
	struct icaltm	 tm2;
};

/*
 * A repeating date (RFC 2445, 4.8.5.3) is a sequence of finite date
 * components: dates, date-times, or bracketed date spans.
 */
struct	icalrdate {
	int		 set; /* whether set */
	struct icalcomp	*tz; /* time-zone */
	struct icaltm	*dates; /* dates (w/timezone) */
	size_t		 datesz; 
	struct icaltm	*datetimes; /* date-times (w/timezone) */
	size_t		 datesz; 
	struct icalper	*pers; /* date spans, if applicable */
	size_t		 persz;
};
#endif

struct	icalrrule {
	int		 set;
	enum icalfreq	 freq;
	struct icaltm	 until;
	unsigned long	 count;
	unsigned long	 interval;
	unsigned long	*bhr;
	size_t		 bhrsz;
	unsigned long	*bmin;
	size_t		 bminsz;
	long		*bmnd;
	size_t		 bmndsz;
	unsigned long	*bmon;
	size_t		 bmonsz;
	unsigned long	*bsec;
	size_t		 bsecsz;
	long		*bsp;
	size_t		 bspsz;
	struct icalwk	*bwkd;
	size_t		 bwkdsz;
	long		*bwkn;
	size_t		 bwknsz;
	long		*byrd;
	size_t		 byrdsz;
	enum icalwkday	 wkst;
};

struct	icaltz {
	enum icaltztype	 type;
	int		 tzfrom;
	int		 tzto;
	struct icaltm	 dtstart;
	struct icalrrule rrule;
};

struct	icaltime {
	const struct icalcomp *tz;
	struct icaltm	 time;
	char		*tzstr;
};

struct	icalcomp {
	struct icalcomp	*next;
	enum icaltype	 type;
	struct icaltm	 created;
	struct icaltm	 lastmod;
	struct icaltm	 dtstamp;
	struct icalrrule rrule;
	struct icaltime	 dtstart;
	struct icaltime	 dtend;
	struct icaldur	 duration;
	struct icaltz	*tzs;
	size_t		 tzsz;
	const char	*uid;
	const char	*tzid;
};

struct	ical {
	unsigned int	 bits;
#define	ICAL_VCALENDAR	 0x001
#define	ICAL_VEVENT	 0x002
#define	ICAL_VTODO	 0x004
#define	ICAL_VJOURNAL	 0x008
#define	ICAL_VFREEBUSY	 0x010
#define	ICAL_VTIMEZONE	 0x020
#define	ICAL_VALARM	 0x040
	struct icalnode	*first;
	struct icalcomp	*comps[ICALTYPE__MAX];
};

/*
 * A CalDAV or DAV property in the XML request.
 */
struct	prop {
	enum proptype	 key; /* key or PROP__MAX if unknown */
	char		*name; /* name (without namespace */
	char		*xmlns; /* XML namespace */
	char		*val; /* the value if provided for setting */
	int		 valid; /* -1 invalid, 0 no valid, 1 valid */
};

/*
 * A parsed CalDAV request.
 * Given the type, this might have properties or hrefs that we care
 * about--it depends.
 */
struct	caldav {
	enum type	  type;
	struct prop	 *props;
	size_t		  propsz;
	char		**hrefs;
	size_t		  hrefsz;
};

__BEGIN_DECLS

void		  kvdbg(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
void		  kvinfo(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
void		  kverr(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
void		  kverrx(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
#define	 	  kerr(fmt, ...) \
		  kverr(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define	 	  kdbg(fmt, ...) \
		  kvdbg(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define	 	  kinfo(fmt, ...) \
		  kvinfo(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define	 	  kerrx(fmt, ...) \
		  kverrx(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

typedef int	(*ical_putchar)(int, void *);

void 		  bufappend(struct buf *, const char *, size_t);
void		  bufreset(struct buf *);

struct ical 	 *ical_parse(const char *, const char *, size_t sz, char **);
void		  ical_free(struct ical *);
int		  ical_print(const struct ical *, ical_putchar, void *);
int		  ical_printfile(int, const struct ical *);
		  /* Experimental... */
#if 0
void		  ical_rrule_generate(const struct icaltm *, 
			const struct icalrrule *);
#endif

struct caldav 	 *caldav_parse(const char *, size_t, char **);
void		  caldav_free(struct caldav *);

extern const enum proptype calprops[CALELEM__MAX];
extern const enum calelem calpropelems[PROP__MAX];
extern const char *const calelems[CALELEM__MAX];
extern const char *const icaltypes[ICALTYPE__MAX];
extern const char *const icaltztypes[ICALTZ__MAX];
extern const char *const icalfreqs[ICALFREQ__MAX];
extern const char *const icalwkdays[ICALWKDAY__MAX];

/*
 * This pertains to back-end logging.
 * 0: be totally silent.
 * 1: print errors and warnings only.
 * >1: print informational messages.
 */
extern int verbose;

__END_DECLS

#endif
