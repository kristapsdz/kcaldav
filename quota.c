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

#ifdef __OpenBSD__
#include <ufs/ufs/quota.h>
#endif
#ifndef __linux__
#include <sys/quota.h>
#endif
#ifdef __linux__
#include <sys/vfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int
quota(const char *file, int fd, 
	long long *used, long long *avail)
{
	struct statfs	 sfs;
#ifndef __linux__
	struct dqblk	 quota;
	int		 fl;
#endif

	if (-1 == fstatfs(fd, &sfs)) {
		fprintf(stderr, "%s: fstafs: %s\n",
			file, strerror(errno));
		return(0);
	} 

	*used = sfs.f_blocks * sfs.f_bsize;
	*avail = sfs.f_bfree * sfs.f_bsize;

#ifndef __linux__
	fl = Q_GETQUOTA | USRQUOTA;
	if (-1 != quotactl(file, fl, getuid(), (char *)&quota)) {
#ifdef __OpenBSD__
		*used = sfs.f_bsize * quota.dqb_curblocks;
		*avail = sfs.f_bsize *
			(quota.dqb_bsoftlimit - quota.dqb_curblocks);
#else
		*used = quota.dqb_curbytes;
		*avail = quota.dqb_bsoftlimit - quota.dqb_curbytes;
#endif
	}
#endif

	return(1);
}
