/*
 * Copyright (c) 2009 Vineeth Pillai
 * Copyright (c) 2014 Jiri Svoboda
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

/** @file
 */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <ddf/driver.h>
#include <ddf/interrupt.h>
#include <ddf/log.h>
#include <device/hw_res_parsed.h>

#define NAME "pl050"

#define PL050_STAT	4
#define PL050_DATA	8

#define PL050_STAT_RXFULL  (1 << 4)

enum {
	IPC_CHAR_READ = DEV_FIRST_CUSTOM_METHOD,
	IPC_CHAR_WRITE,
};

enum {
	buffer_size = 64
};

static int pl050_dev_add(ddf_dev_t *);
static int pl050_fun_online(ddf_fun_t *);
static int pl050_fun_offline(ddf_fun_t *);
static void pl050_char_conn(ipc_callid_t, ipc_call_t *, void *);

static driver_ops_t driver_ops = {
	.dev_add = &pl050_dev_add,
	.fun_online = &pl050_fun_online,
	.fun_offline = &pl050_fun_offline
};

static driver_t pl050_driver = {
	.name = NAME,
	.driver_ops = &driver_ops
};

typedef struct {
	ddf_dev_t *dev;
	ddf_fun_t *fun_a;
	async_sess_t *parent_sess;
	uintptr_t iobase;
	size_t iosize;
	uint8_t buffer[buffer_size];
	size_t buf_rp;
	size_t buf_wp;
	fibril_condvar_t buf_cv;
	fibril_mutex_t buf_lock;
} pl050_t;

static irq_pio_range_t pl050_ranges[] = {
	{
		.base = 0,
		.size = 9,
	}
};

static irq_cmd_t pl050_cmds[] = {
	{
		.cmd = CMD_PIO_READ_8,
		.addr = NULL,
		.dstarg = 1
	},
	{
		.cmd = CMD_AND,
		.value = PL050_STAT_RXFULL,
		.srcarg = 1,
		.dstarg = 3
	},
	{
		.cmd = CMD_PREDICATE,
		.value = 2,
		.srcarg = 3
	},
	{
		.cmd = CMD_PIO_READ_8,
		.addr = NULL,  /* Will be patched in run-time */
		.dstarg = 2
	},
	{
		.cmd = CMD_ACCEPT
	}
};

static irq_code_t pl050_irq_code = {
	sizeof(pl050_ranges) / sizeof(irq_pio_range_t),
	pl050_ranges,
	sizeof(pl050_cmds) / sizeof(irq_cmd_t),
	pl050_cmds
};

static pl050_t *pl050_from_fun(ddf_fun_t *fun)
{
	return (pl050_t *)ddf_dev_data_get(ddf_fun_get_dev(fun));
}

static void pl050_interrupt(ipc_callid_t iid, ipc_call_t *call, ddf_dev_t *dev)
{
	pl050_t *pl050 = (pl050_t *)ddf_dev_data_get(dev);
	size_t nidx;

	fibril_mutex_lock(&pl050->buf_lock);
	nidx = (pl050->buf_wp + 1) % buffer_size;
	if (nidx == pl050->buf_rp) {
		/** Buffer overrunt */
		ddf_msg(LVL_WARN, "Buffer overrun.");
		fibril_mutex_unlock(&pl050->buf_lock);
		return;
	}

	pl050->buffer[pl050->buf_wp] = IPC_GET_ARG2(*call);
	pl050->buf_wp = nidx;
	fibril_condvar_broadcast(&pl050->buf_cv);
	fibril_mutex_unlock(&pl050->buf_lock);
}

static int pl050_init(pl050_t *pl050)
{
	hw_res_list_parsed_t res;
	int rc;

	fibril_mutex_initialize(&pl050->buf_lock);
	fibril_condvar_initialize(&pl050->buf_cv);
	pl050->buf_rp = pl050->buf_wp = 0;

	pl050->parent_sess = ddf_dev_parent_sess_create(pl050->dev,
	    EXCHANGE_SERIALIZE);
	if (pl050->parent_sess == NULL) {
		ddf_msg(LVL_ERROR, "Failed connecitng parent driver.");
		rc = ENOMEM;
		goto error;
	}

	hw_res_list_parsed_init(&res);
	rc = hw_res_get_list_parsed(pl050->parent_sess, &res, 0);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed getting resource list.");
		goto error;
	}

	if (res.mem_ranges.count != 1) {
		ddf_msg(LVL_ERROR, "Expected exactly one memory range.");
		rc = EINVAL;
		goto error;
	}

	pl050->iobase = RNGABS(res.mem_ranges.ranges[0]);
	pl050->iosize = RNGSZ(res.mem_ranges.ranges[0]);

	pl050_irq_code.ranges[0].base = pl050->iobase;
	pl050_irq_code.cmds[0].addr = (void *) pl050->iobase + PL050_STAT;
	pl050_irq_code.cmds[3].addr = (void *) pl050->iobase + PL050_DATA;

	if (res.irqs.count != 1) {
		ddf_msg(LVL_ERROR, "Expected exactly one IRQ.");
		rc = EINVAL;
		goto error;
	}

	ddf_msg(LVL_DEBUG, "iobase=%p irq=%d", (void *)pl050->iobase,
	    res.irqs.irqs[0]);

	rc = register_interrupt_handler(pl050->dev, res.irqs.irqs[0],
	    pl050_interrupt, &pl050_irq_code);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed registering interrupt handler. (%d)",
		    rc);
		goto error;
	}

	return EOK;
error:
	return rc;
}

static int pl050_read(pl050_t *pl050, void *buffer, size_t size)
{
	uint8_t *bp = buffer;
	fibril_mutex_lock(&pl050->buf_lock);

	while (size > 0) {
		while (pl050->buf_rp == pl050->buf_wp)
			fibril_condvar_wait(&pl050->buf_cv, &pl050->buf_lock);
		*bp++ = pl050->buffer[pl050->buf_rp];
		--size;
		pl050->buf_rp = (pl050->buf_rp + 1) % buffer_size;
	}

	fibril_mutex_unlock(&pl050->buf_lock);

	return EOK;
}

static int pl050_write(pl050_t *pl050, void *data, size_t size)
{
	return EOK;
}

void pl050_char_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	pl050_t *pl050 = pl050_from_fun((ddf_fun_t *)arg);

	/* Accept the connection */
	async_answer_0(iid, EOK);

	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);
		sysarg_t method = IPC_GET_IMETHOD(call);
		size_t size = IPC_GET_ARG1(call);

		if (!method) {
			/* The other side has hung up */
			async_answer_0(callid, EOK);
			break;
		}

		switch (method) {
		case IPC_CHAR_READ:
			if (size <= 4 * sizeof(sysarg_t)) {
				sysarg_t message[4] = {};

				pl050_read(pl050, (char *) message, size);
				async_answer_4(callid, size, message[0], message[1],
				    message[2], message[3]);
			} else
				async_answer_0(callid, ELIMIT);
			break;

		case IPC_CHAR_WRITE:
			if (size <= 3 * sizeof(sysarg_t)) {
				const sysarg_t message[3] = {
					IPC_GET_ARG2(call),
					IPC_GET_ARG3(call),
					IPC_GET_ARG4(call)
				};

				pl050_write(pl050, (char *) message, size);
				async_answer_0(callid, size);
			} else
				async_answer_0(callid, ELIMIT);

		default:
			async_answer_0(callid, EINVAL);
		}
	}
}

/** Add device. */
static int pl050_dev_add(ddf_dev_t *dev)
{
	ddf_fun_t *fun_a;
	pl050_t *pl050;
	int rc;

	ddf_msg(LVL_DEBUG, "pl050_dev_add()");

	pl050 = ddf_dev_data_alloc(dev, sizeof(pl050_t));
	if (pl050 == NULL) {
		ddf_msg(LVL_ERROR, "Failed allocating soft state.\n");
		rc = ENOMEM;
		goto error;
	}

	fun_a = ddf_fun_create(dev, fun_inner, "a");
	if (fun_a == NULL) {
		ddf_msg(LVL_ERROR, "Failed creating function 'a'.");
		rc = ENOMEM;
		goto error;
	}

	pl050->fun_a = fun_a;
	pl050->dev = dev;

	rc = pl050_init(pl050);
	if (rc != EOK)
		goto error;

	rc = ddf_fun_add_match_id(fun_a, "char/xtkbd", 10);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed adding match IDs to function %s",
		    "char/xtkbd");
		goto error;
	}

	ddf_fun_set_conn_handler(fun_a, pl050_char_conn);

	rc = ddf_fun_bind(fun_a);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed binding function 'a'. (%d)", rc);
		ddf_fun_destroy(fun_a);
		goto error;
	}

	ddf_msg(LVL_DEBUG, "Device added.");
	return EOK;
error:
	return rc;
}

static int pl050_fun_online(ddf_fun_t *fun)
{
	ddf_msg(LVL_DEBUG, "pl050_fun_online()");
	return ddf_fun_online(fun);
}

static int pl050_fun_offline(ddf_fun_t *fun)
{
	ddf_msg(LVL_DEBUG, "pl050_fun_offline()");
	return ddf_fun_offline(fun);
}

int main(int argc, char *argv[])
{
	int rc;

	printf(NAME ": HelenOS pl050 serial device driver\n");
	rc = ddf_log_init(NAME);
	if (rc != EOK) {
		printf(NAME ": Error connecting logging service.");
		return 1;
	}

	return ddf_driver_main(&pl050_driver);
}

