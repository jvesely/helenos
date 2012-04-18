/*
 * Copyright (c) 2012 Maurizio Lombardi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <device/clock_dev.h>
#include <errno.h>
#include <loc.h>
#include <time.h>
#include <malloc.h>
#include <ipc/clock_ctl.h>
#include <getopt.h>
#include <ctype.h>

#define NAME   "date"

static int read_date_from_arg(char *wdate, struct tm *t);
static int read_time_from_arg(char *wdate, struct tm *t);
static int read_num_from_str(char *str, size_t len, int *n);
static void usage(void);

int
main(int argc, char **argv)
{
	int rc, c;
	category_id_t cat_id;
	size_t        svc_cnt;
	service_id_t  *svc_ids = NULL;
	service_id_t  svc_id;
	char          *svc_name = NULL;
	bool          read_only = true;
	char          *wdate = NULL;
	char          *wtime = NULL;
	sysarg_t      battery_ok;
	struct tm     t;

	while ((c = getopt(argc, argv, "hd:t:")) != -1) {
		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'd':
			wdate = (char *)optarg;
			read_only = false;
			break;
		case 't':
			wtime = (char *)optarg;
			read_only = false;
			break;
		case '?':
			usage();
			return 1;
		}
	}

	/* Get the id of the clock category */
	rc = loc_category_get_id("clock", &cat_id, IPC_FLAG_BLOCKING);
	if (rc != EOK) {
		printf(NAME ": Cannot get clock category id\n");
		goto exit;
	}

	/* Get the list of available services in the clock category */
	rc = loc_category_get_svcs(cat_id, &svc_ids, &svc_cnt);
	if (rc != EOK) {
		printf(NAME ": Cannot get the list of services in category \
		    clock\n");
		goto exit;
	}

	/* Check if the are available services in the clock category */
	if (svc_cnt == 0) {
		printf(NAME ": No available service found in \
		    the clock category\n");
		goto exit;
	}

	/* Get the name of the clock service */
	rc = loc_service_get_name(svc_ids[0], &svc_name);
	if (rc != EOK) {
		printf(NAME ": Cannot get the name of the service\n");
		goto exit;
	}

	/* Get the service id for the device */
	rc = loc_service_get_id(svc_name, &svc_id, 0);
	if (rc != EOK) {
		printf(NAME ": Cannot get the service id for device %s",
		    svc_name);
		goto exit;
	}

	/* Connect to the device */
	async_sess_t *sess = loc_service_connect(EXCHANGE_SERIALIZE,
	    svc_id, 0);
	if (!sess) {
		printf(NAME ": Cannot connect to the device\n");
		goto exit;
	}

	/* Check the battery status (if present) */
	async_exch_t *exch = async_exchange_begin(sess);
	rc = async_req_0_1(exch, CLOCK_GET_BATTERY_STATUS, &battery_ok);
	async_exchange_end(exch);

	if (rc == EOK && !battery_ok)
		printf(NAME ": Warning! RTC battery dead\n");

	/* Read the current date/time */
	rc = clock_dev_time_get(sess, &t);
	if (rc != EOK) {
		printf(NAME ": Cannot read the current time\n");
		goto exit;
	}

	if (read_only) {
		/* Print the current time and exit */
		printf("%02d/%02d/%d ", t.tm_mday,
		    t.tm_mon + 1, 1900 + t.tm_year);
		printf("%02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
	} else {
		if (wdate) {
			rc = read_date_from_arg(wdate, &t);
			if (rc != EOK) {
				printf(NAME ": error, date format not "
				    "recognized\n");
				usage();
				goto exit;
			}
		}
		if (wtime) {
			rc = read_time_from_arg(wtime, &t);
			if (rc != EOK) {
				printf(NAME ": error, time format not "
				    "recognized\n");
				usage();
				goto exit;
			}
		}

		rc = clock_dev_time_set(sess, &t);
		if (rc != EOK) {
			printf(NAME ": error, Unable to set date/time\n");
			goto exit;
		}
	}

exit:
	free(svc_name);
	free(svc_ids);
	return rc;
}

static int
read_date_from_arg(char *wdate, struct tm *t)
{
	int rc;

	if (str_size(wdate) != 10) /* DD/MM/YYYY */
		return EINVAL;

	if (wdate[2] != '/' ||
	    wdate[5] != '/') {
		return EINVAL;
	}

	rc = read_num_from_str(&wdate[0], 2, &t->tm_mday);
	if (rc != EOK)
		return rc;

	rc = read_num_from_str(&wdate[3], 2, &t->tm_mon);
	if (rc != EOK)
		return rc;
	t->tm_mon--;

	rc = read_num_from_str(&wdate[6], 4, &t->tm_year);
	t->tm_year -= 1900;
	return rc;
}

static int
read_time_from_arg(char *wtime, struct tm *t)
{
	int rc;
	size_t len = str_size(wtime);
	bool sec_present = len > 5;

	if (len > 8 || len < 5) /* HH:MM[:SS] */
		return EINVAL;

	if (sec_present && wtime[5] != ':')
		return EINVAL;

	if (wtime[2] != ':')
		return EINVAL;

	rc = read_num_from_str(&wtime[0], 2, &t->tm_hour);
	if (rc != EOK)
		return rc;

	rc = read_num_from_str(&wtime[3], 2, &t->tm_min);
	if (rc != EOK)
		return rc;

	if (sec_present)
		rc = read_num_from_str(&wtime[6], 2, &t->tm_sec);
	else
		t->tm_sec = 0;

	return rc;
}

static int
read_num_from_str(char *str, size_t len, int *n)
{
	size_t i;

	*n = 0;

	for (i = 0; i < len; ++i, ++str) {
		if (!isdigit(*str))
			return EINVAL;
		*n *= 10;
		*n += *str - '0';
	}

	return EOK;
}

static void
usage(void)
{
	printf("Usage: date [-d DD/MM/YYYY] [-t HH:MM[:SS]]\n");
	printf("       -d   Change the current date\n");
	printf("       -t   Change the current time\n");
}

