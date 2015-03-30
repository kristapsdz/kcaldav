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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

int
main(int argc, char *argv[])
{
	int		 c;
	struct config	*p = NULL;

	if (-1 != (c = getopt(argc, argv, "")))
		return(EXIT_FAILURE);

	argc -= optind;
	argv += optind;

	if (0 == argc)
		return(EXIT_FAILURE);

	if ((c = config_parse(argv[0], &p, NULL /* FIXME */)) > 0) {
		printf("displayname = [%s]\n",
			NULL == p->displayname ?
			"(none)" : p->displayname);
		printf("emailaddress = [%s]\n",
			NULL == p->emailaddress ?
			"(none)" : p->emailaddress);
		printf("privilege = ");
		if (PERMS_NONE == p->perms)
			printf(" NONE");
		if (PERMS_READ & p->perms) 
			printf(" READ");
		if (PERMS_WRITE & p->perms)
			printf(" WRITE");
		putchar('\n');
	}
	config_free(p);
	return(c <= 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}
