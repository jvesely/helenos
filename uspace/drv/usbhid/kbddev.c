/*
 * Copyright (c) 2011 Lubos Slovak
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

/** @addtogroup drvusbhid
 * @{
 */
/**
 * @file
 * USB HID keyboard device structure and API.
 */

#include <errno.h>
#include <str_error.h>
#include <stdio.h>

#include <io/keycode.h>
#include <ipc/kbd.h>
#include <async.h>
#include <fibril.h>
#include <fibril_synch.h>

#include <usb/usb.h>
#include <usb/dp.h>
#include <usb/request.h>
#include <usb/classes/hid.h>
#include <usb/pipes.h>
#include <usb/debug.h>
#include <usb/classes/hidparser.h>
#include <usb/classes/classes.h>
#include <usb/classes/hidut.h>

#include <usb/devdrv.h>

#include "kbddev.h"
#include "hiddev.h"
#include "hidreq.h"
#include "layout.h"
#include "conv.h"
#include "kbdrepeat.h"

/*----------------------------------------------------------------------------*/
/** Default modifiers when the keyboard is initialized. */
static const unsigned DEFAULT_ACTIVE_MODS = KM_NUM_LOCK;

/** Boot protocol report size (key part). */
static const size_t BOOTP_REPORT_SIZE = 6;

/** Boot protocol total report size. */
static const size_t BOOTP_BUFFER_SIZE = 8;

/** Boot protocol output report size. */
static const size_t BOOTP_BUFFER_OUT_SIZE = 1;

/** Boot protocol error key code. */
static const uint8_t BOOTP_ERROR_ROLLOVER = 1;

/** Default idle rate for keyboards. */
static const uint8_t IDLE_RATE = 0;

/** Delay before a pressed key starts auto-repeating. */
static const unsigned int DEFAULT_DELAY_BEFORE_FIRST_REPEAT = 500 * 1000;

/** Delay between two repeats of a pressed key when auto-repeating. */
static const unsigned int DEFAULT_REPEAT_DELAY = 50 * 1000;

/** Keyboard polling endpoint description for boot protocol class. */
static usb_endpoint_description_t boot_poll_endpoint_description = {
	.transfer_type = USB_TRANSFER_INTERRUPT,
	.direction = USB_DIRECTION_IN,
	.interface_class = USB_CLASS_HID,
	.interface_subclass = USB_HID_SUBCLASS_BOOT,
	.interface_protocol = USB_HID_PROTOCOL_KEYBOARD,
	.flags = 0
};

/** General keyboard polling endpoint description. */
//static usb_endpoint_description_t general_poll_endpoint_description = {
//	.transfer_type = USB_TRANSFER_INTERRUPT,
//	.direction = USB_DIRECTION_IN,
//	.interface_class = USB_CLASS_HID,
//	.flags = 0
//};

/* Array of endpoints expected on the device, NULL terminated. */
usb_endpoint_description_t 
    *usbhid_kbd_endpoints[USBHID_KBD_POLL_EP_COUNT + 1] = {
	&boot_poll_endpoint_description,
//	&general_poll_endpoint_description,
	NULL
};


typedef enum usbhid_kbd_flags {
	USBHID_KBD_STATUS_UNINITIALIZED = 0,
	USBHID_KBD_STATUS_INITIALIZED = 1,
	USBHID_KBD_STATUS_TO_DESTROY = -1
} usbhid_kbd_flags;

/*----------------------------------------------------------------------------*/
/* Keyboard layouts                                                           */
/*----------------------------------------------------------------------------*/

#define NUM_LAYOUTS 3

/** Keyboard layout map. */
static layout_op_t *layout[NUM_LAYOUTS] = {
	&us_qwerty_op,
	&us_dvorak_op,
	&cz_op
};

static int active_layout = 0;

/*----------------------------------------------------------------------------*/
/* Modifier constants                                                         */
/*----------------------------------------------------------------------------*/
/** Mapping of USB modifier key codes to generic modifier key codes. */
static const keycode_t usbhid_modifiers_keycodes[USB_HID_MOD_COUNT] = {
	KC_LCTRL,         /* USB_HID_MOD_LCTRL */
	KC_LSHIFT,        /* USB_HID_MOD_LSHIFT */
	KC_LALT,          /* USB_HID_MOD_LALT */
	0,                /* USB_HID_MOD_LGUI */
	KC_RCTRL,         /* USB_HID_MOD_RCTRL */
	KC_RSHIFT,        /* USB_HID_MOD_RSHIFT */
	KC_RALT,          /* USB_HID_MOD_RALT */
	0,                /* USB_HID_MOD_RGUI */
};

typedef enum usbhid_lock_code {
	USBHID_LOCK_NUM = 0x53,
	USBHID_LOCK_CAPS = 0x39,
	USBHID_LOCK_SCROLL = 0x47,
	USBHID_LOCK_COUNT = 3
} usbhid_lock_code;

static const usbhid_lock_code usbhid_lock_codes[USBHID_LOCK_COUNT] = {
	USBHID_LOCK_NUM,
	USBHID_LOCK_CAPS,
	USBHID_LOCK_SCROLL
};

/*----------------------------------------------------------------------------*/
/* IPC method handler                                                         */
/*----------------------------------------------------------------------------*/

static void default_connection_handler(ddf_fun_t *, ipc_callid_t, ipc_call_t *);
ddf_dev_ops_t keyboard_ops = {
	.default_handler = default_connection_handler
};

/** 
 * Default handler for IPC methods not handled by DDF.
 *
 * Currently recognizes only one method (IPC_M_CONNECT_TO_ME), in which case it
 * assumes the caller is the console and thus it stores IPC phone to it for 
 * later use by the driver to notify about key events.
 *
 * @param fun Device function handling the call.
 * @param icallid Call id.
 * @param icall Call data.
 */
void default_connection_handler(ddf_fun_t *fun,
    ipc_callid_t icallid, ipc_call_t *icall)
{
	sysarg_t method = IPC_GET_IMETHOD(*icall);
	
	usbhid_kbd_t *kbd_dev = (usbhid_kbd_t *)fun->driver_data;
	assert(kbd_dev != NULL);

	if (method == IPC_M_CONNECT_TO_ME) {
		int callback = IPC_GET_ARG5(*icall);

		if (kbd_dev->console_phone != -1) {
			async_answer_0(icallid, ELIMIT);
			return;
		}

		kbd_dev->console_phone = callback;
		async_answer_0(icallid, EOK);
		return;
	}
	
	async_answer_0(icallid, EINVAL);
}

/*----------------------------------------------------------------------------*/
/* Key processing functions                                                   */
/*----------------------------------------------------------------------------*/
/**
 * Handles turning of LED lights on and off.
 *
 * In case of USB keyboards, the LEDs are handled in the driver, not in the 
 * device. When there should be a change (lock key was pressed), the driver
 * uses a Set_Report request sent to the device to set the state of the LEDs.
 *
 * This functions sets the LED lights according to current settings of modifiers
 * kept in the keyboard device structure.
 *
 * @param kbd_dev Keyboard device structure.
 */
static void usbhid_kbd_set_led(usbhid_kbd_t *kbd_dev) 
{
	uint8_t buffer[BOOTP_BUFFER_OUT_SIZE];
	int rc= 0;
	
	memset(buffer, 0, BOOTP_BUFFER_OUT_SIZE);
	uint8_t leds = 0;

	if (kbd_dev->mods & KM_NUM_LOCK) {
		leds |= USB_HID_LED_NUM_LOCK;
	}
	
	if (kbd_dev->mods & KM_CAPS_LOCK) {
		leds |= USB_HID_LED_CAPS_LOCK;
	}
	
	if (kbd_dev->mods & KM_SCROLL_LOCK) {
		leds |= USB_HID_LED_SCROLL_LOCK;
	}

	// TODO: COMPOSE and KANA
	
	usb_log_debug("Creating output report.\n");
	usb_log_debug("Leds: 0x%x\n", leds);
	if ((rc = usb_hid_boot_keyboard_output_report(
	    leds, buffer, BOOTP_BUFFER_OUT_SIZE)) != EOK) {
		usb_log_warning("Error composing output report to the keyboard:"
		    "%s.\n", str_error(rc));
		return;
	}
	
	usb_log_debug("Output report buffer: %s\n", 
	    usb_debug_str_buffer(buffer, BOOTP_BUFFER_OUT_SIZE, 0));
	
	assert(kbd_dev->usb_dev != NULL);
	
	usbhid_req_set_report(&kbd_dev->usb_dev->ctrl_pipe, 
	    kbd_dev->usb_dev->interface_no, USB_HID_REPORT_TYPE_OUTPUT, 
	    buffer, BOOTP_BUFFER_OUT_SIZE);
}

/*----------------------------------------------------------------------------*/
/**
 * Processes key events.
 *
 * @note This function was copied from AT keyboard driver and modified to suit
 *       USB keyboard.
 *
 * @note Lock keys are not sent to the console, as they are completely handled
 *       in the driver. It may, however, be required later that the driver
 *       sends also these keys to application (otherwise it cannot use those
 *       keys at all).
 * 
 * @param kbd_dev Keyboard device structure.
 * @param type Type of the event (press / release). Recognized values: 
 *             KEY_PRESS, KEY_RELEASE
 * @param key Key code of the key according to HID Usage Tables.
 */
void usbhid_kbd_push_ev(usbhid_kbd_t *kbd_dev, int type, unsigned int key)
{
	console_event_t ev;
	unsigned mod_mask;

	/*
	 * These parts are copy-pasted from the AT keyboard driver.
	 *
	 * They definitely require some refactoring, but will keep it for later
	 * when the console and keyboard system is changed in HelenOS.
	 */
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
			kbd_dev->mods = kbd_dev->mods | mod_mask;
		else
			kbd_dev->mods = kbd_dev->mods & ~mod_mask;
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
			unsigned int locks_old = kbd_dev->lock_keys;
			
			kbd_dev->mods = 
			    kbd_dev->mods ^ (mod_mask & ~kbd_dev->lock_keys);
			kbd_dev->lock_keys = kbd_dev->lock_keys | mod_mask;

			/* Update keyboard lock indicator lights. */
			if (kbd_dev->lock_keys != locks_old) {
				usbhid_kbd_set_led(kbd_dev);
			}
		} else {
			kbd_dev->lock_keys = kbd_dev->lock_keys & ~mod_mask;
		}
	}

	if (key == KC_CAPS_LOCK || key == KC_NUM_LOCK || key == KC_SCROLL_LOCK) {
		// do not send anything to the console, this is our business
		return;
	}
	
	if (type == KEY_PRESS && (kbd_dev->mods & KM_LCTRL) && key == KC_F1) {
		active_layout = 0;
		layout[active_layout]->reset();
		return;
	}

	if (type == KEY_PRESS && (kbd_dev->mods & KM_LCTRL) && key == KC_F2) {
		active_layout = 1;
		layout[active_layout]->reset();
		return;
	}

	if (type == KEY_PRESS && (kbd_dev->mods & KM_LCTRL) && key == KC_F3) {
		active_layout = 2;
		layout[active_layout]->reset();
		return;
	}
	
	ev.type = type;
	ev.key = key;
	ev.mods = kbd_dev->mods;

	ev.c = layout[active_layout]->parse_ev(&ev);

	usb_log_debug2("Sending key %d to the console\n", ev.key);
	if (kbd_dev->console_phone < 0) {
		usb_log_warning(
		    "Connection to console not ready, key discarded.\n");
		return;
	}
	
	async_msg_4(kbd_dev->console_phone, KBD_EVENT, ev.type, ev.key, 
	    ev.mods, ev.c);
}

/*----------------------------------------------------------------------------*/
/**
 * Checks if modifiers were pressed or released and generates key events.
 *
 * @param kbd_dev Keyboard device structure.
 * @param modifiers Bitmap of modifiers.
 *
 * @sa usbhid_kbd_push_ev()
 */
//static void usbhid_kbd_check_modifier_changes(usbhid_kbd_t *kbd_dev, 
//    const uint8_t *key_codes, size_t count)
//{
//	/*
//	 *  why the USB keyboard has NUM_, SCROLL_ and CAPS_LOCK
//	 *       both as modifiers and as keyUSB_HID_LOCK_COUNTs with their own scancodes???
//	 *
//	 * modifiers should be sent as normal keys to usbhid_parse_scancode()!!
//	 * so maybe it would be better if I received it from report parser in 
//	 * that way
//	 */
	
//	int i;
//	for (i = 0; i < count; ++i) {
//		if ((modifiers & usb_hid_modifiers_consts[i]) &&
//		    !(kbd_dev->modifiers & usb_hid_modifiers_consts[i])) {
//			// modifier pressed
//			if (usbhid_modifiers_keycodes[i] != 0) {
//				usbhid_kbd_push_ev(kbd_dev, KEY_PRESS, 
//				    usbhid_modifiers_keycodes[i]);
//			}
//		} else if (!(modifiers & usb_hid_modifiers_consts[i]) &&
//		    (kbd_dev->modifiers & usb_hid_modifiers_consts[i])) {
//			// modifier released
//			if (usbhid_modifiers_keycodes[i] != 0) {
//				usbhid_kbd_push_ev(kbd_dev, KEY_RELEASE, 
//				    usbhid_modifiers_keycodes[i]);
//			}
//		}	// no change
//	}
	
//	kbd_dev->modifiers = modifiers;
//}

/*----------------------------------------------------------------------------*/

static inline int usbhid_kbd_is_lock(unsigned int key_code) 
{
	return (key_code == KC_NUM_LOCK
	    || key_code == KC_SCROLL_LOCK
	    || key_code == KC_CAPS_LOCK);
}

/*----------------------------------------------------------------------------*/
/**
 * Checks if some keys were pressed or released and generates key events.
 *
 * An event is created only when key is pressed or released. Besides handling
 * the events (usbhid_kbd_push_ev()), the auto-repeat fibril is notified about
 * key presses and releases (see usbhid_kbd_repeat_start() and 
 * usbhid_kbd_repeat_stop()).
 *
 * @param kbd_dev Keyboard device structure.
 * @param key_codes Parsed keyboard report - codes of currently pressed keys 
 *                  according to HID Usage Tables.
 * @param count Number of key codes in report (size of the report).
 *
 * @sa usbhid_kbd_push_ev(), usbhid_kbd_repeat_start(), usbhid_kbd_repeat_stop()
 */
static void usbhid_kbd_check_key_changes(usbhid_kbd_t *kbd_dev, 
    const uint8_t *key_codes, size_t count)
{
	unsigned int key;
	unsigned int i, j;
	
	/*
	 * First of all, check if the kbd have reported phantom state.
	 *
	 *  this must be changed as we don't know which keys are modifiers
	 *       and which are regular keys.
	 */
	i = 0;
	// all fields should report Error Rollover
	while (i < count &&
	    key_codes[i] == BOOTP_ERROR_ROLLOVER) {
		++i;
	}
	if (i == count) {
		usb_log_debug("Phantom state occured.\n");
		// phantom state, do nothing
		return;
	}
	
	/* TODO: quite dummy right now, think of better implementation */
	assert(count == kbd_dev->key_count);
	
	/*
	 * 1) Key releases
	 */
	for (j = 0; j < count; ++j) {
		// try to find the old key in the new key list
		i = 0;
		while (i < kbd_dev->key_count
		    && key_codes[i] != kbd_dev->keys[j]) {
			++i;
		}
		
		if (i == count) {
			// not found, i.e. the key was released
			key = usbhid_parse_scancode(kbd_dev->keys[j]);
			if (!usbhid_kbd_is_lock(key)) {
				usbhid_kbd_repeat_stop(kbd_dev, key);
			}
			usbhid_kbd_push_ev(kbd_dev, KEY_RELEASE, key);
			usb_log_debug2("Key released: %d\n", key);
		} else {
			// found, nothing happens
		}
	}
	
	/*
	 * 1) Key presses
	 */
	for (i = 0; i < kbd_dev->key_count; ++i) {
		// try to find the new key in the old key list
		j = 0;
		while (j < count && kbd_dev->keys[j] != key_codes[i]) { 
			++j;
		}
		
		if (j == count) {
			// not found, i.e. new key pressed
			key = usbhid_parse_scancode(key_codes[i]);
			usb_log_debug2("Key pressed: %d (keycode: %d)\n", key,
			    key_codes[i]);
			usbhid_kbd_push_ev(kbd_dev, KEY_PRESS, key);
			if (!usbhid_kbd_is_lock(key)) {
				usbhid_kbd_repeat_start(kbd_dev, key);
			}
		} else {
			// found, nothing happens
		}
	}
	
	memcpy(kbd_dev->keys, key_codes, count);

	usb_log_debug("New stored keycodes: %s\n", 
	    usb_debug_str_buffer(kbd_dev->keys, kbd_dev->key_count, 0));
}

/*----------------------------------------------------------------------------*/
/* Callbacks for parser                                                       */
/*----------------------------------------------------------------------------*/
/**
 * Callback function for the HID report parser.
 *
 * This function is called by the HID report parser with the parsed report.
 * The parsed report is used to check if any events occured (key was pressed or
 * released, modifier was pressed or released).
 *
 * @param key_codes Parsed keyboard report - codes of currently pressed keys 
 *                  according to HID Usage Tables.
 * @param count Number of key codes in report (size of the report).
 * @param modifiers Bitmap of modifiers (Ctrl, Alt, Shift, GUI).
 * @param arg User-specified argument. Expects pointer to the keyboard device
 *            structure representing the keyboard.
 *
 * @sa usbhid_kbd_check_key_changes(), usbhid_kbd_check_modifier_changes()
 */
static void usbhid_kbd_process_keycodes(const uint8_t *key_codes, size_t count,
    uint8_t modifiers, void *arg)
{
	if (arg == NULL) {
		usb_log_warning("Missing argument in callback "
		    "usbhid_process_keycodes().\n");
		return;
	}
	
	usbhid_kbd_t *kbd_dev = (usbhid_kbd_t *)arg;
	assert(kbd_dev != NULL);

	usb_log_debug("Got keys from parser: %s\n", 
	    usb_debug_str_buffer(key_codes, count, 0));
	
	if (count != kbd_dev->key_count) {
		usb_log_warning("Number of received keycodes (%d) differs from"
		    " expected number (%d).\n", count, kbd_dev->key_count);
		return;
	}
	
	///usbhid_kbd_check_modifier_changes(kbd_dev, key_codes, count);
	usbhid_kbd_check_key_changes(kbd_dev, key_codes, count);
}

/*----------------------------------------------------------------------------*/
/* General kbd functions                                                      */
/*----------------------------------------------------------------------------*/
/**
 * Processes data received from the device in form of report.
 *
 * This function uses the HID report parser to translate the data received from
 * the device into generic USB HID key codes and into generic modifiers bitmap.
 * The parser then calls the given callback (usbhid_kbd_process_keycodes()).
 *
 * @note Currently, only the boot protocol is supported.
 *
 * @param kbd_dev Keyboard device structure (must be initialized).
 * @param buffer Data from the keyboard (i.e. the report).
 * @param actual_size Size of the data from keyboard (report size) in bytes.
 *
 * @sa usbhid_kbd_process_keycodes(), usb_hid_boot_keyboard_input_report(),
 *     usb_hid_parse_report().
 */
static void usbhid_kbd_process_data(usbhid_kbd_t *kbd_dev,
                                    uint8_t *buffer, size_t actual_size)
{
	assert(kbd_dev->initialized == USBHID_KBD_STATUS_INITIALIZED);
	assert(kbd_dev->parser != NULL);
	
	usb_hid_report_in_callbacks_t *callbacks =
	    (usb_hid_report_in_callbacks_t *)malloc(
	        sizeof(usb_hid_report_in_callbacks_t));
	
	callbacks->keyboard = usbhid_kbd_process_keycodes;

	usb_log_debug("Calling usb_hid_parse_report() with "
	    "buffer %s\n", usb_debug_str_buffer(buffer, actual_size, 0));
	
//	int rc = usb_hid_boot_keyboard_input_report(buffer, actual_size,
//	    callbacks, kbd_dev);
	int rc = usb_hid_parse_report(kbd_dev->parser, buffer,
	    actual_size, callbacks, kbd_dev);
	
	if (rc != EOK) {
		usb_log_warning("Error in usb_hid_boot_keyboard_input_report():"
		    "%s\n", str_error(rc));
	}
}

/*----------------------------------------------------------------------------*/
/* HID/KBD structure manipulation                                             */
/*----------------------------------------------------------------------------*/

static void usbhid_kbd_mark_unusable(usbhid_kbd_t *kbd_dev)
{
	kbd_dev->initialized = USBHID_KBD_STATUS_TO_DESTROY;
}

/*----------------------------------------------------------------------------*/
/* HID/KBD polling                                                            */
/*----------------------------------------------------------------------------*/
/**
 * Main keyboard polling function.
 *
 * This function uses the Interrupt In pipe of the keyboard to poll for events.
 * The keyboard is initialized in a way that it reports only when a key is 
 * pressed or released, so there is no actual need for any sleeping between
 * polls (see usbhid_kbd_try_add_device() or usbhid_kbd_init()).
 *
 * @param kbd_dev Initialized keyboard structure representing the device to 
 *                poll.
 *
 * @sa usbhid_kbd_process_data()
 */
//static void usbhid_kbd_poll(usbhid_kbd_t *kbd_dev)
//{
//	int rc, sess_rc;
//	uint8_t buffer[BOOTP_BUFFER_SIZE];
//	size_t actual_size;
	
//	usb_log_debug("Polling keyboard...\n");
	
//	if (!kbd_dev->initialized) {
//		usb_log_error("HID/KBD device not initialized!\n");
//		return;
//	}
	
//	assert(kbd_dev->hid_dev != NULL);
//	assert(kbd_dev->hid_dev->initialized);

//	while (true) {
//		sess_rc = usb_pipe_start_session(
//		    &kbd_dev->hid_dev->poll_pipe);
//		if (sess_rc != EOK) {
//			usb_log_warning("Failed to start a session: %s.\n",
//			    str_error(sess_rc));
//			break;
//		}

//		rc = usb_pipe_read(&kbd_dev->hid_dev->poll_pipe, 
//		    buffer, BOOTP_BUFFER_SIZE, &actual_size);
		
//		sess_rc = usb_pipe_end_session(
//		    &kbd_dev->hid_dev->poll_pipe);

//		if (rc != EOK) {
//			usb_log_warning("Error polling the keyboard: %s.\n",
//			    str_error(rc));
//			break;
//		}

//		if (sess_rc != EOK) {
//			usb_log_warning("Error closing session: %s.\n",
//			    str_error(sess_rc));
//			break;
//		}

//		/*
//		 * If the keyboard answered with NAK, it returned no data.
//		 * This implies that no change happened since last query.
//		 */
//		if (actual_size == 0) {
//			usb_log_debug("Keyboard returned NAK\n");
//			continue;
//		}

//		/*
//		 * TODO: Process pressed keys.
//		 */
//		usb_log_debug("Calling usbhid_kbd_process_data()\n");
//		usbhid_kbd_process_data(kbd_dev, buffer, actual_size);
		
//		// disabled for now, no reason to sleep
//		//async_usleep(kbd_dev->hid_dev->poll_interval);
//	}
//}

/*----------------------------------------------------------------------------*/
/**
 * Function executed by the main driver fibril.
 *
 * Just starts polling the keyboard for events.
 * 
 * @param arg Initialized keyboard device structure (of type usbhid_kbd_t) 
 *            representing the device.
 *
 * @retval EOK if the fibril finished polling the device.
 * @retval EINVAL if no device was given in the argument.
 *
 * @sa usbhid_kbd_poll()
 *
 * @todo Change return value - only case when the fibril finishes is in case
 *       of some error, so the error should probably be propagated from function
 *       usbhid_kbd_poll() to here and up.
 */
//static int usbhid_kbd_fibril(void *arg)
//{
//	if (arg == NULL) {
//		usb_log_error("No device!\n");
//		return EINVAL;
//	}
	
//	usbhid_kbd_t *kbd_dev = (usbhid_kbd_t *)arg;

//	usbhid_kbd_poll(kbd_dev);
	
//	// as there is another fibril using this device, so we must leave the
//	// structure to it, but mark it for destroying.
//	usbhid_kbd_mark_unusable(kbd_dev);
//	// at the end, properly destroy the KBD structure
////	usbhid_kbd_free(&kbd_dev);
////	assert(kbd_dev == NULL);

//	return EOK;
//}

static int usbhid_dev_get_report_descriptor(usbhid_kbd_t *kbd_dev)
{
	assert(kbd_dev != NULL);
	assert(kbd_dev->usb_dev != NULL);
	assert(kbd_dev->usb_dev->interface_no >= 0);
	
	usb_dp_parser_t parser =  {
		.nesting = usb_dp_standard_descriptor_nesting
	};
	
	usb_dp_parser_data_t parser_data = {
		.data = kbd_dev->usb_dev->descriptors.configuration,
		.size = kbd_dev->usb_dev->descriptors.configuration_size,
		.arg = NULL
	};
	
	/*
	 * First nested descriptor of the configuration descriptor.
	 */
	uint8_t *d = 
	    usb_dp_get_nested_descriptor(&parser, &parser_data, 
	    kbd_dev->usb_dev->descriptors.configuration);
	
	/*
	 * Find the interface descriptor corresponding to our interface number.
	 */
	int i = 0;
	while (d != NULL && i < kbd_dev->usb_dev->interface_no) {
		d = usb_dp_get_sibling_descriptor(&parser, &parser_data, 
		    kbd_dev->usb_dev->descriptors.configuration, d);
	}
	
	if (d == NULL) {
		usb_log_fatal("The %. interface descriptor not found!\n",
		    kbd_dev->usb_dev->interface_no);
		return ENOENT;
	}
	
	/*
	 * First nested descriptor of the interface descriptor.
	 */
	uint8_t *iface_desc = d;
	d = usb_dp_get_nested_descriptor(&parser, &parser_data, iface_desc);
	
	/*
	 * Search through siblings until the HID descriptor is found.
	 */
	while (d != NULL && *(d + 1) != USB_DESCTYPE_HID) {
		d = usb_dp_get_sibling_descriptor(&parser, &parser_data, 
		    iface_desc, d);
	}
	
	if (d == NULL) {
		usb_log_fatal("No HID descriptor found!\n");
		return ENOENT;
	}
	
	if (*d != sizeof(usb_standard_hid_descriptor_t)) {
		usb_log_fatal("HID descriptor hass wrong size (%u, expected %u"
		    ")\n", *d, sizeof(usb_standard_hid_descriptor_t));
		return EINVAL;
	}
	
	usb_standard_hid_descriptor_t *hid_desc = 
	    (usb_standard_hid_descriptor_t *)d;
	
	uint16_t length =  hid_desc->report_desc_info.length;
	size_t actual_size = 0;
	
	/*
	 * Start session for the control transfer.
	 */
	int sess_rc = usb_pipe_start_session(&kbd_dev->usb_dev->ctrl_pipe);
	if (sess_rc != EOK) {
		usb_log_warning("Failed to start a session: %s.\n",
		    str_error(sess_rc));
		return sess_rc;
	}

	/*
	 * Allocate space for the report descriptor.
	 */
	kbd_dev->report_desc = (uint8_t *)malloc(length);
	if (kbd_dev->report_desc == NULL) {
		usb_log_fatal("Failed to allocate space for Report descriptor."
		    "\n");
		return ENOMEM;
	}
	
	usb_log_debug("Getting Report descriptor, expected size: %u\n", length);
	
	/*
	 * Get the descriptor from the device.
	 */
	int rc = usb_request_get_descriptor(&kbd_dev->usb_dev->ctrl_pipe,
	    USB_REQUEST_TYPE_STANDARD, USB_REQUEST_RECIPIENT_INTERFACE,
	    USB_DESCTYPE_HID_REPORT, 0, kbd_dev->usb_dev->interface_no,
	    kbd_dev->report_desc, length, &actual_size);

	if (rc != EOK) {
		free(kbd_dev->report_desc);
		kbd_dev->report_desc = NULL;
		return rc;
	}

	if (actual_size != length) {
		free(kbd_dev->report_desc);
		kbd_dev->report_desc = NULL;
		usb_log_fatal("Report descriptor has wrong size (%u, expected "
		    "%u)\n", actual_size, length);
		return EINVAL;
	}
	
	/*
	 * End session for the control transfer.
	 */
	sess_rc = usb_pipe_end_session(&kbd_dev->usb_dev->ctrl_pipe);
	if (sess_rc != EOK) {
		usb_log_warning("Failed to end a session: %s.\n",
		    str_error(sess_rc));
		free(kbd_dev->report_desc);
		kbd_dev->report_desc = NULL;
		return sess_rc;
	}
	
	kbd_dev->report_desc_size = length;
	
	usb_log_debug("Done.\n");
	
	return EOK;
}

/*----------------------------------------------------------------------------*/

static int usbhid_kbd_process_report_descriptor(usbhid_kbd_t *kbd_dev)
{
	int rc = usbhid_dev_get_report_descriptor(kbd_dev);
	
	if (rc != EOK) {
		usb_log_warning("Problem with getting Report descriptor: %s.\n",
		    str_error(rc));
		return rc;
	}
	
	rc = usb_hid_parse_report_descriptor(kbd_dev->parser, 
	    kbd_dev->report_desc, kbd_dev->report_desc_size);
	if (rc != EOK) {
		usb_log_warning("Problem parsing Report descriptor: %s.\n",
		    str_error(rc));
		return rc;
	}
	
	usb_hid_descriptor_print(kbd_dev->parser);
	
	/*
	 * TODO: if failed, try to parse the boot report descriptor.
	 */
	
	return EOK;
}

/*----------------------------------------------------------------------------*/
/* API functions                                                              */
/*----------------------------------------------------------------------------*/
/**
 * Creates a new USB/HID keyboard structure.
 *
 * The structure returned by this function is not initialized. Use 
 * usbhid_kbd_init() to initialize it prior to polling.
 *
 * @return New uninitialized structure for representing a USB/HID keyboard or
 *         NULL if not successful (memory error).
 */
usbhid_kbd_t *usbhid_kbd_new(void)
{
	usbhid_kbd_t *kbd_dev = 
	    (usbhid_kbd_t *)malloc(sizeof(usbhid_kbd_t));

	if (kbd_dev == NULL) {
		usb_log_fatal("No memory!\n");
		return NULL;
	}
	
	memset(kbd_dev, 0, sizeof(usbhid_kbd_t));
	
	kbd_dev->parser = (usb_hid_report_parser_t *)(malloc(sizeof(
	    usb_hid_report_parser_t)));
	if (kbd_dev->parser == NULL) {
		usb_log_fatal("No memory!\n");
		free(kbd_dev);
		return NULL;
	}
	
	kbd_dev->console_phone = -1;
	kbd_dev->initialized = USBHID_KBD_STATUS_UNINITIALIZED;
	
	return kbd_dev;
}

/*----------------------------------------------------------------------------*/
/**
 * Initialization of the USB/HID keyboard structure.
 *
 * This functions initializes required structures from the device's descriptors.
 *
 * During initialization, the keyboard is switched into boot protocol, the idle
 * rate is set to 0 (infinity), resulting in the keyboard only reporting event
 * when a key is pressed or released. Finally, the LED lights are turned on 
 * according to the default setup of lock keys.
 *
 * @note By default, the keyboards is initialized with Num Lock turned on and 
 *       other locks turned off.
 *
 * @param kbd_dev Keyboard device structure to be initialized.
 * @param dev DDF device structure of the keyboard.
 *
 * @retval EOK if successful.
 * @retval EINVAL if some parameter is not given.
 * @return Other value inherited from function usbhid_dev_init().
 */
int usbhid_kbd_init(usbhid_kbd_t *kbd_dev, usb_device_t *dev)
{
	int rc;
	
	usb_log_debug("Initializing HID/KBD structure...\n");
	
	if (kbd_dev == NULL) {
		usb_log_error("Failed to init keyboard structure: no structure"
		    " given.\n");
		return EINVAL;
	}
	
	if (dev == NULL) {
		usb_log_error("Failed to init keyboard structure: no USB device"
		    " given.\n");
		return EINVAL;
	}
	
	if (kbd_dev->initialized == USBHID_KBD_STATUS_INITIALIZED) {
		usb_log_warning("Keyboard structure already initialized.\n");
		return EINVAL;
	}
	
	//rc = usbhid_dev_init(kbd_dev->hid_dev, dev, &poll_endpoint_description);
	
	
//	if (rc != EOK) {
//		usb_log_error("Failed to initialize HID device structure: %s\n",
//		   str_error(rc));
//		return rc;
//	}
	
//	assert(kbd_dev->hid_dev->initialized == USBHID_KBD_STATUS_INITIALIZED);
	
	// save the size of the report (boot protocol report by default)
//	kbd_dev->key_count = BOOTP_REPORT_SIZE;
	
	/* The USB device should already be initialized, save it in structure */
	kbd_dev->usb_dev = dev;
	
	/* Initialize the report parser. */
	rc = usb_hid_parser_init(kbd_dev->parser);
	if (rc != EOK) {
		usb_log_error("Failed to initialize report parser.\n");
		return rc;
	}
	
	/* Get the report descriptor and parse it. */
	rc = usbhid_kbd_process_report_descriptor(kbd_dev);
	if (rc != EOK) {
		usb_log_warning("Could not process report descriptor.\n");
		return rc;
	}
	
	/*
	 * TODO: make more general
	 */
	usb_hid_report_path_t path;
	path.usage_page = USB_HIDUT_PAGE_KEYBOARD;
	kbd_dev->key_count = usb_hid_report_input_length(
	    kbd_dev->parser, &path);
	
	usb_log_debug("Size of the input report: %zu\n", kbd_dev->key_count);
	
	kbd_dev->keys = (uint8_t *)calloc(
	    kbd_dev->key_count, sizeof(uint8_t));
	
	if (kbd_dev->keys == NULL) {
		usb_log_fatal("No memory!\n");
		return ENOMEM;
	}
	
	kbd_dev->modifiers = 0;
	kbd_dev->mods = DEFAULT_ACTIVE_MODS;
	kbd_dev->lock_keys = 0;
	
	kbd_dev->repeat.key_new = 0;
	kbd_dev->repeat.key_repeated = 0;
	kbd_dev->repeat.delay_before = DEFAULT_DELAY_BEFORE_FIRST_REPEAT;
	kbd_dev->repeat.delay_between = DEFAULT_REPEAT_DELAY;
	
	kbd_dev->repeat_mtx = (fibril_mutex_t *)(
	    malloc(sizeof(fibril_mutex_t)));
	if (kbd_dev->repeat_mtx == NULL) {
		usb_log_fatal("No memory!\n");
		free(kbd_dev->keys);
		return ENOMEM;
	}
	
	fibril_mutex_initialize(kbd_dev->repeat_mtx);
	
	/*
	 * Set boot protocol.
	 * Set LEDs according to initial setup.
	 * Set Idle rate
	 */
	//assert(kbd_dev->hid_dev != NULL);
	//assert(kbd_dev->hid_dev->initialized);
	//usbhid_req_set_protocol(kbd_dev->hid_dev, USB_HID_PROTOCOL_BOOT);
	
	usbhid_kbd_set_led(kbd_dev);
	
	usbhid_req_set_idle(&kbd_dev->usb_dev->ctrl_pipe, 
	    kbd_dev->usb_dev->interface_no, IDLE_RATE);
	
	kbd_dev->initialized = USBHID_KBD_STATUS_INITIALIZED;
	usb_log_debug("HID/KBD device structure initialized.\n");
	
	return EOK;
}

/*----------------------------------------------------------------------------*/

bool usbhid_kbd_polling_callback(usb_device_t *dev, uint8_t *buffer,
     size_t buffer_size, void *arg)
{
	if (dev == NULL || buffer == NULL || arg == NULL) {
		// do not continue polling (???)
		return false;
	}
	
	usbhid_kbd_t *kbd_dev = (usbhid_kbd_t *)arg;
	
	// TODO: add return value from this function
	usbhid_kbd_process_data(kbd_dev, buffer, buffer_size);
	
	return true;
}

/*----------------------------------------------------------------------------*/

void usbhid_kbd_polling_ended_callback(usb_device_t *dev, bool reason, 
     void *arg)
{
	if (dev == NULL || arg == NULL) {
		return;
	}
	
	usbhid_kbd_t *kbd = (usbhid_kbd_t *)arg;
	
	usbhid_kbd_mark_unusable(kbd);
}

/*----------------------------------------------------------------------------*/

int usbhid_kbd_is_initialized(const usbhid_kbd_t *kbd_dev)
{
	return (kbd_dev->initialized == USBHID_KBD_STATUS_INITIALIZED);
}

/*----------------------------------------------------------------------------*/

int usbhid_kbd_is_ready_to_destroy(const usbhid_kbd_t *kbd_dev)
{
	return (kbd_dev->initialized == USBHID_KBD_STATUS_TO_DESTROY);
}

/*----------------------------------------------------------------------------*/
/**
 * Properly destroys the USB/HID keyboard structure.
 *
 * @param kbd_dev Pointer to the structure to be destroyed.
 */
void usbhid_kbd_free(usbhid_kbd_t **kbd_dev)
{
	if (kbd_dev == NULL || *kbd_dev == NULL) {
		return;
	}
	
	// hangup phone to the console
	async_hangup((*kbd_dev)->console_phone);
	
//	if ((*kbd_dev)->hid_dev != NULL) {
//		usbhid_dev_free(&(*kbd_dev)->hid_dev);
//		assert((*kbd_dev)->hid_dev == NULL);
//	}
	
	if ((*kbd_dev)->repeat_mtx != NULL) {
		/* TODO: replace by some check and wait */
		assert(!fibril_mutex_is_locked((*kbd_dev)->repeat_mtx));
		free((*kbd_dev)->repeat_mtx);
	}
	
	// destroy the parser
	if ((*kbd_dev)->parser != NULL) {
		usb_hid_free_report_parser((*kbd_dev)->parser);
	}
	
	/* TODO: what about the USB device structure?? */

	free(*kbd_dev);
	*kbd_dev = NULL;
}

/**
 * @}
 */
