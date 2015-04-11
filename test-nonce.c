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
#include <unistd.h>

#include "extern.h"

int verbose = 1;

int
main(int argc, char *argv[])
{
	char	 sfn[22];
	char	 nonce[17];
	char	*np;
	size_t	 i;
	int	 fd, rc;

	strlcpy(sfn, "/tmp/nonce.XXXXXXXXXX", sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1) {
		kerr("%s: mkstemp", sfn);
		unlink(sfn);
		return(EXIT_FAILURE);
	}
	close(fd);

	for (i = 0; i < 100; i++) {
		snprintf(nonce, sizeof(nonce), "%0.16zu", i);
		if ((rc = nonce_verify(sfn, nonce, 0)) < 0) {
			unlink(sfn);
			return(EXIT_FAILURE);
		} else if (rc > 0) {
			kerrx("found nonce!?");
			unlink(sfn);
			return(EXIT_FAILURE);
		}
	}

	for (i = 0; i < 2000; i++) {
		if (nonce_new(sfn, &np) < 0) {
			unlink(sfn);
			return(EXIT_FAILURE);
		}
		if ((rc = nonce_verify(sfn, np, 1)) < 0) {
			unlink(sfn);
			return(EXIT_FAILURE);
		} else if (0 == rc) {
			kerrx("didn't find nonce!?");
			unlink(sfn);
			return(EXIT_FAILURE);
		} else if ((rc = nonce_verify(sfn, np, 1)) < 0) {
			unlink(sfn);
			return(EXIT_FAILURE);
		} else if (rc > 0) {
			kerrx("replay attack!?");
			unlink(sfn);
			return(EXIT_FAILURE);
		}
	}

	unlink(sfn);
	return(EXIT_SUCCESS);
}
