#ifndef EXTERN_H
#define EXTERN_H

enum	type {
	TYPE_CALMULTIGET,
	TYPE_CALQUERY,
	TYPE_MKCALENDAR,
	TYPE_PROPFIND,
};

enum	calelem {
	CALELEM_CALENDAR_HOME_SET,
	CALELEM_CALENDAR_MULTIGET,
	CALELEM_CALENDAR_QUERY,
	CALELEM_DISPLAYNAME,
	CALELEM_GETETAG,
	CALELEM_HREF,
	CALELEM_MKCALENDAR,
	CALELEM_PRINCIPAL_URL,
	CALELEM_PROP,
	CALELEM_PROPFIND,
	CALELEM_RESOURCETYPE,
	CALELEM__MAX
};

enum	proptype {
	PROP_CALENDAR_HOME_SET,
	PROP_DISPLAYNAME,
	PROP_GETETAG,
	PROP_PRINCIPAL_URL,
	PROP_RESOURCETYPE,
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
	char		*value;
};

struct	caldav {
	enum type	  type;
	struct prop	 *props;
	size_t		  propsz;
	char		**hrefs;
	size_t		  hrefsz;
};

struct	config {
	char		 *displayname;
	char		 *calendarhomeset;
};

__BEGIN_DECLS

typedef void	(*ical_putchar)(int, void *);

void 		 bufappend(struct buf *, const char *, size_t);
void		 bufreset(struct buf *);

int		 ical_merge(struct ical *, const struct ical *);
int		 ical_putfile(const char *, const struct ical *);
struct ical 	*ical_parse(const char *);
struct ical 	*ical_parsefile(const char *);
struct ical 	*ical_parsefile_open(const char *, int *);
int		 ical_parsefile_close(const char *, int);
void		 ical_free(struct ical *);
void		 ical_print(const struct ical *, ical_putchar, void *);
void		 ical_printfile(int, const struct ical *);

struct caldav	*caldav_parsefile(const char *);
struct caldav 	*caldav_parse(const char *, size_t);
void		 caldav_free(struct caldav *);

struct config	*config_parse(const char *);
void		 config_free(struct config *);

const enum proptype *calprops;
const enum calelem *calpropelems;
const char *const *calelems;

__END_DECLS

#endif
