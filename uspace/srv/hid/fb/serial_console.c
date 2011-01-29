/*
 * Copyright (c) 2006 Ondrej Palkovsky
 * Copyright (c) 2008 Martin Decky
 * Copyright (c) 2008 Pavel Rimsky
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
 * @defgroup serial Serial console
 * @brief Serial console services (putc, puts, clear screen, cursor goto,...)
 * @{
 */

/** @file
 */

#include <stdio.h>
#include <async.h>
#include <ipc/fb.h>
#include <bool.h>
#include <errno.h>
#include <io/color.h>
#include <io/style.h>
#include <str.h>
#include <inttypes.h>
#include <io/screenbuffer.h>

#include "main.h"
#include "serial_console.h"

#define MAX_CONTROL 20

static sysarg_t scr_width;
static sysarg_t scr_height;
static bool color = true;    /**< True if producing color output. */
static bool utf8 = false;    /**< True if producing UTF8 output. */
static putc_function_t putc_function;

static sysarg_t lastcol = 0;
static sysarg_t lastrow = 0;
static attrs_t cur_attr = {
	.t = at_style,
	.a.s.style = STYLE_NORMAL
};

/* Allow only 1 connection */
static int client_connected = 0;

enum sgr_color_index {
	CI_BLACK   = 0,
	CI_RED     = 1,
	CI_GREEN   = 2,
	CI_BROWN   = 3,
	CI_BLUE    = 4,
	CI_MAGENTA = 5,
	CI_CYAN    = 6,
	CI_WHITE   = 7
};

enum sgr_command {
	SGR_RESET       = 0,
	SGR_BOLD        = 1,
	SGR_UNDERLINE   = 4,
	SGR_BLINK       = 5,
	SGR_REVERSE     = 7,
	SGR_FGCOLOR     = 30,
	SGR_BGCOLOR     = 40
};

static int color_map[] = {
	[COLOR_BLACK]   = CI_BLACK,
	[COLOR_BLUE]    = CI_RED,
	[COLOR_GREEN]   = CI_GREEN,
	[COLOR_CYAN]    = CI_CYAN,
	[COLOR_RED]     = CI_RED,
	[COLOR_MAGENTA] = CI_MAGENTA,
	[COLOR_YELLOW]  = CI_BROWN,
	[COLOR_WHITE]   = CI_WHITE
};

void serial_puts(const char *str)
{
	while (*str)
		putc_function(*(str++));
}

static void serial_putchar(wchar_t ch)
{
	if (utf8 != true) {
		if (ch >= 0 && ch < 128)
			(*putc_function)((uint8_t) ch);
		else
			(*putc_function)('?');
		
		return;
	}
	
	size_t offs = 0;
	char buf[STR_BOUNDS(1)];
	if (chr_encode(ch, buf, &offs, STR_BOUNDS(1)) == EOK) {
		size_t i;
		for (i = 0; i < offs; i++)
			(*putc_function)(buf[i]);
	} else
		(*putc_function)('?');
}

void serial_goto(const sysarg_t col, const sysarg_t row)
{
	if ((col > scr_width) || (row > scr_height))
		return;
	
	char control[MAX_CONTROL];
	snprintf(control, MAX_CONTROL, "\033[%" PRIun ";%" PRIun "f",
	    row + 1, col + 1);
	serial_puts(control);
}

/** ECMA-48 Set Graphics Rendition. */
static void serial_sgr(const unsigned int mode)
{
	char control[MAX_CONTROL];
	snprintf(control, MAX_CONTROL, "\033[%um", mode);
	serial_puts(control);
}

static void serial_set_style(console_style_t style)
{
	switch (style) {
	case STYLE_EMPHASIS:
		serial_sgr(SGR_RESET);
		if (color) {
			serial_sgr(SGR_FGCOLOR + CI_RED);
			serial_sgr(SGR_BGCOLOR + CI_WHITE);
		}
		serial_sgr(SGR_BOLD);
		break;
	case STYLE_INVERTED:
		serial_sgr(SGR_RESET);
		if (color) {
			serial_sgr(SGR_FGCOLOR + CI_WHITE);
			serial_sgr(SGR_BGCOLOR + CI_BLACK);
		} else
			serial_sgr(SGR_REVERSE);
		break;
	case STYLE_SELECTED:
		serial_sgr(SGR_RESET);
		if (color) {
			serial_sgr(SGR_FGCOLOR + CI_WHITE);
			serial_sgr(SGR_BGCOLOR + CI_RED);
		} else
			serial_sgr(SGR_UNDERLINE);
		break;
	default:
		serial_sgr(SGR_RESET);
		if (color) {
			serial_sgr(SGR_FGCOLOR + CI_BLACK);
			serial_sgr(SGR_BGCOLOR + CI_WHITE);
		}
	}
}

static void serial_set_idx(uint8_t fgcolor, uint8_t bgcolor,
    uint8_t flags)
{
	serial_sgr(SGR_RESET);
	if (color) {
		serial_sgr(SGR_FGCOLOR + color_map[fgcolor & 7]);
		serial_sgr(SGR_BGCOLOR + color_map[bgcolor & 7]);
		if (flags & CATTR_BRIGHT)
			serial_sgr(SGR_BOLD);
	} else {
		if (fgcolor >= bgcolor)
			serial_sgr(SGR_REVERSE);
	}
}

static void serial_set_rgb(uint32_t fgcolor, uint32_t bgcolor)
{
	serial_sgr(SGR_RESET);
	
	if (fgcolor >= bgcolor)
		serial_sgr(SGR_REVERSE);
}

static void serial_set_attrs(attrs_t *a)
{
	switch (a->t) {
	case at_style:
		serial_set_style(a->a.s.style);
		break;
	case at_rgb:
		serial_set_rgb(a->a.r.fg_color, a->a.r.bg_color);
		break;
	case at_idx:
		serial_set_idx(a->a.i.fg_color, a->a.i.bg_color,
		    a->a.i.flags);
		break;
	}
}

void serial_clrscr(void)
{
	/* Initialize graphic rendition attributes. */
	serial_sgr(SGR_RESET);
	if (color) {
		serial_sgr(SGR_FGCOLOR + CI_BLACK);
		serial_sgr(SGR_BGCOLOR + CI_WHITE);
	}
	
	serial_puts("\033[2J");
	
	serial_set_attrs(&cur_attr);
}

void serial_scroll(ssize_t i)
{
	if (i > 0) {
		serial_goto(0, scr_height - 1);
		while (i--)
			serial_puts("\033D");
	} else if (i < 0) {
		serial_goto(0, 0);
		while (i++)
			serial_puts("\033M");
	}
}

/** Set scrolling region. */
void serial_set_scroll_region(sysarg_t last_row)
{
	char control[MAX_CONTROL];
	snprintf(control, MAX_CONTROL, "\033[0;%" PRIun "r", last_row);
	serial_puts(control);
}

void serial_cursor_disable(void)
{
	serial_puts("\033[?25l");
}

void serial_cursor_enable(void)
{
	serial_puts("\033[?25h");
}

void serial_console_init(putc_function_t putc_fn, sysarg_t w, sysarg_t h)
{
	scr_width = w;
	scr_height = h;
	putc_function = putc_fn;
}

/** Draw text data to viewport.
 *
 * @param vport  Viewport id
 * @param data   Text data.
 * @param x0     Leftmost column of the area.
 * @param y0     Topmost row of the area.
 * @param width  Number of rows.
 * @param height Number of columns.
 *
 */
static void draw_text_data(keyfield_t *data, sysarg_t x0, sysarg_t y0,
    sysarg_t width, sysarg_t height)
{
	attrs_t *a0 = &data[0].attrs;
	serial_set_attrs(a0);
	
	sysarg_t y;
	for (y = 0; y < height; y++) {
		serial_goto(x0, y0 + y);
		
		sysarg_t x;
		for (x = 0; x < width; x++) {
			attrs_t *attr = &data[y * width + x].attrs;
			
			if (!attrs_same(*a0, *attr)) {
				serial_set_attrs(attr);
				a0 = attr;
			}
			
			serial_putchar(data[y * width + x].character);
		}
	}
}

/**
 * Main function of the thread serving client connections.
 */
void serial_client_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	keyfield_t *interbuf = NULL;
	size_t intersize = 0;
	
	if (client_connected) {
		ipc_answer_0(iid, ELIMIT);
		return;
	}
	
	client_connected = 1;
	ipc_answer_0(iid, EOK);
	
	/* Clear the terminal, set scrolling region
	   to 0 - height rows. */
	serial_clrscr();
	serial_goto(0, 0);
	serial_set_scroll_region(scr_height);
	
	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);
		
		wchar_t c;
		sysarg_t col;
		sysarg_t row;
		sysarg_t w;
		sysarg_t h;
		ssize_t rows;
		
		int retval;
		
		switch (IPC_GET_IMETHOD(call)) {
		case IPC_M_PHONE_HUNGUP:
			client_connected = 0;
			ipc_answer_0(callid, EOK);
			
			/* Exit thread */
			return;
		case IPC_M_SHARE_OUT:
			/* We accept one area for data interchange */
			intersize = IPC_GET_ARG2(call);
			if (intersize >= scr_width * scr_height *
			    sizeof(*interbuf)) {
				receive_comm_area(callid, &call,
				    (void *) &interbuf);
				continue;
			}
			
			retval = EINVAL;
			break;
		case FB_DRAW_TEXT_DATA:
			col = IPC_GET_ARG1(call);
			row = IPC_GET_ARG2(call);
			w = IPC_GET_ARG3(call);
			h = IPC_GET_ARG4(call);
			
			if (!interbuf) {
				retval = EINVAL;
				break;
			}
			
			if ((col + w > scr_width) || (row + h > scr_height)) {
				retval = EINVAL;
				break;
			}
			
			draw_text_data(interbuf, col, row, w, h);
			lastcol = col + w;
			lastrow = row + h - 1;
			retval = 0;
			break;
		case FB_PUTCHAR:
			c = IPC_GET_ARG1(call);
			col = IPC_GET_ARG2(call);
			row = IPC_GET_ARG3(call);
			
			if ((lastcol != col) || (lastrow != row))
				serial_goto(col, row);
			
			lastcol = col + 1;
			lastrow = row;
			serial_putchar(c);
			retval = 0;
			break;
		case FB_CURSOR_GOTO:
			col = IPC_GET_ARG1(call);
			row = IPC_GET_ARG2(call);
			serial_goto(col, row);
			lastcol = col;
			lastrow = row;
			retval = 0;
			break;
		case FB_GET_CSIZE:
			ipc_answer_2(callid, EOK, scr_width, scr_height);
			continue;
		case FB_GET_COLOR_CAP:
			ipc_answer_1(callid, EOK, color ? FB_CCAP_INDEXED :
			    FB_CCAP_STYLE);
			continue;
		case FB_CLEAR:
			serial_clrscr();
			retval = 0;
			break;
		case FB_SET_STYLE:
			cur_attr.t = at_style;
			cur_attr.a.s.style = IPC_GET_ARG1(call);
			serial_set_attrs(&cur_attr);
			retval = 0;
			break;
		case FB_SET_COLOR:
			cur_attr.t = at_idx;
			cur_attr.a.i.fg_color = IPC_GET_ARG1(call);
			cur_attr.a.i.bg_color = IPC_GET_ARG2(call);
			cur_attr.a.i.flags = IPC_GET_ARG3(call);
			serial_set_attrs(&cur_attr);
			retval = 0;
			break;
		case FB_SET_RGB_COLOR:
			cur_attr.t = at_rgb;
			cur_attr.a.r.fg_color = IPC_GET_ARG1(call);
			cur_attr.a.r.bg_color = IPC_GET_ARG2(call);
			serial_set_attrs(&cur_attr);
			retval = 0;
			break;
		case FB_SCROLL:
			rows = IPC_GET_ARG1(call);
			
			if (rows >= 0) {
				if ((sysarg_t) rows > scr_height) {
					retval = EINVAL;
					break;
				}
			} else {
				if ((sysarg_t) (-rows) > scr_height) {
					retval = EINVAL;
					break;
				}
			}
			
			serial_scroll(rows);
			serial_goto(lastcol, lastrow);
			retval = 0;
			break;
		case FB_CURSOR_VISIBILITY:
			if(IPC_GET_ARG1(call))
				serial_cursor_enable();
			else
				serial_cursor_disable();
			retval = 0;
			break;
		case FB_SCREEN_YIELD:
			serial_sgr(SGR_RESET);
			serial_puts("\033[2J");
			serial_goto(0, 0);
			serial_cursor_enable();
			retval = 0;
			break;
		case FB_SCREEN_RECLAIM:
			serial_clrscr();
			retval = 0;
			break;
		default:
			retval = ENOENT;
		}
		ipc_answer_0(callid, retval);
	}
}

/**
 * @}
 */
