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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "md5.h"

const char *const icaltypes[ICALTYPE__MAX] = {
	"VCALENDAR", /* ICALTYPE_VCALENDAR */
	"VEVENT", /* ICALTYPE_VEVENT */
	"VTODO", /* ICALTYPE_VTODO */
	"VJOURNAL", /* ICALTYPE_VJOURNAL */
	"VFREEBUSY", /* ICALTYPE_FVREEBUSY */
	"VTIMEZONE", /* ICALTYPE_VTIMEZONE */
	"VALARM", /* ICALTYPE_VALARM */
};

static void
icalnode_free(struct icalnode *p, int first)
{
	struct icalnode	*np;

	if (NULL == p)
		return;
	np = p->next;
	icalnode_free(p->first, 0);
	free(p->name);
	free(p->val);
	free(p);
	if (0 == first)
		icalnode_free(np, 0);
}

void
ical_free(struct ical *p)
{
	struct icalcomp	*c;
	size_t		 i;

	if (NULL == p)
		return;

	for (i = 0; i < ICALTYPE__MAX; i++) {
		while (NULL != (c = p->comps[i])) {
			p->comps[i] = c->next;
			free(c);
		}
	}
	
	icalnode_free(p->first, 0);
	free(p);
}

/*
 * Parse a CRLF-terminated line out of it the iCalendar file.
 * We ignore the last line, so non-CRLF-terminated EOFs are bad.
 * Returns 0 when no CRLF is left, 1 on more lines.
 */
static int
ical_line(const char *cp, struct buf *buf, size_t *pos, size_t sz)
{
	const char	*end;
	size_t		 len;

	buf->sz = 0;
	while (*pos < sz) {
		end = memmem(&cp[*pos], sz - *pos, "\r\n", 2);
		if (NULL == end)
			return(0);
		len = end - &cp[*pos];
		bufappend(buf, &cp[*pos], len);
		if (' ' != end[2] && '\t' != end[2]) 
			break;
		*pos += len + 2;
		while (' ' == cp[*pos] || '\t' == cp[*pos])
			(*pos)++;
	}

	if (*pos < sz)
		*pos += len + 2;
	assert(*pos <= sz);
	return(1);
}

/*
 * We're at a BEGIN statement.
 * See if this statement matchines one of our known components, e.g.,
 * VEVENT.
 * Then set that the iCalendar contains a VEVENT (bit-wise) and also
 * append to a linked list of known VEVENTs.
 * While here, mark that the node is a given type, too.
 */
static int
icalcomp_alloc(struct icalcomp **comps, 
	struct ical *p, struct icalnode *np)
{
	size_t	 i;

	np->type = ICALTYPE__MAX;
	for (i = 0; i < ICALTYPE__MAX; i++) {
		if (strcmp(icaltypes[i], np->val))
			continue;
		p->bits |= 1u << i;
		np->type = i;
		if (NULL != comps[i]) {
			assert(NULL != p->comps[i]);
			comps[i]->next = calloc
				(1, sizeof(struct icalcomp));
			comps[i] = comps[i]->next;
		} else {
			assert(NULL == p->comps[i]);
			comps[i] = calloc
				(1, sizeof(struct icalcomp));
		}

		if (NULL == comps[i]) {
			kerr(NULL);
			return(0);
		} else if (NULL == p->comps[i])
			p->comps[i] = comps[i];

		break;
	}

	return(1);
}

/*
 * Parses an entire file "file" into an iCalendar buffer after
 * establishing a shared advisory lock on "file".
 */
struct ical *
ical_parsefile(const char *file)
{


	return(ical_parsefile_open(file, NULL));
}

/*
 * Close out an ical_parsefile_open() context where "keep" was not NULL,
 * i.e., the file descriptor "fd" is valid.
 * (If "fd" is -1, this function does nothing.)
 * Returns 0 on unlock or close failure.
 * Returns 1 otherwise.
 */
int
ical_parsefile_close(const char *file, int fd)
{

	if (-1 == fd)
		return(1);

	return(close_unlock(file, fd) >= 0);
}

/*
 * Open "file" and dump its contents into an iCalendar object.
 * The type of lock we take out on "file" depends on "keep": if "keep"
 * is non-NULL, we keep the file descriptor open with an exclusive lock
 * (it must be closed with ical_parsefile_close()); otherwise, we use a
 * shared lock and free it when the routine closes.
 * Returns NULL on parse failure, read, memory, or lock failure.
 * Returns the object, otherwise.
 */
struct ical *
ical_parsefile_open(const char *file, int *keep)
{
	int	 	 fd;
	char		*map;
	struct stat	 st;
	struct ical	*p;

	if (NULL != keep)
		*keep = fd = open_lock_ex(file, O_RDONLY, 06000);
	else
		fd = open_lock_sh(file, O_RDONLY, 06000);

	if (-1 == fd)
		return(NULL);

	if (-1 == fstat(fd, &st)) {
		kerr("%s: fstat", file);
		ical_parsefile_close(file, fd);
		return(NULL);
	}

	map = mmap(NULL, st.st_size, 
		PROT_READ, MAP_SHARED, fd, 0);

	if (MAP_FAILED == map) {
		kerr("%s: mmap", file);
		ical_parsefile_close(file, fd);
		return(NULL);
	} 

	p = ical_parse(file, map, st.st_size);
	if (-1 == munmap(map, st.st_size))
		kerr("%s: munmap", file);

	if (NULL == keep && ! ical_parsefile_close(file, fd)) {
		ical_free(p);
		p = NULL;
	}
	return(p);
}

/*
 * This function is very straightforward.
 * Given a buffer "cp" (not necessarily nil-terminated, but CRLF
 * terminated in the way of iCalendar) of size "sz", parse the contained
 * iCalendar and return it.
 * Returns NULL on parse failure or memory exhaustion.
 * Returns the well-formed iCalendar structure otherwise.
 */
struct ical *
ical_parse(const char *file, const char *cp, size_t sz)
{
	struct ical	*p;
	struct icalnode	*cur, *np;
	char		*name, *val;
	struct buf	 buf;
	MD5_CTX		 ctx;
	size_t		 i, pos, line;
	unsigned char	 digest[16];
	const char	*fp;
	struct icalcomp	*comps[ICALTYPE__MAX];

	memset(&buf, 0, sizeof(struct buf));
	memset(comps, 0, sizeof(comps));

	if (NULL == (p = calloc(1, sizeof(struct ical)))) {
		kerr(NULL);
		return(NULL);
	}

	/* Take an MD5 digest of the whole buffer. */
	MD5Init(&ctx);
	MD5Update(&ctx, cp, sz);
	MD5Final(digest, &ctx);
	for (i = 0; i < sizeof(digest); i++) 
	         snprintf(&p->digest[i * 2], 3, "%02x", digest[i]);

	fp = NULL == file ? "<buffer>" : file;
	cur = NULL;

	for (line = pos = 0; pos < sz; ) {
		line++;
		/* iCalendar is line-based: get the next line. */
		if (0 == ical_line(cp, &buf, &pos, sz)) {
			kerrx("%s:%zu: no CRLF", fp, sz);
			break;
		}

		/* Parse the name/value pair from the line. */
		if (NULL == (name = buf.buf)) {
			kerrx("%s:%zu: no line pair", fp, sz);
			break;
		}

		if (NULL != (val = strchr(name, ':')))
			*val++ = '\0';
		if (NULL == val) {
			kerrx("%s:%zu: no value", fp, sz);
			break;
		}

		/* Allocate the line record. */
		if (NULL == (np = calloc(1, sizeof(struct icalnode)))) {
			kerr(NULL);
			break;
		} else if (NULL == (np->name = strdup(name))) {
			kerr(NULL);
			icalnode_free(np, 0);
			break;
		} else if (val && NULL == (np->val = strdup(val))) {
			kerr(NULL);
			icalnode_free(np, 0);
			break;
		}

		/*
		 * If this line defines a component, then scan it and
		 * set the appropriate field on our calendar.
		 * For example, if we're a VTODO, then set the bit on
		 * the calendar object that we contain the VTODO.
		 */
		if (NULL != val && 0 == strcmp("BEGIN", np->name))
			if ( ! icalcomp_alloc(comps, p, np)) {
				icalnode_free(np, 0);
				break;
			}

		/* Handle the first entry and bad nesting. */
		if (NULL == p->first) {
			p->first = cur = np;
			continue;
		} else if (NULL == cur) {
			kerrx("%s:%zu: bad nest", fp, sz);
			break;
		} 

		assert(NULL != cur);
		assert(NULL != np);

		/* Handle empty blocks. */
		if (0 == strcmp("BEGIN", cur->name)) {
			if (0 == strcmp("END", np->name)) {
				np->parent = cur->parent;
				cur->next = np;
				cur = np;
				continue;
			}
			cur->first = np;
			np->parent = cur;
			cur = np;
			continue;
		} 
		
		if (0 == strcmp("END", np->name))
			if (NULL == (cur = cur->parent)) {
				kerrx("%s:%zu: bad nest", fp, sz);
				break;
			}

		np->parent = cur->parent;
		cur->next = np;
		cur = np;
	}

	free(buf.buf);

	/* Handle all sorts of error conditions. */
	if (NULL == p->first)
		kerrx("%s: empty", fp);
	else if (strcmp(p->first->name, "BEGIN") ||
		 strcmp(p->first->val, "VCALENDAR"))
		kerrx("%s: bad root", fp);
	else if (NULL != cur && NULL != cur->parent)
		kerrx("%s: bad nest", fp);
	else if (pos < sz)
		kerrx("%s: bad parse", fp);
	else
		return(p);

	ical_free(p);
	return(NULL);
}

static int
icalnode_putc(int c, void *arg)
{
	int	 fd = *(int *)arg;

	return(write(fd, &c, 1));
}

static int
icalnode_putchar(char c, size_t *col, ical_putchar fp, void *arg)
{

	if (74 == *col) {
		if ((*fp)('\r', arg) < 0)
			return(-1);
		if ((*fp)('\n', arg) < 0)
			return(-1);
		if ((*fp)(' ', arg) < 0)
			return(-1);
		*col = 1;
	}
	if ((*fp)(c, arg) < 0)
		return(-1);
	(*col)++;
	return(1);
}

/*
 * Print an iCalendar using the given callback "fp".
 * We only return zero when "fp" returns <0.
 * Otherwise return non-zero.
 */
static int
icalnode_print(const struct icalnode *p, ical_putchar fp, void *arg)
{
	const char	*cp;
	size_t		 col;

	if (NULL == p) 
		return(1);

	col = 0;
	for (cp = p->name; '\0' != *cp; cp++) 
		if (icalnode_putchar(*cp, &col, fp, arg) < 0)
			return(0);
	if (icalnode_putchar(':', &col, fp, arg) < 0)
		return(0);
	for (cp = p->val; '\0' != *cp; cp++) 
		if (icalnode_putchar(*cp, &col, fp, arg) < 0)
			return(0);
	if ((*fp)('\r', arg) < 0)
		return(0);
	if ((*fp)('\n', arg) < 0)
		return(0);
	if (icalnode_print(p->first, fp, arg) < 0)
		return(0);
	if (icalnode_print(p->next, fp, arg) < 0)
		return(0);
	return(1);
}

/*
 * Print (write) an iCalendar using ical_putchar as a write callback.
 */
int
ical_print(const struct ical *p, ical_putchar fp, void *arg)
{

	return(icalnode_print(p->first, fp, arg));
}

/*
 * Print an iCalendar directly to the given file descriptor.
 */
int
ical_printfile(int fd, const struct ical *p)
{

	return(icalnode_print(p->first, icalnode_putc, &fd));
}
