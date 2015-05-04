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
#ifndef EXTERN_H
#define EXTERN_H

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
	CALELEM_CALENDAR_QUERY,
	CALELEM_CALENDAR_TIMEZONE,
	CALELEM_CALENDAR_USER_ADDRESS_SET,
	CALELEM_CURRENT_USER_PRINCIPAL,
	CALELEM_CURRENT_USER_PRIVILEGE_SET,
	CALELEM_DISPLAYNAME,
	CALELEM_GETCONTENTTYPE,
	CALELEM_GETCTAG,
	CALELEM_GETETAG,
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
	PROP_CALENDAR_TIMEZONE,
	PROP_CALENDAR_USER_ADDRESS_SET,
	PROP_CURRENT_USER_PRINCIPAL,
	PROP_CURRENT_USER_PRIVILEGE_SET,
	PROP_DISPLAYNAME,
	PROP_GETCONTENTTYPE,
	PROP_GETCTAG,
	PROP_GETETAG,
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

/*
 * An iCalendar component type.
 */
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

/*
 * A node in an iCalendar parse tree.
 * Nodes don't necessarily correspond to component types.
 * Use "struct icalcomp" for any real work.
 */
struct	icalnode {
	char		*name;
	char		*param; /* might be NULL */
	char		*val; /* might be NULL */
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

/*
 * An iCalendar weekday consists of both a signed numeric value and a
 * named weekday component.
 */
struct	icalwk {
	long	 	 wk; /* zero if not set */
	enum icalwkday	 wkday;  /* ICALWKDAY_NONE if not set */
};

/*
 * Time as a YYYYMMDDTHHMMSS and broken down into the number of seconds
 * from the epoch (not tied to any time-zone, including UTC).
 * The "tm" value is zero if nothing has been set.
 */
struct	icaltm {
	time_t		 tm; /* from epoch (not UTC) */
	int		 ly; /* in leap year? */
	unsigned long	 year; /* from 1900 */
	unsigned long	 mon; /* month from 1 */
	unsigned long	 mday; /* day of month from 1 */
	unsigned long	 hour; /* hour (0--23) */
	unsigned long	 min; /* minute (0--59) */
	unsigned long	 sec; /* second (0--59) */
	unsigned long	 day; /* day in year (from 0) */
	unsigned long	 wday; /* day in week (0 == saturday) */
};

/*
 * An iCalendar duration (RFC 2445, 4.3.6).
 * If this has no values, the sign is zero.
 * All values are counted from zero.
 */
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
	int		 set; /* has been set */
	enum icalfreq	 freq; /* FREQ */
	struct icaltm	 until; /* UNTIL */
	unsigned long	 count; /* COUNT */
	unsigned long	 interval; /* INTERVAL */
	unsigned long	*bhr; /* BYHOUR */
	size_t		 bhrsz;
	unsigned long	*bmin; /* BYMINUTE */
	size_t		 bminsz;
	long		*bmnd; /* BYMONTHDAY */
	size_t		 bmndsz;
	unsigned long	*bmon; /* BYMONTH */
	size_t		 bmonsz;
	unsigned long	*bsec; /* BYSECOND */
	size_t		 bsecsz;
	long		*bsp; /* BYSETPOS */
	size_t		 bspsz;
	struct icalwk	*bwkd; /* BYDAY */
	size_t		 bwkdsz;
	long		*bwkn; /* BYWEEKNO */
	size_t		 bwknsz;
	long		*byrd; /* BYYEARDAY */
	size_t		 byrdsz;
	enum icalwkday	 wkst; /* WKST */
};

/*
 * This is either a daylight or standard-time time-zone block.
 * Each iCalendar timezone has at least one of these.
 */
struct	icaltz {
	enum icaltztype	 type;
	int		 tzfrom; /* seconds from */
	int		 tzto; /* seconds to */
	struct icaltm	 dtstart;
	struct icalrrule rrule;
};

/*
 * An iCalendar date-time can also be associated with a time-zone, e.g.,
 * DTSTART for a VEVENT.
 * This structure contains the timezone or NULL if there is no time-zone
 * (i.e., the date is in UTC).
 * It also contains the time within the timezone (or UTC).
 */
struct	icaltime {
	struct icalcomp	*tz;
	struct icaltm	 time;
};

/*
 * An iCalendar component.
 * Each of these may be associated with component properties such as the
 * UID or DTSTART, which are referenced here.
 * Some of these components are type-specific (such as anything related
 * to the timezones), but we put them all here anyway.
 */
struct	icalcomp {
	struct icalcomp	*next;
	enum icaltype	 type;
	struct icaltm	 created; /* CREATED (or zero) */
	struct icaltm	 lastmod; /* LASTMODIFIED (or zero) */
	struct icaltm	 dtstamp; /* DTSTAMP (or zero) */
	struct icalrrule rrule; /* RRULE (or zeroed) */
	struct icaltime	 dtstart; /* DTSTART (or zero) */
	struct icaltime	 dtend; /* DTEND (or zero) */
	struct icaldur	 duration; /* DURATION (or zero) */
	struct icaltz	*tzs; /* timezone rules (or NULL) */
	size_t		 tzsz; /* size of tzs */
	const char	*uid; /* UID of component */
	const char	*tzid; /* TZID of component */
};

/*
 * Each iCalendar has a set of component types associated with it picked
 * up during the parse (it's certainly an error not to define
 * ICAL_VCALENDAR).
 * Mark these off, then make a list of the correspondong nodes picked up
 * for those types in the array of component pointers.
 */
struct	ical {
	unsigned int	 bits;
#define	ICAL_VCALENDAR	 0x001
#define	ICAL_VEVENT	 0x002
#define	ICAL_VTODO	 0x004
#define	ICAL_VJOURNAL	 0x008
#define	ICAL_VFREEBUSY	 0x010
#define	ICAL_VTIMEZONE	 0x020
#define	ICAL_VALARM	 0x040
	char	 	 digest[33];
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

/*
 * Algorithm for HTTP digest.
 */
enum	httpalg {
	HTTPALG_MD5 = 0,
	HTTPALG_MD5_SESS,
	HTTPALG__MAX
};

/*
 * Quality of protection (QOP) for HTTP digest.
 */
enum	httpqop {
	HTTPQOP_NONE = 0,
	HTTPQOP_AUTH,
	HTTPQOP_AUTH_INT,
	HTTPQOP__MAX
};

/*
 * Parsed HTTP ``Authorization'' header (RFC 2617).
 * These are just copied over from kcgi's values.
 */
#define	KREALM		"kcaldav"
struct	httpauth {
	enum httpalg	 alg;
	enum httpqop	 qop;
	const char	*user;
	const char	*uri;
	const char	*realm;
	const char	*nonce;
	const char	*cnonce;
	const char	*response;
	size_t		 count;
	const char	*opaque;
	const char	*req;
	size_t		 reqsz;
	const char	*method;
};

struct	res {
	char		*data;
	struct ical	*ical;
	int64_t		 etag;
	char		*url;
	int64_t		 collection;
	int64_t		 id;
};

struct	coln {
	char		*url; /* name of collection */
	char		*displayname; /* displayname of collection */
	char		*colour; /* colour (RGBA) */
	char		*description; /* free-form description */
	int64_t		 ctag; /* collection tag */
	int64_t		 id; /* unique identifier */
};

/*
 * A principal is a user of the system.
 * The HTTP authorised user (struct httpauth) is matched against a list
 * of principals when the password file is read.
 */
struct	prncpl {
	char		*name; /* username */
	char		*hash; /* MD5 of name, realm, and password */
	uint64_t	 quota_used; /* quota (VFS) */
	uint64_t	 quota_avail; /* quota (VFS) */
	char		*email; /* email address */
	struct coln	*cols; /* owned collections */
	size_t		 colsz; /* number of owned collections */
	int64_t		 id; /* unique identifier */
};

/*
 * Return codes for nonce operations.
 */
enum	nonceerr {
	NONCE_ERR, /* generic error */
	NONCE_NOTFOUND, /* nonce entry not found */
	NONCE_REPLAY, /* replay attack detected! */
	NONCE_OK /* nonce checks out */
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

struct ical 	 *ical_parse(const char *, const char *, size_t sz);
void		  ical_free(struct ical *);
int		  ical_print(const struct ical *, ical_putchar, void *);
int		  ical_printfile(int, const struct ical *);
		  /* Experimental... */
void		  ical_rrule_generate(const struct icaltm *, 
			const struct icalrrule *);

struct caldav 	 *caldav_parse(const char *, size_t);
void		  caldav_free(struct caldav *);

int		  db_collection_new(const char *, int64_t);
int		  db_collection_remove(int64_t);
int		  db_collection_resources(void (*)(const struct res *, void *), int64_t, void *);
int		  db_collection_update(const struct coln *);
int		  db_init(const char *, int);
int		  db_nonce_new(char **);
enum nonceerr	  db_nonce_update(const char *, size_t);
enum nonceerr	  db_nonce_validate(const char *, size_t);
int		  db_owner_check_or_set(int64_t);
int		  db_prncpl_load(struct prncpl **, const char *);
int		  db_prncpl_new(const char *, const char *, const char *, const char *);
int		  db_prncpl_update(const struct prncpl *);
int		  db_resource_delete(const char *, int64_t, int64_t);
int		  db_resource_remove(const char *, int64_t);
int		  db_resource_load(struct res **, const char *, int64_t);
int		  db_resource_new(const char *, const char *, int64_t);
int		  db_resource_update(const char *, const char *, int64_t, int64_t);

int		  httpauth_nonce(const struct httpauth *, char **);

void		  prncpl_free(struct prncpl *);
int		  prncpl_validate(const struct prncpl *, 
			const struct httpauth *);

void		  res_free(struct res *);

extern const enum proptype calprops[CALELEM__MAX];
extern const enum calelem calpropelems[PROP__MAX];
extern const char *const calelems[CALELEM__MAX];
extern const char *const icaltypes[ICALTYPE__MAX];
extern const char *const icaltztypes[ICALTZ__MAX];
extern const char *const icalfreqs[ICALFREQ__MAX];
extern const char *const icalwkdays[ICALWKDAY__MAX];
extern int verbose;

__END_DECLS

#endif
