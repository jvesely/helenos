/*
 * Copyright (c) 2006 Josef Cejka
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

/**
 * @addtogroup kbdgen generic
 * @brief HelenOS generic uspace keyboard handler.
 * @ingroup kbd
 * @{
 */
/** @file
 */

#include <ipc/services.h>
#include <ipc/kbd.h>
#include <sysinfo.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ns.h>
#include <ns_obsolete.h>
#include <async.h>
#include <async_obsolete.h>
#include <errno.h>
#include <adt/fifo.h>
#include <io/console.h>
#include <io/keycode.h>
#include <devmap.h>
#include <kbd.h>
#include <kbd_port.h>
#include <kbd_ctl.h>
#include <layout.h>

// FIXME: remove this header
#include <kernel/ipc/ipc_methods.h>

#define NAME       "kbd"
#define NAMESPACE  "hid_in"

int client_phone = -1;

/** Currently active modifiers. */
static unsigned mods = KM_NUM_LOCK;

/** Currently pressed lock keys. We track these to tackle autorepeat. */
static unsigned lock_keys;

static kbd_port_ops_t *kbd_port;

bool irc_service = false;
int irc_phone = -1;

#define NUM_LAYOUTS 3

static layout_op_t *layout[NUM_LAYOUTS] = {
	&us_qwerty_op,
	&us_dvorak_op,
	&cz_op
};

static int active_layout = 0;

void kbd_push_scancode(int scancode)
{
/*	printf("scancode: 0x%x\n", scancode);*/
	kbd_ctl_parse_scancode(scancode);
}

void kbd_push_ev(int type, unsigned int key)
{
	kbd_event_t ev;
	unsigned mod_mask;

	switch (key) {
	case KC_LCTRL: mod_mask = KM_LCTRL; break;
	case KC_RCTRL: mod_mask = KM_RCTRL; break;
	case KC_LSHIFT: mod_mask = KM_LSHIFT; break;
	case KC_RSHIFT: mod_mask = KM_RSHIFT; break;
	case KC_LALT: mod_mask = KM_LALT; break;
	case KC_RALT: mod_mask = KM_RALT; break;
	default: mod_mask = 0; break;
	}

	if (mod_mask != 0) {
		if (type == KEY_PRESS)
			mods = mods | mod_mask;
		else
			mods = mods & ~mod_mask;
	}

	switch (key) {
	case KC_CAPS_LOCK: mod_mask = KM_CAPS_LOCK; break;
	case KC_NUM_LOCK: mod_mask = KM_NUM_LOCK; break;
	case KC_SCROLL_LOCK: mod_mask = KM_SCROLL_LOCK; break;
	default: mod_mask = 0; break;
	}

	if (mod_mask != 0) {
		if (type == KEY_PRESS) {
			/*
			 * Only change lock state on transition from released
			 * to pressed. This prevents autorepeat from messing
			 * up the lock state.
			 */
			mods = mods ^ (mod_mask & ~lock_keys);
			lock_keys = lock_keys | mod_mask;

			/* Update keyboard lock indicator lights. */
			kbd_ctl_set_ind(mods);
		} else {
			lock_keys = lock_keys & ~mod_mask;
		}
	}
/*
	printf("type: %d\n", type);
	printf("mods: 0x%x\n", mods);
	printf("keycode: %u\n", key);
*/
	if (type == KEY_PRESS && (mods & KM_LCTRL) &&
		key == KC_F1) {
		active_layout = 0;
		layout[active_layout]->reset();
		return;
	}

	if (type == KEY_PRESS && (mods & KM_LCTRL) &&
		key == KC_F2) {
		active_layout = 1;
		layout[active_layout]->reset();
		return;
	}

	if (type == KEY_PRESS && (mods & KM_LCTRL) &&
		key == KC_F3) {
		active_layout = 2;
		layout[active_layout]->reset();
		return;
	}

	ev.type = type;
	ev.key = key;
	ev.mods = mods;

	ev.c = layout[active_layout]->parse_ev(&ev);

	async_obsolete_msg_4(client_phone, KBD_EVENT, ev.type, ev.key, ev.mods, ev.c);
}

static void client_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	ipc_callid_t callid;
	ipc_call_t call;
	int retval;

	async_answer_0(iid, EOK);

	while (true) {
		callid = async_get_call(&call);
		
		if (!IPC_GET_IMETHOD(call)) {
			if (client_phone != -1) {
				async_obsolete_hangup(client_phone);
				client_phone = -1;
			}
			
			async_answer_0(callid, EOK);
			return;
		}
		
		switch (IPC_GET_IMETHOD(call)) {
		case IPC_M_CONNECT_TO_ME:
			if (client_phone != -1) {
				retval = ELIMIT;
				break;
			}
			client_phone = IPC_GET_ARG5(call);
			retval = 0;
			break;
		case KBD_YIELD:
			(*kbd_port->yield)();
			retval = 0;
			break;
		case KBD_RECLAIM:
			(*kbd_port->reclaim)();
			retval = 0;
			break;
		default:
			retval = EINVAL;
		}
		async_answer_0(callid, retval);
	}	
}

static kbd_port_ops_t *kbd_select_port(void)
{
	kbd_port_ops_t *kbd_port;

#if defined(UARCH_amd64)
	kbd_port = &chardev_port;
#elif defined(UARCH_arm32) && defined(MACHINE_gta02)
	kbd_port = &chardev_port;
#elif defined(UARCH_arm32) && defined(MACHINE_testarm)
	kbd_port = &gxemul_port;
#elif defined(UARCH_arm32) && defined(MACHINE_integratorcp)
	kbd_port = &pl050_port;
#elif defined(UARCH_ia32)
	kbd_port = &chardev_port;
#elif defined(MACHINE_i460GX)
	kbd_port = &chardev_port;
#elif defined(MACHINE_ski)
	kbd_port = &ski_port;
#elif defined(MACHINE_msim)
	kbd_port = &msim_port;
#elif defined(MACHINE_lgxemul) || defined(MACHINE_bgxemul)
	kbd_port = &gxemul_port;
#elif defined(UARCH_ppc32)
	kbd_port = &adb_port;
#elif defined(UARCH_sparc64) && defined(PROCESSOR_sun4v)
	kbd_port = &niagara_port;
#elif defined(UARCH_sparc64) && defined(MACHINE_serengeti)
	kbd_port = &sgcn_port;
#elif defined(UARCH_sparc64) && defined(MACHINE_generic)
	kbd_port = &sun_port;
#else
	kbd_port = &dummy_port;
#endif
	return kbd_port;
}

int main(int argc, char **argv)
{
	printf("%s: HelenOS Keyboard service\n", NAME);
	
	sysarg_t fhc;
	sysarg_t obio;
	
	if (((sysinfo_get_value("kbd.cir.fhc", &fhc) == EOK) && (fhc))
	    || ((sysinfo_get_value("kbd.cir.obio", &obio) == EOK) && (obio)))
		irc_service = true;
	
	if (irc_service) {
		while (irc_phone < 0)
			irc_phone = service_obsolete_connect_blocking(SERVICE_IRC, 0, 0);
	}
	
	/* Select port driver. */
	kbd_port = kbd_select_port();

	/* Initialize port driver. */
	if ((*kbd_port->init)() != 0)
		return -1;

	/* Initialize controller driver. */
	if (kbd_ctl_init(kbd_port) != 0)
		return -1;

	/* Initialize (reset) layout. */
	layout[active_layout]->reset();
	
	/* Register driver */
	int rc = devmap_driver_register(NAME, client_connection);
	if (rc < 0) {
		printf("%s: Unable to register driver (%d)\n", NAME, rc);
		return -1;
	}
	
	char kbd[DEVMAP_NAME_MAXLEN + 1];
	snprintf(kbd, DEVMAP_NAME_MAXLEN, "%s/%s", NAMESPACE, NAME);
	
	devmap_handle_t devmap_handle;
	if (devmap_device_register(kbd, &devmap_handle) != EOK) {
		printf("%s: Unable to register device %s\n", NAME, kbd);
		return -1;
	}

	printf(NAME ": Accepting connections\n");
	async_manager();

	/* Not reached. */
	return 0;
}

/**
 * @}
 */ 
