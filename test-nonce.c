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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <bsd/string.h>
#endif
#include <unistd.h>

#include "extern.h"

int verbose = 1;

int
main(int argc, char *argv[])
{
	char		 sfn[22];
	char		 nonce[17];
	char		*np;
	size_t		 i;
	int		 fd;
	enum nonceerr	 er;

	strlcpy(sfn, "/tmp/nonce.XXXXXXXXXX", sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1) {
		kerr("%s: mkstemp", sfn);
		unlink(sfn);
		return(EXIT_FAILURE);
	}
	close(fd);

	for (i = 0; i < 100; i++) {
		snprintf(nonce, sizeof(nonce), "%016zu", i);
		if (NONCE_ERR == (er = nonce_update(sfn, nonce, 0)))
			kerrx("nonce database failure");
		else if (NONCE_NOTFOUND != er)
			kerrx("found nonce!?");
		else
			continue;
		unlink(sfn);
		return(EXIT_FAILURE);
	}

	for (i = 0; i < 2000; i++) {
		if ( ! nonce_new(sfn, &np))
			kerrx("nonce database failure");
		else if (NONCE_ERR == (er = nonce_update(sfn, np, 1)))
			kerrx("nonce database failure");
		else if (NONCE_NOTFOUND == er)
			kerrx("didn't find nonce!?");
		else if (NONCE_ERR == (er = nonce_update(sfn, np, 1)))
			kerrx("nonce database failure");
		else if (NONCE_REPLAY != er) 
			kerrx("replay attack!?");
		else
			continue;
		unlink(sfn);
		return(EXIT_FAILURE);
	}

	unlink(sfn);
	return(EXIT_SUCCESS);
}
