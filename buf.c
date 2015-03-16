#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

void
bufreset(struct buf *p)
{
	
	p->sz = 0;
	p->buf[0] = '\0';
}

void
bufappend(struct buf *p, const char *s, size_t len)
{

	if (0 == len)
		return;

	if (p->sz + len + 1 > p->max) {
		p->max = p->sz + len + 1024;
		p->buf = realloc(p->buf, p->max);
	}

	memcpy(p->buf + p->sz, s, len);
	p->sz += len;
	p->buf[p->sz] = '\0';
}

