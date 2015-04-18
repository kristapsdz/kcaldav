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
	CALELEM_CALENDAR_DATA,
	CALELEM_CALENDAR_HOME_SET,
	CALELEM_CALENDAR_MULTIGET,
	CALELEM_CALENDAR_QUERY,
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
	PROP_CALENDAR_DATA,
	PROP_CALENDAR_HOME_SET,
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

struct	icalrrule {
	enum icalfreq	 freq;
	time_t		 until;
	unsigned long	 count;
	unsigned long	 interval;
	unsigned long	*bhr;
	size_t		 bhrsz;
	long		*bmin;
	size_t		 bminsz;
	long		*bmnd;
	size_t		 bmndsz;
	long		*bmon;
	size_t		 bmonsz;
	unsigned long	*bsec;
	size_t		 bsecsz;
	long		*bsp;
	size_t		 bspsz;
	long		*bwkn;
	size_t		 bwknsz;
	long		*byrd;
	size_t		 byrdsz;
};

/*
 * This is either a daylight or standard-time time-zone block.
 * Each iCalendar timezone has at least one of these.
 */
struct	icaltz {
	enum icaltztype	 type;
	int		 tzfrom;
	int		 tzto;
	time_t		 dtstart;
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
	time_t		 time;
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
	time_t		 created; /* CREATED (or zero) */
	time_t		 lastmod; /* LASTMODIFIED (or zero) */
	time_t		 dtstamp; /* DTSTAMP (or zero) */
	struct icaltime	 dtstart; /* DTSTART (or zero) */
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
	enum proptype	 key;
	char		*name;
	char		*xmlns;
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
};

/*
 * A principal is a user of the system.
 * The HTTP authorised user (struct httpauth) is matched against a list
 * of principals when the password file is read.
 */
struct	prncpl {
	int		 writable; /* can write to principal file */
	char		*name; /* username */
	char		*homedir; /* within calendar root */
	char		*email; /* email address */
};

/*
 * Principal entry.
 * These are parsed out of the principal file.
 */
struct	pentry {
	char	*hash; /* MD5(username:realm:pass) */
	char	*user; /* username */
	char	*uid; /* (not used) */
	char	*gid; /* (not used) */
	char 	*cls;  /* (not used) */
	char	*change; /* (not used) */
	char	*expire;  /* (not used) */
	char	*gecos; /* (not used) */
	char	*homedir; /* principal home */
};

struct	priv {
	char		*prncpl;
	unsigned int	 perms;
#define	PERMS_NONE	 0x00
#define	PERMS_READ	 0x01
#define	PERMS_WRITE	 0x02
#define	PERMS_DELETE	 0x04
#define	PERMS_ALL	(0x04|0x02|0x01)
};

/*
 * A collection configuration.
 * A principal (struct prncpl) is matched against the configuration list
 * to produce the permissions.
 */
struct	config {
	long long	  bytesused;
	long long	  bytesavail;
	char		 *displayname;
	unsigned int	  perms; /* current principal perms */
	struct priv	 *privs;
	size_t		  privsz;
};

enum	nonceerr {
	NONCE_ERR,
	NONCE_NOTFOUND,
	NONCE_REPLAY,
	NONCE_OK
};

__BEGIN_DECLS

/* Logging functions. */
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
struct ical 	 *ical_parsefile(const char *);
struct ical 	 *ical_parsefile_open(const char *, int *);
int		  ical_parsefile_close(const char *, int);
void		  ical_free(struct ical *);
int		  ical_print(const struct ical *, ical_putchar, void *);
int		  ical_printfile(int, const struct ical *);
int		  ical_parsedatetime(time_t *, const struct icalnode *);

struct caldav 	 *caldav_parse(const char *, size_t);
void		  caldav_free(struct caldav *);

void		  ctag_get(const char *, char *);
int		  ctag_update(const char *);

int		  httpauth_nonce(const char *, const struct httpauth *, char **);

int		  nonce_new(const char *, char **);
enum nonceerr	  nonce_update(const char *, const char *, size_t);
enum nonceerr	  nonce_validate(const char *, const char *, size_t);

int		  config_parse(const char *, struct config **, const struct prncpl *);
int		  config_replace(const char *, const struct config *);
void		  config_free(struct config *);

FILE		 *fdopen_lock(const char *, int, const char *);
int		  fclose_unlock(const char *, FILE *, int);
int		  open_lock_ex(const char *, int, mode_t);
int		  open_lock_sh(const char *, int, mode_t);
int		  close_unlock(const char *, int);
int		  quota(const char *, int, long long *, long long *);

int		  prncpl_replace(const char *, const char *,
			const char *, const char *);
int		  prncpl_parse(const char *, const char *,
		        const struct httpauth *, struct prncpl **,
			const char *, size_t);
void		  prncpl_free(struct prncpl *);
int		  prncpl_line(char *, size_t, 
			const char *, size_t, struct pentry *);
int		  prncpl_pentry_check(const struct pentry *);
int		  prncpl_pentry_dup(struct pentry *, const struct pentry *);
void		  prncpl_pentry_free(struct pentry *);
void		  prncpl_pentry_freelist(struct pentry *, size_t);
int		  prncpl_pentry_write(int, const char *, const struct pentry *);

extern const enum proptype calprops[CALELEM__MAX];
extern const enum calelem calpropelems[PROP__MAX];
extern const char *const calelems[CALELEM__MAX];
extern const char *const icaltypes[ICALTYPE__MAX];
extern const char *const icaltztypes[ICALTZ__MAX];
extern const char *const icalfreqs[ICALFREQ__MAX];
extern int verbose;

__END_DECLS

#endif
