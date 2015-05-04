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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgixml.h>

#include "extern.h"

/*
 * Run a series of checks for the nonce validity.
 * This requires us to first open the nonce database read-only and see
 * if we've seen the nonce or not.
 * If we have and it's a replay--bam.
 * Otherwise, re-open the database read-writable and check again.
 * If we find it and it's a replay--bam.
 * If we find it and it's not, update the nonce count.
 * If we don't find it, start over.
 * Return -2 on system failure, -1 on replay, 0 on stale, 1 on ok.
 */
int
httpauth_nonce(const struct httpauth *auth, char **np)
{
	enum nonceerr	 er;

	/*
	 * Now we see whether our nonce lookup fails.
	 * This is still occuring over a read-only database, as an
	 * adversary could be playing us by submitting replay attacks
	 * (or random nonce values) over and over again in the hopes of
	 * filling up our nonce database.
	 */
	er = db_nonce_validate(auth->nonce, auth->count);

	if (NONCE_ERR == er) {
		kerrx("%s: nonce database failure", auth->user);
		return(-2);
	} else if (NONCE_NOTFOUND == er) {
		/*
		 * We don't have the nonce.
		 * This means that the client has either used one of our
		 * bogus initial nonces or is using one from a much
		 * earlier session.
		 * Tell them to retry with a new nonce.
		 */
		if ( ! db_nonce_new(np)) {
			kerrx("%s: nonce database failure", auth->user);
			return(-2);
		}
		return(0);
	} else if (NONCE_REPLAY == er) {
		kerrx("%s: REPLAY ATTACK\n", auth->user);
		return(-1);
	} 

	/*
	 * Now we actually update our nonce file.
	 * We only get here if the nonce value exists and is fresh.
	 */
	er = db_nonce_update(auth->nonce, auth->count);

	if (NONCE_ERR == er) {
		kerrx("%s: nonce database failure", auth->user);
		return(-2);
	} else if (NONCE_NOTFOUND == er) {
		kerrx("%s: nonce update not found?", auth->user);
		if ( ! db_nonce_new(np)) {
			kerrx("%s: nonce database failure", auth->user);
			return(-2);
		}
		return(0);
	} else if (NONCE_REPLAY == er) {
		kerrx("%s: REPLAY ATTACK\n", auth->user);
		return(-1);
	} 

	return(1);
}
