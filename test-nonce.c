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

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libkcaldav.h"
#include "extern.h"

int verbose = 1;

int
main(int argc, char *argv[])
{
	char		 nonce[17];
	char		*np;
	size_t		 i;
	enum nonceerr	 er;

	if ( ! db_init(CALDIR, 0))
		return(EXIT_FAILURE);

	for (i = 0; i < 100; i++) {
		snprintf(nonce, sizeof(nonce), "%016zu", i);
		if (NONCE_ERR == (er = db_nonce_update(nonce, 0)))
			kerrx("nonce database failure");
		else if (NONCE_NOTFOUND != er)
			kerrx("found nonce!?");
		else
			continue;
		return(EXIT_FAILURE);
	}

	for (i = 0; i < 2000; i++) {
		if ( ! db_nonce_new(&np))
			kerrx("nonce database failure");
		else if (NONCE_ERR == (er = db_nonce_update(np, 1)))
			kerrx("nonce database failure");
		else if (NONCE_NOTFOUND == er)
			kerrx("didn't find nonce!?");
		else if (NONCE_ERR == (er = db_nonce_update(np, 1)))
			kerrx("nonce database failure");
		else if (NONCE_REPLAY != er) 
			kerrx("replay attack!?");
		else
			continue;
		return(EXIT_FAILURE);
	}

	return(EXIT_SUCCESS);
}
