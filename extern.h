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
 */
struct	icalnode {
	char		*name;
	char		*val; /* might be NULL */
	enum icaltype	 type; /* if found, else ICALTYPE__MAX */
	struct icalnode	*parent;
	struct icalnode	*first;
	struct icalnode	*next;
};

/*
 * An iCalendar component.
 * Each of these may be associated with component properties such as the
 * UID or DTSTART, which are referenced here.
 */
struct	icalcomp {
	struct icalcomp	*next;
	enum icaltype	 type;
	const char	*uid; /* UID of component */
	const char	*start; /* DTSTART of component */
	const char	*end; /* DTEND of component */
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
 * We only keep the critical parts required for authentication.
 */
#define	KREALM		"kcaldav"
struct	httpauth {
	int		 authorised;
	enum httpalg	 alg;
	enum httpqop	 qop;
	char		*user;
	char		*uri;
	char		*realm;
	char		*nonce;
	char		*cnonce;
	char		*response;
	char		*count;
};

/*
 * A principal is a user of the system.
 * The HTTP authorised user (struct httpauth) is matched against a list
 * of principals when the password file is read.
 */
struct	prncpl {
	char		*name;
	char		*homedir;
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

/*
 * A collection configuration.
 * A principal (struct prncpl) is matched against the configuration list
 * to produce the permissions.
 */
struct	config {
	long long	  bytesused;
	long long	  bytesavail;
	char		 *displayname;
	char		 *emailaddress;
	unsigned int	  perms;
#define	PERMS_NONE	  0x00
#define	PERMS_READ	  0x01
#define	PERMS_WRITE	  0x02
#define	PERMS_DELETE	  0x04
#define	PERMS_ALL	 (0x04|0x02|0x01)
};

__BEGIN_DECLS

/* Logging functions. */
void		  kvdbg(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
void		  kverr(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
void		  kverrx(const char *, size_t, const char *, ...)
			__attribute__((format(printf, 3, 4)));
#define	 	  kerr(fmt, ...) \
		  kverr(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define	 	  kdbg(fmt, ...) \
		  kvdbg(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
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

struct caldav 	 *caldav_parse(const char *, size_t);
void		  caldav_free(struct caldav *);

void		  ctag_get(const char *, char *);
int		  ctag_update(const char *);

int		  config_parse(const char *, struct config **, const struct prncpl *);
void		  config_free(struct config *);

FILE		 *fdopen_lock(const char *, int, const char *);
int		  fclose_unlock(const char *, FILE *, int);
int		  open_lock_ex(const char *, int, mode_t);
int		  open_lock_sh(const char *, int, mode_t);
int		  close_unlock(const char *, int);
int		  quota(const char *, int, long long *, long long *);

int		  prncpl_parse(const char *, const char *,
		        const struct httpauth *, struct prncpl **,
			const char *, size_t);
void		  prncpl_free(struct prncpl *);
int		  prncpl_line(char *, size_t, 
			const char *, size_t, struct pentry *);
int		  prncpl_pentry(const struct pentry *p);

struct httpauth	*httpauth_parse(const char *);
void		 httpauth_free(struct httpauth *);

extern const enum proptype calprops[CALELEM__MAX];
extern const enum calelem calpropelems[PROP__MAX];
extern const char *const calelems[CALELEM__MAX];
extern const char *const icaltypes[ICALTYPE__MAX];
extern int verbose;

__END_DECLS

#endif
