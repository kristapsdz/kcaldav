/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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

#if HAVE_ERR
# include <err.h>
#endif
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "db.h"

int
main(int argc, char *argv[])
{
	char		 nonce[17];
	char		*np;
	size_t		 i;
	enum nonceerr	 er;

	if (getopt(argc, argv, "") != -1)
		return 1;

	argc -= optind;
	argv += optind;

	if (argc != 1)
		return 1;

	if (!db_init(argv[0], 0))
		errx(1, "db_init");

	for (i = 0; i < 100; i++) {
		snprintf(nonce, sizeof(nonce), "%016zu", i);
		if ((er = db_nonce_update(nonce, 0)) == NONCE_ERR)
			errx(1, "nonce database failure");
		if (er != NONCE_NOTFOUND)
			errx(1, "found nonce!?");
	}

	for (i = 0; i < 100; i++) {
		if (!db_nonce_new(&np))
			errx(1, "nonce database failure");
		if ((er = db_nonce_update(np, 1)) == NONCE_ERR)
			errx(1, "nonce database failure");
		if (er == NONCE_NOTFOUND)
			errx(1, "didn't find nonce!?");
		if ((er = db_nonce_update(np, 1)) == NONCE_ERR)
			errx(1, "nonce database failure");
		if (er != NONCE_REPLAY) 
			errx(1, "replay attack!?");
	}

	return 0;
}
