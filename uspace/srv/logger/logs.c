/*
 * Copyright (c) 2012 Vojtech Horky
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

/** @addtogroup logger
 * @{
 */
#include <assert.h>
#include <malloc.h>
#include <str.h>
#include <stdio.h>
#include <errno.h>
#include "logger.h"


static FIBRIL_MUTEX_INITIALIZE(log_list_guard);
static LIST_INITIALIZE(log_list);


static logger_log_t *find_log_by_name_and_parent_no_lock(const char *name, logger_log_t *parent)
{
	list_foreach(log_list, it) {
		logger_log_t *log = list_get_instance(it, logger_log_t, link);
		if ((parent == log->parent) && (str_cmp(log->name, name) == 0))
			return log;
	}

	return NULL;
}

static logger_dest_t *create_dest(const char *name)
{
	logger_dest_t *result = malloc(sizeof(logger_dest_t));
	char *logfilename;
	asprintf(&logfilename, "/log/%s", name);
	result->logfile = fopen(logfilename, "a");
	return result;
}

logger_log_t *find_or_create_log(const char *name, sysarg_t parent_id)
{
	logger_log_t *result = NULL;
	logger_log_t *parent = (logger_log_t *) parent_id;

	fibril_mutex_lock(&log_list_guard);

	result = find_log_by_name_and_parent_no_lock(name, parent);
	if (result != NULL)
		goto leave;

	result = malloc(sizeof(logger_log_t));
	if (result == NULL)
		goto leave;


	result->logged_level = LOG_LEVEL_USE_DEFAULT;
	result->name = str_dup(name);
	if (parent == NULL) {
		result->full_name = str_dup(name);
		result->dest = create_dest(name);
	} else {
		asprintf(&result->full_name, "%s/%s", parent->full_name, name);
		result->dest = parent->dest;
	}
	result->parent = parent;

	link_initialize(&result->link);

	list_append(&result->link, &log_list);

leave:
	fibril_mutex_unlock(&log_list_guard);

	return result;
}

logger_log_t *find_log_by_name(const char *name)
{
	logger_log_t *result = NULL;

	fibril_mutex_lock(&log_list_guard);
	list_foreach(log_list, it) {
		logger_log_t *log = list_get_instance(it, logger_log_t, link);
		if (str_cmp(log->full_name, name) == 0) {
			result = log;
			break;
		}
	}
	fibril_mutex_unlock(&log_list_guard);

	return result;
}

logger_log_t *find_log_by_id(sysarg_t id)
{
	logger_log_t *result = NULL;

	fibril_mutex_lock(&log_list_guard);
	list_foreach(log_list, it) {
		logger_log_t *log = list_get_instance(it, logger_log_t, link);
		if ((sysarg_t) log == id) {
			result = log;
			break;
		}
	}
	fibril_mutex_unlock(&log_list_guard);

	return result;
}

static log_level_t get_actual_log_level(logger_log_t *log)
{
	/* Find recursively proper log level. */
	if (log->logged_level == LOG_LEVEL_USE_DEFAULT) {
		if (log->parent == NULL)
			return get_default_logging_level();
		else
			return get_actual_log_level(log->parent);
	}
	return log->logged_level;
}

bool shall_log_message(logger_log_t *log, log_level_t level)
{
	return level <= get_actual_log_level(log);
}

/**
 * @}
 */
