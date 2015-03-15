#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

void
ical_free(struct ical *p)
{

	if (NULL == p)
		return;

	free(p->data);
	free(p);
}

static int
ical_line(const char **cp, struct buf *buf)
{
	const char	*end, *start;

	buf->sz = 0;
	for (start = *cp; ; start = end + 2) {
		if (NULL == (end = strstr(start, "\r\n")))
			return(0);
		bufappend(buf, start, end - start);
		if (' ' != end[2] && '\t' != end[2]) 
			break;
	}

	printf("%s\n", buf->buf);
	*cp = end + 2;
	return(1);

}

struct ical *
ical_parse(const char *cp)
{
	struct ical	*p;
	int		 rc;
	struct buf	 buf;

	memset(&buf, 0, sizeof(struct buf));

	if (NULL == (p = calloc(1, sizeof(struct ical))))
		return(NULL);

	for (rc = 1; '\0' != *cp && 1 == rc; ) 
		rc = ical_line(&cp, &buf);

	if (0 != rc) {
		if (NULL == (p->data = strdup(cp)))
			ical_free(p);
		rc = NULL != p->data;
	} else
		ical_free(p);

	free(buf.buf);
	return(0 == rc ? NULL : p);
}
