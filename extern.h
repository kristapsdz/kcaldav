#ifndef EXTERN_H
#define EXTERN_H

enum	type {
	TYPE_CALQUERY,
	TYPE_MKCALENDAR,
	TYPE_PROPFIND,
	TYPE_PROPLIST /* not CalDav */
};

enum	calelem {
	CALELEM_MKCALENDAR,
	CALELEM_PROPFIND,
	CALELEM_PROPLIST,
	CALELEM_DISPLAYNAME,
	CALELEM_CALDESC,
	CALELEM_CALQUERY,
	CALELEM_CALTIMEZONE,
	CALELEM_CREATIONDATE,
	CALELEM_GETCONTENTLANGUAGE,
	CALELEM_GETCONTENTLENGTH,
	CALELEM_GETCONTENTTYPE,
	CALELEM_GETETAG,
	CALELEM_GETLASTMODIFIED,
	CALELEM_LOCKDISCOVERY,
	CALELEM_RESOURCETYPE,
	CALELEM_SOURCE,
	CALELEM_SUPPORTLOCK,
	CALELEM_EXECUTABLE,
	CALELEM_CHECKEDIN,
	CALELEM_CHECKEDOUT,
	CALELEM__MAX
};


enum	proptype {
	PROP_CALDESC,
	PROP_CALTIMEZONE,
	PROP_CREATIONDATE,
	PROP_DISPLAYNAME,
	PROP_GETCONTENTLANGUAGE,
	PROP_GETCONTENTLENGTH,
	PROP_GETCONTENTTYPE,
	PROP_GETETAG,
	PROP_GETLASTMODIFIED,
	PROP_LOCKDISCOVERY,
	PROP_RESOURCETYPE,
	PROP_SOURCE,
	PROP_SUPPORTLOCK,
	PROP_EXECUTABLE,
	PROP_CHECKEDIN,
	PROP_CHECKEDOUT,
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
	char		*value;
};

struct	caldav {
	enum type	 type;
	struct prop	*props;
	size_t		 propsz;
};

__BEGIN_DECLS

typedef void	(*ical_putchar)(int, void *);

void 		 bufappend(struct buf *, const char *, size_t);
void		 bufreset(struct buf *);

int		 ical_putfile(const char *, const struct ical *);
struct ical 	*ical_parse(const char *);
struct ical 	*ical_parsefile(const char *);
void		 ical_free(struct ical *);
void		 ical_printfile(FILE *, const struct ical *);
void		 ical_print(const struct ical *, ical_putchar, void *);

struct caldav	*caldav_parsefile(const char *);
struct caldav 	*caldav_parse(const char *, size_t);
void		 caldav_free(struct caldav *);

const enum proptype *calprops;
const enum calelem *calpropelems;
const char *const *calelems;

__END_DECLS

#endif
