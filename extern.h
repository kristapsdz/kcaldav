#ifndef EXTERN_H
#define EXTERN_H

enum	type {
	TYPE_CALQUERY,
	TYPE_MKCALENDAR,
	TYPE_PROPFIND
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
	PROP_GETLOCKDISCOVERY,
	PROP_RESOURCETYPE,
	PROP_SOURCE,
	PROP_SUPPORTLOCK,
	PROP__MAX
};

struct	buf {
	char		*buf;
	size_t		 sz;
	size_t		 max;
};

struct	ical {
	char		*data;
};

struct	prop {
	enum proptype	 key;
	char		*value;
};

struct	propfind {
	struct prop	*props;
	size_t		 propsz;
};

struct	mkcal {
	struct prop	*props;
	size_t		 propsz;
};

union	data {
	struct mkcal	 mkcal;
	struct propfind	 propfind;
};

struct	caldav {
	enum type	 type;
	union data	 d;
};

__BEGIN_DECLS

void 		 bufappend(struct buf *, const char *, size_t);
void		 bufreset(struct buf *);

struct ical 	*ical_parse(const char *);
void		 ical_free(struct ical *);

struct caldav 	*caldav_parse(const char *, size_t);
void		 caldav_free(struct caldav *);

const enum proptype *calprops;
const char *const *calelems;

__END_DECLS

#endif
