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

enum	type {
	TYPE_CALMULTIGET,
	TYPE_CALQUERY,
	TYPE_MKCALENDAR,
	TYPE_PROPERTYUPDATE,
	TYPE_PROPFIND,
};

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
	CALELEM_MKCALENDAR,
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

struct	buf {
	char		*buf;
	size_t		 sz;
	size_t		 max;
};

struct	icalnode {
	char		*name;
	char		*val;
	struct icalnode	*parent;
	struct icalnode	*first;
	struct icalnode	*next;
};

struct	ical {
	char	 	 digest[33];
	struct icalnode	*first;
};

struct	prop {
	enum proptype	 key;
	char		*name;
	char		*xmlns;
};

struct	caldav {
	enum type	  type;
	struct prop	 *props;
	size_t		  propsz;
	char		**hrefs;
	size_t		  hrefsz;
};

/*
 * Parsed HTTP ``Authorization'' header (RFC 2617).
 * We only keep the critical parts required for authentication.
 */
struct	httpauth {
	int		 authorised;
	char		*user;
	char		*uri;
	char		*realm;
	char		*nonce;
	char		*response;
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
void		  kvdbg(const char *, size_t, const char *, ...);
void		  kverr(const char *, size_t, const char *, ...);
void		  kverrx(const char *, size_t, const char *, ...);
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

struct caldav	 *caldav_parsefile(const char *);
struct caldav 	 *caldav_parse(const char *, size_t);
void		  caldav_free(struct caldav *);

void		  ctag_get(const char *, char *);
int		  ctag_update(const char *);

int		  config_parse(const char *, struct config **, const struct prncpl *);
void		  config_free(struct config *);

int		  open_lock_ex(const char *, int, mode_t);
int		  open_lock_sh(const char *, int, mode_t);
int		  close_unlock(const char *, int);
int		  quota(const char *, int, long long *, long long *);

int		  prncpl_parse(const char *, const char *,
		        const struct httpauth *, struct prncpl **);
void		  prncpl_free(struct prncpl *);
int		  prncpl_line(char *, size_t, 
			const char *, size_t, struct pentry *);

struct httpauth	*httpauth_parse(const char *);
void		 httpauth_free(struct httpauth *);

extern const enum proptype calprops[CALELEM__MAX];
extern const enum calelem calpropelems[PROP__MAX];
extern const char *const calelems[CALELEM__MAX];
extern int verbose;

__END_DECLS

#endif
