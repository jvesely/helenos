/*	$OpenBSD: input.c,v 1.12 2005/04/13 02:33:08 deraadt Exp $	*/
/*    $NetBSD: input.c,v 1.3 1996/02/06 22:47:33 jtc Exp $    */

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek and Darren F. Provine.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)input.c	8.1 (Berkeley) 5/31/93
 */

/** @addtogroup dummy_load
 * @{
 */
/** @file
 */

/*
 * Top input.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <str.h>

#include "input.h"

#include <async.h>
#include <vfs/vfs.h>
#include <io/console.h>
#include <ipc/console.h>

#define USEC_COUNT 1000000

/* return true iff the given timeval is positive */
#define	TV_POS(tv) \
	((tv)->tv_sec > 0 || ((tv)->tv_sec == 0 && (tv)->tv_usec > 0))

/* subtract timeval `sub' from `res' */
#define	TV_SUB(res, sub) \
	(res)->tv_sec -= (sub)->tv_sec; \
	(res)->tv_usec -= (sub)->tv_usec; \
	if ((res)->tv_usec < 0) { \
		(res)->tv_usec += 1000000; \
		(res)->tv_sec--; \
	}

/* We will use a hack here - if lastchar is non-zero, it is
 * the last character read. We will somehow simulate the select
 * semantics.
 */
static aid_t getchar_inprog = 0;
static char lastchar = '\0';

/*
 * Do a `read wait': select for reading from stdin, with timeout *tvp.
 * On return, modify *tvp to reflect the amount of time spent waiting.
 * It will be positive only if input appeared before the time ran out;
 * otherwise it will be zero or perhaps negative.
 *
 * If tvp is nil, wait forever, but return if select is interrupted.
 *
 * Return 0 => no input, 1 => can read() from stdin
 *
 */
int rwait(struct timeval *tvp)
{
	struct timeval starttv, endtv, *s;
	static ipc_call_t charcall;
	ipcarg_t rc;
	
	/*
	 * Someday, select() will do this for us.
	 * Just in case that day is now, and no one has
	 * changed this, we use a temporary.
	 */
	if (tvp) {
		(void) gettimeofday(&starttv, NULL);
		endtv = *tvp;
		s = &endtv;
	} else
		s = NULL;
	
	if (!lastchar) {
again:
		if (!getchar_inprog) {
			getchar_inprog = async_send_0(fphone(stdin),
			    CONSOLE_GET_EVENT, &charcall);
		}
		
		if (!s)
			async_wait_for(getchar_inprog, &rc);
		else if (async_wait_timeout(getchar_inprog, &rc, s->tv_usec) == ETIMEOUT) {
			tvp->tv_sec = 0;
			tvp->tv_usec = 0;
			return (0);
		}
		
		getchar_inprog = 0;
		if (rc) {
			printf("End of file, bug?\n");
			exit(1);
		}
		
		if (IPC_GET_ARG1(charcall) == KEY_RELEASE)
			goto again;
		
		lastchar = IPC_GET_ARG4(charcall);
	}
	
	if (tvp) {
		/* since there is input, we may not have timed out */
		(void) gettimeofday(&endtv, NULL);
		TV_SUB(&endtv, &starttv);
		TV_SUB(tvp, &endtv);  /* adjust *tvp by elapsed time */
	}
	
	return 1;
}

/*
 * `sleep' for the current turn time (using select).
 * Eat any input that might be available.
 */
void tsleep(unsigned int sec)
{
	struct timeval tv;
	
	tv.tv_sec = 0;
	tv.tv_usec = sec * USEC_COUNT;
	while (TV_POS(&tv))
		if (rwait(&tv)) {
			lastchar = '\0';
		} else
			break;
}

/*
 * getchar with timeout.
 */
int tgetchar(unsigned int sec)
{
	static struct timeval timeleft;
	char c;
	
	/*
	 * Reset timeleft to USEC_COUNT whenever it is not positive.
	 * In any case, wait to see if there is any input.  If so,
	 * take it, and update timeleft so that the next call to
	 * tgetchar() will not wait as long.  If there is no input,
	 * make timeleft zero or negative, and return -1.
	 *
	 * Most of the hard work is done by rwait().
	 */
	if (!TV_POS(&timeleft)) {
		timeleft.tv_sec = 0;
		timeleft.tv_usec = sec * USEC_COUNT;
	}
	
	if (!rwait(&timeleft))
		return -1;
	
	c = lastchar;
	lastchar = '\0';
	return ((int) (unsigned char) c);
}

/** @}
 */
