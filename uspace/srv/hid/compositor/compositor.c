/*
 * Copyright (c) 2012 Petr Koupy
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

/** @addtogroup compositor
 * @{
 */
/** @file
 */

#include <sys/types.h>
#include <stdbool.h>
#include <errno.h>
#include <str_error.h>
#include <byteorder.h>
#include <stdio.h>
#include <libc.h>

#include <align.h>
#include <as.h>
#include <malloc.h>

#include <atomic.h>
#include <fibril_synch.h>
#include <adt/prodcons.h>
#include <adt/list.h>
#include <io/input.h>
#include <ipc/graph.h>
#include <ipc/window.h>

#include <async.h>
#include <loc.h>
#include <devman.h>

#include <event.h>
#include <device/graph_dev.h>
#include <io/keycode.h>
#include <io/mode.h>
#include <io/visualizer.h>
#include <io/window.h>

#include <transform.h>
#include <rectangle.h>
#include <surface.h>
#include <cursor.h>
#include <source.h>
#include <drawctx.h>
#include <codec/tga.h>

#include "images.h"
#include "compositor.h"

#define NAME       "compositor"
#define NAMESPACE  "comp"

/* Until there is blitter support and some further optimizations, window
 * animations are too slow to be practically usable. */
#ifndef ANIMATE_WINDOW_TRANSFORMS
#define ANIMATE_WINDOW_TRANSFORMS 0
#endif

static char *server_name;
static sysarg_t coord_origin;
static pixel_t bg_color;

typedef struct {
	link_t link;
	service_id_t in_dsid;
	service_id_t out_dsid;
	prodcons_t queue;
	transform_t transform;
	double dx;
	double dy;
	double fx;
	double fy;
	double angle;
	uint8_t opacity;
	surface_t *surface;
} window_t;

static service_id_t winreg_id;
static sysarg_t window_id = 0;
static FIBRIL_MUTEX_INITIALIZE(window_list_mtx);
static LIST_INITIALIZE(window_list);
static double scale_back_x;
static double scale_back_y;

typedef struct {
	link_t link;
	sysarg_t id;
	uint8_t state;
	desktop_point_t pos;
	sysarg_t btn_num;
	desktop_point_t btn_pos;
	desktop_vector_t accum;
	sysarg_t grab_flags;
	bool pressed;
	cursor_t cursor;
	window_t ghost;
	desktop_vector_t accum_ghost;
} pointer_t;

static sysarg_t pointer_id = 0;
static FIBRIL_MUTEX_INITIALIZE(pointer_list_mtx);
static LIST_INITIALIZE(pointer_list);

typedef struct {
	link_t link;
	service_id_t dsid;
	vslmode_t mode;
	async_sess_t *sess;
	desktop_point_t pos;
	surface_t *surface;
} viewport_t;

static FIBRIL_MUTEX_INITIALIZE(viewport_list_mtx);
static LIST_INITIALIZE(viewport_list);

/** Input server proxy */
static input_t *input;

static int comp_key_press(input_t *, kbd_event_type_t, keycode_t, keymod_t, wchar_t);
static int comp_mouse_move(input_t *, int, int);
static int comp_abs_move(input_t *, unsigned, unsigned, unsigned, unsigned);
static int comp_mouse_button(input_t *, int, int);

static input_ev_ops_t input_ev_ops = {
	.key = comp_key_press,
	.move = comp_mouse_move,
	.abs_move = comp_abs_move,
	.button = comp_mouse_button
};

static void input_disconnect(void);


static pointer_t *input_pointer(input_t *input)
{
	return input->user;
}

static pointer_t *pointer_create()
{
	pointer_t *p = (pointer_t *) malloc(sizeof(pointer_t));
	if (!p) {
		return NULL;
	}

	link_initialize(&p->link);
	p->pos.x = coord_origin;
	p->pos.y = coord_origin;
	p->btn_num = 1;
	p->btn_pos = p->pos;
	p->accum.x = 0;
	p->accum.y = 0;
	p->grab_flags = GF_EMPTY;
	p->pressed = false;
	p->state = 0;
	cursor_init(&p->cursor, CURSOR_DECODER_EMBEDDED, NULL);

	/* Ghost window for transformation animation. */
	transform_identity(&p->ghost.transform);
	transform_translate(&p->ghost.transform, coord_origin, coord_origin);
	p->ghost.dx = coord_origin;
	p->ghost.dy = coord_origin;
	p->ghost.fx = 1;
	p->ghost.fy = 1;
	p->ghost.angle = 0;
	p->ghost.opacity = 255;
	p->ghost.surface = NULL;
	p->accum_ghost.x = 0;
	p->accum_ghost.y = 0;

	return p;
}

static void pointer_destroy(pointer_t *p)
{
	if (p) {
		cursor_release(&p->cursor);
		free(p);
	}
}

static window_t *window_create(sysarg_t x_offset, sysarg_t y_offset)
{
	window_t *win = (window_t *) malloc(sizeof(window_t));
	if (!win) {
		return NULL;
	}

	link_initialize(&win->link);
	prodcons_initialize(&win->queue);
	transform_identity(&win->transform);
	transform_translate(&win->transform, 
	    coord_origin + x_offset, coord_origin + y_offset);
	win->dx = coord_origin + x_offset;
	win->dy = coord_origin + y_offset;
	win->fx = 1;
	win->fy = 1;
	win->angle = 0;
	win->opacity = 255;
	win->surface = NULL;

	return win;
}

static void window_destroy(window_t *win)
{
	if (win) {
		if (win->surface) {
			surface_destroy(win->surface);
		}
		free(win);
	}
}

static bool comp_coord_to_client(sysarg_t x_in, sysarg_t y_in, transform_t win_trans,
    sysarg_t x_lim, sysarg_t y_lim, sysarg_t *x_out, sysarg_t *y_out)
{
	double x = x_in;
	double y = y_in;
	transform_invert(&win_trans);
	transform_apply_affine(&win_trans, &x, &y);

	/* Since client coordinate origin is (0, 0), it is necessary to check
	 * coordinates to avoid underflow. Moreover, it is convenient to also
	 * check against provided upper limits to determine whether the converted
	 * coordinates are within the client window.  */
	if (x < 0 || y < 0) {
		return false;
	} else {
		(*x_out) = (sysarg_t) (x + 0.5);
		(*y_out) = (sysarg_t) (y + 0.5);

		if ((*x_out) >= x_lim || (*y_out) >= y_lim) {
			return false;
		} else {
			return true;
		}
	}
}

static void comp_coord_from_client(double x_in, double y_in, transform_t win_trans,
    sysarg_t *x_out, sysarg_t *y_out)
{
	double x = x_in;
	double y = y_in;
	transform_apply_affine(&win_trans, &x, &y);
	
	/* It is assumed that compositor coordinate origin is chosen in such way,
	 * that underflow/overflow here would be unlikely. */
	(*x_out) = (sysarg_t) (x + 0.5);
	(*y_out) = (sysarg_t) (y + 0.5);
}

static void comp_coord_bounding_rect(double x_in, double y_in,
    double w_in, double h_in, transform_t win_trans,
    sysarg_t *x_out, sysarg_t *y_out, sysarg_t *w_out, sysarg_t *h_out)
{
	if (w_in > 0 && h_in > 0) {
		sysarg_t x[4];
		sysarg_t y[4];
		comp_coord_from_client(x_in, y_in, win_trans, &x[0], &y[0]);
		comp_coord_from_client(x_in + w_in - 1, y_in, win_trans, &x[1], &y[1]);
		comp_coord_from_client(x_in + w_in - 1, y_in + h_in - 1, win_trans, &x[2], &y[2]);
		comp_coord_from_client(x_in, y_in + h_in - 1, win_trans, &x[3], &y[3]);
		(*x_out) = x[0];
		(*y_out) = y[0];
		(*w_out) = x[0];
		(*h_out) = y[0];
		for (int i = 1; i < 4; ++i) {
			(*x_out) = (x[i] < (*x_out)) ? x[i] : (*x_out);
			(*y_out) = (y[i] < (*y_out)) ? y[i] : (*y_out);
			(*w_out) = (x[i] > (*w_out)) ? x[i] : (*w_out);
			(*h_out) = (y[i] > (*h_out)) ? y[i] : (*h_out);
		}
		(*w_out) = (*w_out) - (*x_out) + 1;
		(*h_out) = (*h_out) - (*y_out) + 1;
	} else {
		(*x_out) = 0;
		(*y_out) = 0;
		(*w_out) = 0;
		(*h_out) = 0;
	}
}

static void comp_damage(sysarg_t x_dmg_glob, sysarg_t y_dmg_glob,
    sysarg_t w_dmg_glob, sysarg_t h_dmg_glob)
{
	fibril_mutex_lock(&viewport_list_mtx);
	fibril_mutex_lock(&window_list_mtx);
	fibril_mutex_lock(&pointer_list_mtx);

	list_foreach(viewport_list, link) {

		/* Determine what part of the viewport must be updated. */
		viewport_t *vp = list_get_instance(link, viewport_t, link);
		sysarg_t x_dmg_vp, y_dmg_vp, w_dmg_vp, h_dmg_vp;
		surface_get_resolution(vp->surface, &w_dmg_vp, &h_dmg_vp);
		bool isec_vp = rectangle_intersect(
		    x_dmg_glob, y_dmg_glob, w_dmg_glob, h_dmg_glob,
		    vp->pos.x, vp->pos.y, w_dmg_vp, h_dmg_vp,
		    &x_dmg_vp, &y_dmg_vp, &w_dmg_vp, &h_dmg_vp);

		if (isec_vp) {

			/* Paint background color. */
			for (sysarg_t y = y_dmg_vp - vp->pos.y; y <  y_dmg_vp - vp->pos.y + h_dmg_vp; ++y) {
				pixel_t *dst = pixelmap_pixel_at(
				    surface_pixmap_access(vp->surface), x_dmg_vp - vp->pos.x, y);
				sysarg_t count = w_dmg_vp;
				while (count-- != 0) {
					*dst++ = bg_color;
				}
			}
			surface_add_damaged_region(vp->surface,
			    x_dmg_vp - vp->pos.x, y_dmg_vp - vp->pos.y, w_dmg_vp, h_dmg_vp);

			transform_t transform;
			source_t source;
			drawctx_t context;

			source_init(&source);
			source_set_filter(&source, filter_nearest);
			drawctx_init(&context, vp->surface);
			drawctx_set_compose(&context, compose_over);
			drawctx_set_source(&context, &source);

			/* For each window. */
			for (link_t *link = window_list.head.prev;
			    link != &window_list.head; link = link->prev) {

				/* Determine what part of the window intersects with the
				 * updated area of the current viewport. */
				window_t *win = list_get_instance(link, window_t, link);
				if (!win->surface) {
					continue;
				}
				sysarg_t x_dmg_win, y_dmg_win, w_dmg_win, h_dmg_win;
				surface_get_resolution(win->surface, &w_dmg_win, &h_dmg_win);
				comp_coord_bounding_rect(0, 0, w_dmg_win, h_dmg_win, win->transform,
				    &x_dmg_win, &y_dmg_win, &w_dmg_win, &h_dmg_win);
				bool isec_win = rectangle_intersect(
				    x_dmg_vp, y_dmg_vp, w_dmg_vp, h_dmg_vp,
				    x_dmg_win, y_dmg_win, w_dmg_win, h_dmg_win,
				    &x_dmg_win, &y_dmg_win, &w_dmg_win, &h_dmg_win);

				if (isec_win) {
					/* Prepare conversion from global coordinates to viewport
					 * coordinates. */
					transform = win->transform;
					double_point_t pos;
					pos.x = vp->pos.x;
					pos.y = vp->pos.y;
					transform_translate(&transform, -pos.x, -pos.y);

					source_set_transform(&source, transform);				
					source_set_texture(&source, win->surface, false);
					source_set_alpha(&source, PIXEL(win->opacity, 0, 0, 0));

					drawctx_transfer(&context,
					    x_dmg_win - vp->pos.x, y_dmg_win - vp->pos.y, w_dmg_win, h_dmg_win);
				}
			}

			list_foreach(pointer_list, link) {

				pointer_t *ptr = list_get_instance(link, pointer_t, link);
				if (ptr->ghost.surface) {

					sysarg_t x_bnd_ghost, y_bnd_ghost, w_bnd_ghost, h_bnd_ghost;
					sysarg_t x_dmg_ghost, y_dmg_ghost, w_dmg_ghost, h_dmg_ghost;
					surface_get_resolution(ptr->ghost.surface, &w_bnd_ghost, &h_bnd_ghost);
					comp_coord_bounding_rect(0, 0, w_bnd_ghost, h_bnd_ghost, ptr->ghost.transform,
					    &x_bnd_ghost, &y_bnd_ghost, &w_bnd_ghost, &h_bnd_ghost);
					bool isec_ghost = rectangle_intersect(
					    x_dmg_vp, y_dmg_vp, w_dmg_vp, h_dmg_vp,
					    x_bnd_ghost, y_bnd_ghost, w_bnd_ghost, h_bnd_ghost,
					    &x_dmg_ghost, &y_dmg_ghost, &w_dmg_ghost, &h_dmg_ghost);

					if (isec_ghost) {
						/* FIXME: Ghost is currently drawn based on the bounding
						 * rectangle of the window, which is sufficient as long
						 * as the windows can be rotated only by 90 degrees. 
						 * For ghost to be compatible with arbitrary-angle
						 * rotation, it should be drawn as four lines adjusted
						 * by the transformation matrix. That would however
						 * require to equip libdraw with line drawing functionality. */

						transform_t transform = ptr->ghost.transform;
						double_point_t pos;
						pos.x = vp->pos.x;
						pos.y = vp->pos.y;
						transform_translate(&transform, -pos.x, -pos.y);

						pixel_t ghost_color;

						if (y_bnd_ghost == y_dmg_ghost) {
							for (sysarg_t x = x_dmg_ghost - vp->pos.x;
								    x < x_dmg_ghost - vp->pos.x + w_dmg_ghost; ++x) {
								ghost_color = surface_get_pixel(vp->surface,
								    x, y_dmg_ghost - vp->pos.y);
								surface_put_pixel(vp->surface,
								    x, y_dmg_ghost - vp->pos.y, INVERT(ghost_color));
							}
						}

						if (y_bnd_ghost + h_bnd_ghost == y_dmg_ghost + h_dmg_ghost) {
							for (sysarg_t x = x_dmg_ghost - vp->pos.x;
								    x < x_dmg_ghost - vp->pos.x + w_dmg_ghost; ++x) {
								ghost_color = surface_get_pixel(vp->surface,
								    x, y_dmg_ghost - vp->pos.y + h_dmg_ghost - 1);
								surface_put_pixel(vp->surface,
								    x, y_dmg_ghost - vp->pos.y + h_dmg_ghost - 1, INVERT(ghost_color));
							}
						}

						if (x_bnd_ghost == x_dmg_ghost) {
							for (sysarg_t y = y_dmg_ghost - vp->pos.y;
								    y < y_dmg_ghost - vp->pos.y + h_dmg_ghost; ++y) {
								ghost_color = surface_get_pixel(vp->surface,
								    x_dmg_ghost - vp->pos.x, y);
								surface_put_pixel(vp->surface,
								    x_dmg_ghost - vp->pos.x, y, INVERT(ghost_color));
							}
						}

						if (x_bnd_ghost + w_bnd_ghost == x_dmg_ghost + w_dmg_ghost) {
							for (sysarg_t y = y_dmg_ghost - vp->pos.y;
								    y < y_dmg_ghost - vp->pos.y + h_dmg_ghost; ++y) {
								ghost_color = surface_get_pixel(vp->surface,
								    x_dmg_ghost - vp->pos.x + w_dmg_ghost - 1, y);
								surface_put_pixel(vp->surface,
								    x_dmg_ghost - vp->pos.x + w_dmg_ghost - 1, y, INVERT(ghost_color));
							}
						}
					}

				}
			}

			list_foreach(pointer_list, link) {

				/* Determine what part of the pointer intersects with the
				 * updated area of the current viewport. */
				pointer_t *ptr = list_get_instance(link, pointer_t, link);
				sysarg_t x_dmg_ptr, y_dmg_ptr, w_dmg_ptr, h_dmg_ptr;
				surface_t *sf_ptr = ptr->cursor.states[ptr->state];
				surface_get_resolution(sf_ptr, &w_dmg_ptr, &h_dmg_ptr);
				bool isec_ptr = rectangle_intersect(
				    x_dmg_vp, y_dmg_vp, w_dmg_vp, h_dmg_vp,
				    ptr->pos.x, ptr->pos.y, w_dmg_ptr, h_dmg_ptr,
				    &x_dmg_ptr, &y_dmg_ptr, &w_dmg_ptr, &h_dmg_ptr);

				if (isec_ptr) {
					/* Pointer is currently painted directly by copying pixels.
					 * However, it is possible to draw the pointer similarly
					 * as window by using drawctx_transfer. It would allow
					 * more sophisticated control over drawing, but would also
					 * cost more regarding the performance. */

					sysarg_t x_vp = x_dmg_ptr - vp->pos.x;
					sysarg_t y_vp = y_dmg_ptr - vp->pos.y;
					sysarg_t x_ptr = x_dmg_ptr - ptr->pos.x;
					sysarg_t y_ptr = y_dmg_ptr - ptr->pos.y;

					for (sysarg_t y = 0; y < h_dmg_ptr; ++y) {
						pixel_t *src = pixelmap_pixel_at(
						    surface_pixmap_access(sf_ptr), x_ptr, y_ptr + y);
						pixel_t *dst = pixelmap_pixel_at(
						    surface_pixmap_access(vp->surface), x_vp, y_vp + y);
						sysarg_t count = w_dmg_ptr;
						while (count-- != 0) {
							*dst = (*src & 0xff000000) ? *src : *dst;
							++dst; ++src;
						}
					}
					surface_add_damaged_region(vp->surface, x_vp, y_vp, w_dmg_ptr, h_dmg_ptr);
				}

			}
		}
	}

	fibril_mutex_unlock(&pointer_list_mtx);
	fibril_mutex_unlock(&window_list_mtx);

	/* Notify visualizers about updated regions. */
	list_foreach(viewport_list, link) {
		viewport_t *vp = list_get_instance(link, viewport_t, link);
		sysarg_t x_dmg_vp, y_dmg_vp, w_dmg_vp, h_dmg_vp;
		surface_get_damaged_region(vp->surface, &x_dmg_vp, &y_dmg_vp, &w_dmg_vp, &h_dmg_vp);
		surface_reset_damaged_region(vp->surface);
		visualizer_update_damaged_region(
		    vp->sess, x_dmg_vp, y_dmg_vp, w_dmg_vp, h_dmg_vp, 0, 0);
	}

	fibril_mutex_unlock(&viewport_list_mtx);
}

static void comp_window_get_event(window_t *win, ipc_callid_t iid, ipc_call_t *icall)
{
	window_event_t *event = (window_event_t *) prodcons_consume(&win->queue);

	ipc_callid_t callid;
	size_t len;

	if (!async_data_read_receive(&callid, &len)) {
		async_answer_0(iid, EINVAL);
		free(event);
		return;
	}
	int rc = async_data_read_finalize(callid, event, len);
	if (rc != EOK) {
		async_answer_0(iid, ENOMEM);
		free(event);
		return;
	}
	async_answer_0(iid, EOK);
	
	free(event);
}

static void comp_window_damage(window_t *win, ipc_callid_t iid, ipc_call_t *icall)
{
	double x = IPC_GET_ARG1(*icall);
	double y = IPC_GET_ARG2(*icall);
	double width = IPC_GET_ARG3(*icall);
	double height = IPC_GET_ARG4(*icall);

	if (width == 0 || height == 0) {
		comp_damage(0, 0, UINT32_MAX, UINT32_MAX);
	} else {
		fibril_mutex_lock(&window_list_mtx);
		sysarg_t x_dmg_glob, y_dmg_glob, w_dmg_glob, h_dmg_glob;
		comp_coord_bounding_rect(x - 1, y - 1, width + 2, height + 2,
		    win->transform, &x_dmg_glob, &y_dmg_glob, &w_dmg_glob, &h_dmg_glob);
		fibril_mutex_unlock(&window_list_mtx);
		comp_damage(x_dmg_glob, y_dmg_glob, w_dmg_glob, h_dmg_glob);
	}

	async_answer_0(iid, EOK);
}

static void comp_window_grab(window_t *win, ipc_callid_t iid, ipc_call_t *icall)
{
	sysarg_t pos_id = IPC_GET_ARG1(*icall);
	sysarg_t grab_flags = IPC_GET_ARG2(*icall);

	fibril_mutex_lock(&pointer_list_mtx);
	list_foreach(pointer_list, link) {
		pointer_t *pointer = list_get_instance(link, pointer_t, link);
		if (pointer->id == pos_id) {
			pointer->grab_flags = pointer->pressed ? grab_flags : GF_EMPTY;
			// TODO change pointer->state according to grab_flags
			break;
		}
	}
	fibril_mutex_unlock(&pointer_list_mtx);

	if ((grab_flags & GF_RESIZE_X) || (grab_flags & GF_RESIZE_Y)) {
		scale_back_x = 1;
		scale_back_y = 1;
	}

	async_answer_0(iid, EOK);
}

static void comp_window_resize(window_t *win, ipc_callid_t iid, ipc_call_t *icall)
{
	int rc;

	ipc_callid_t callid;
	size_t size;
	unsigned int flags;

	/* Start sharing resized window with client. */
	if (!async_share_out_receive(&callid, &size, &flags)) {
		async_answer_0(iid, EINVAL);
		return;
	}
	void *new_cell_storage;
	rc = async_share_out_finalize(callid, &new_cell_storage);
	if ((rc != EOK) || (new_cell_storage == AS_MAP_FAILED)) {
		async_answer_0(iid, ENOMEM);
		return;
	}

	/* Create new surface for the resized window. */
	surface_t *new_surface = surface_create(
	    IPC_GET_ARG1(*icall), IPC_GET_ARG2(*icall),
	    new_cell_storage, SURFACE_FLAG_SHARED);
	if (!new_surface) {
		as_area_destroy(new_cell_storage);
		async_answer_0(iid, ENOMEM);
		return;
	}

	/* Switch new surface with old surface and calculate damage. */
	fibril_mutex_lock(&window_list_mtx);

	sysarg_t old_width = 0;
	sysarg_t old_height = 0;
	if (win->surface) {
		surface_get_resolution(win->surface, &old_width, &old_height);
		surface_destroy(win->surface);
	}

	win->surface = new_surface;

	sysarg_t new_width = 0;
	sysarg_t new_height = 0;
	surface_get_resolution(win->surface, &new_width, &new_height);

	sysarg_t x, y;
	sysarg_t width = old_width > new_width ? old_width : new_width;
	sysarg_t height = old_height > new_height ? old_height : new_height;
	comp_coord_bounding_rect(0, 0, width, height, win->transform, &x, &y, &width, &height);

	fibril_mutex_unlock(&window_list_mtx);

	comp_damage(x, y, width, height);

	async_answer_0(iid, EOK);
}

static void comp_post_event_win(window_event_t *event, window_t *target)
{
	fibril_mutex_lock(&window_list_mtx);
	window_t *window = NULL;
	list_foreach(window_list, link) {
		window = list_get_instance(link, window_t, link);
		if (window == target) {
			prodcons_produce(&window->queue, &event->link);
		}
	}
	if (!window) {
		free(event);
	}
	fibril_mutex_unlock(&window_list_mtx);
}

static void comp_post_event_top(window_event_t *event)
{
	fibril_mutex_lock(&window_list_mtx);
	window_t *win = (window_t *) list_first(&window_list);
	if (win) {
		prodcons_produce(&win->queue, &event->link);
	} else {
		free(event);
	}
	fibril_mutex_unlock(&window_list_mtx);
}

static void comp_window_close(window_t *win, ipc_callid_t iid, ipc_call_t *icall)
{
	/* Stop managing the window. */
	fibril_mutex_lock(&window_list_mtx);
	list_remove(&win->link);
	window_t *win_focus = (window_t *) list_first(&window_list);
	window_event_t *event_focus = (window_event_t *) malloc(sizeof(window_event_t));
	if (event_focus) {
		link_initialize(&event_focus->link);
		event_focus->type = ET_WINDOW_FOCUS;
	}
	fibril_mutex_unlock(&window_list_mtx);

	if (event_focus && win_focus) {
		comp_post_event_win(event_focus, win_focus);
	}

	/* Calculate damage. */
	sysarg_t x = 0;
	sysarg_t y = 0;
	sysarg_t width = 0;
	sysarg_t height = 0;
	if (win->surface) {
		surface_get_resolution(win->surface, &width, &height);
		comp_coord_bounding_rect(
		    0, 0, width, height, win->transform, &x, &y, &width, &height);
	}

	/* Release window resources. */
	loc_service_unregister(win->in_dsid);
	loc_service_unregister(win->out_dsid);
	while (!list_empty(&win->queue.list)) {
		list_remove(list_first(&win->queue.list));
	}
	window_destroy(win);

	comp_damage(x, y, width, height);

	async_answer_0(iid, EOK);
}

static void comp_window_close_request(window_t *win, ipc_callid_t iid, ipc_call_t *icall)
{
    window_event_t *event = (window_event_t *) malloc(sizeof(window_event_t));
	if (event == NULL) {
		async_answer_0(iid, ENOMEM);
		return;
	}

	link_initialize(&event->link);
	event->type = ET_WINDOW_CLOSE;

	prodcons_produce(&win->queue, &event->link);
	async_answer_0(iid, EOK);
}

static void client_connection(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	ipc_call_t call;
	ipc_callid_t callid;
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*icall);

	/* Allocate resources for new window and register it to the location service. */
	if (service_id == winreg_id) {
		async_answer_0(iid, EOK);

		callid = async_get_call(&call);
		if (IPC_GET_IMETHOD(call) == WINDOW_REGISTER) {
			fibril_mutex_lock(&window_list_mtx);

			window_t *win = window_create(IPC_GET_ARG1(call), IPC_GET_ARG2(call));
			if (!win) {
				async_answer_2(callid, ENOMEM, 0, 0);
				return;
			}

			char name_in[LOC_NAME_MAXLEN + 1];
			snprintf(name_in, LOC_NAME_MAXLEN, "%s%s/win%zuin", NAMESPACE,
			    server_name, window_id);

			char name_out[LOC_NAME_MAXLEN + 1];
			snprintf(name_out, LOC_NAME_MAXLEN, "%s%s/win%zuout", NAMESPACE,
			    server_name, window_id);

			++window_id;

			if (loc_service_register(name_in, &win->in_dsid) != EOK) {
				window_destroy(win);
				async_answer_2(callid, EINVAL, 0, 0);
				return;
			}

			if (loc_service_register(name_out, &win->out_dsid) != EOK) {
				loc_service_unregister(win->in_dsid);
				window_destroy(win);
				async_answer_2(callid, EINVAL, 0, 0);
				return;
			}

			window_t *win_unfocus = (window_t *) list_first(&window_list);
			list_prepend(&win->link, &window_list);

			window_event_t *event_unfocus = (window_event_t *) malloc(sizeof(window_event_t));
			if (event_unfocus) {
				link_initialize(&event_unfocus->link);
				event_unfocus->type = ET_WINDOW_UNFOCUS;
			}
			
			async_answer_2(callid, EOK, win->in_dsid, win->out_dsid);
			fibril_mutex_unlock(&window_list_mtx);
			
			if (event_unfocus && win_unfocus) {
				comp_post_event_win(event_unfocus, win_unfocus);
			}

			return;
		} else {
			async_answer_0(callid, EINVAL);
			return;
		}
	}

	/* Match the client with pre-allocated window. */
	window_t *win = NULL;
	fibril_mutex_lock(&window_list_mtx);
	list_foreach(window_list, link) {
		window_t *cur = list_get_instance(link, window_t, link);
		if (cur->in_dsid == service_id || cur->out_dsid == service_id) {
			win = cur;
			break;
		}
	}
	fibril_mutex_unlock(&window_list_mtx);

	if (win) {
		async_answer_0(iid, EOK);
	} else {
		async_answer_0(iid, EINVAL);
		return;
	}

	/* Each client establishes two separate connections. */
	if (win->in_dsid == service_id) {
		while (true) {
			callid = async_get_call(&call);

			if (!IPC_GET_IMETHOD(call)) {
				async_answer_0(callid, EINVAL);
				return;
			}

			switch (IPC_GET_IMETHOD(call)) {
			case WINDOW_GET_EVENT:
				comp_window_get_event(win, callid, &call);
				break;
			default:
				async_answer_0(callid, EINVAL);
			}
		}
	} else if (win->out_dsid == service_id) {
		while (true) {
			callid = async_get_call(&call);

			if (!IPC_GET_IMETHOD(call)) {
				async_answer_0(callid, EINVAL);
				return;
			}

			switch (IPC_GET_IMETHOD(call)) {
			case WINDOW_DAMAGE:
				comp_window_damage(win, callid, &call);
				break;
			case WINDOW_GRAB:
				comp_window_grab(win, callid, &call);
				break;
			case WINDOW_RESIZE:
				comp_window_resize(win, callid, &call);
				break;
			case WINDOW_CLOSE:
				comp_window_close(win, callid, &call);
				break;
			case WINDOW_CLOSE_REQUEST:
				comp_window_close_request(win, callid, &call);
				break;
			default:
				async_answer_0(callid, EINVAL);
			}
		}
	}
}

static void comp_mode_change(viewport_t *vp, ipc_callid_t iid, ipc_call_t *icall)
{
	int rc;
	sysarg_t mode_idx = IPC_GET_ARG2(*icall);
	fibril_mutex_lock(&viewport_list_mtx);

	/* Retrieve the mode that shall be set. */
	vslmode_t new_mode;
	rc = visualizer_get_mode(vp->sess, &new_mode, mode_idx);
	if (rc != EOK) {
		fibril_mutex_unlock(&viewport_list_mtx);
		async_answer_0(iid, EINVAL);
		return;
	}

	/* Create surface with respect to the retrieved mode. */
	surface_t *new_surface = surface_create(new_mode.screen_width, 
	    new_mode.screen_height, NULL, SURFACE_FLAG_SHARED);
	if (!new_surface) {
		fibril_mutex_unlock(&viewport_list_mtx);
		async_answer_0(iid, ENOMEM);
		return;
	}

	/* Try to set the mode and share out the surface. */
	rc = visualizer_set_mode(vp->sess,
		new_mode.index, new_mode.version, surface_direct_access(new_surface));
	if (rc != EOK) {
		surface_destroy(new_surface);
		fibril_mutex_unlock(&viewport_list_mtx);
		async_answer_0(iid, rc);
		return;
	}

	/* Destroy old surface and update viewport. */
	surface_destroy(vp->surface);
	vp->mode = new_mode;
	vp->surface = new_surface;

	fibril_mutex_unlock(&viewport_list_mtx);
	async_answer_0(iid, EOK);

	comp_damage(0, 0, UINT32_MAX, UINT32_MAX);
}

static void viewport_destroy(viewport_t *vp)
{
	if (vp) {
		visualizer_yield(vp->sess);
		surface_destroy(vp->surface);
		async_hangup(vp->sess);
		free(vp);
	}
}

static void comp_visualizer_disconnect(viewport_t *vp, ipc_callid_t iid, ipc_call_t *icall)
{
	/* Release viewport resources. */
	fibril_mutex_lock(&viewport_list_mtx);
	list_remove(&vp->link);
	viewport_destroy(vp);

	/* Terminate compositor if there are no more viewports. */
	if (list_empty(&viewport_list)) {
		fibril_mutex_unlock(&viewport_list_mtx);
		loc_service_unregister(winreg_id);
		input_disconnect();

		/* Close all clients and their windows. */
		fibril_mutex_lock(&window_list_mtx);
		list_foreach(window_list, link) {
			window_t *win = list_get_instance(link, window_t, link);
			window_event_t *event = (window_event_t *) malloc(sizeof(window_event_t));
			if (event) {
				link_initialize(&event->link);
				event->type = WINDOW_CLOSE;
				prodcons_produce(&win->queue, &event->link);
			}
		}
		fibril_mutex_unlock(&window_list_mtx);

		async_answer_0(iid, EOK);

		/* All fibrils of the compositor will terminate soon. */
	} else {
		fibril_mutex_unlock(&viewport_list_mtx);
		async_answer_0(iid, EOK);
	}
}

static void vsl_notifications(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	viewport_t *vp = NULL;
	fibril_mutex_lock(&viewport_list_mtx);
	list_foreach(viewport_list, link) {
		viewport_t *cur = list_get_instance(link, viewport_t, link);
		if (cur->dsid == (service_id_t) IPC_GET_ARG1(*icall)) {
			vp = cur;
			break;
		}
	}
	fibril_mutex_unlock(&viewport_list_mtx);

	if (!vp) {
		return;
	}

	/* Ignore parameters, the connection is already opened. */
	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);

		if (!IPC_GET_IMETHOD(call)) {
			async_hangup(vp->sess);
			return;
		}

		switch (IPC_GET_IMETHOD(call)) {
		case VISUALIZER_MODE_CHANGE:
			comp_mode_change(vp, callid, &call);
			break;
		case VISUALIZER_DISCONNECT:
			comp_visualizer_disconnect(vp, callid, &call);
			return;
		default:
			async_answer_0(callid, EINVAL);
		}
	}
}

static async_sess_t *vsl_connect(const char *svc)
{
	int rc;
	async_sess_t *sess;
	service_id_t dsid;
	devman_handle_t handle;

	rc = loc_service_get_id(svc, &dsid, 0);
	if (rc != EOK) {
		return NULL;
	}

	rc = devman_fun_sid_to_handle(dsid, &handle);
	if (rc == EOK) {
		sess = devman_device_connect(EXCHANGE_SERIALIZE, handle, 0);
		if (sess == NULL) {
			printf("%s: Unable to connect to visualizer %s\n", NAME, svc);
			return NULL;
		}
		rc = graph_dev_connect(sess);
		if (rc != EOK) {
			return NULL;
		}
	} else if (rc == ENOENT) {
		sess = loc_service_connect(EXCHANGE_SERIALIZE, dsid, 0);
		if (sess == NULL) {
			printf("%s: Unable to connect to visualizer %s\n", NAME, svc);
			return NULL;
		}
	} else {
		return NULL;
	}

	async_exch_t *exch = async_exchange_begin(sess);
	rc = async_connect_to_me(exch, dsid, 0, 0, vsl_notifications, NULL);
	async_exchange_end(exch);

	if (rc != EOK) {
		async_hangup(sess);
		printf("%s: Unable to create callback connection to service %s (%s)\n",
		    NAME, svc, str_error(rc));
		return NULL;
	}

	return sess;
}

static viewport_t *viewport_create(const char *vsl_name)
{
	int rc;

	viewport_t *vp = (viewport_t *) malloc(sizeof(viewport_t));
	if (!vp) {
		return NULL;
	}

	link_initialize(&vp->link);
	vp->pos.x = coord_origin;
	vp->pos.y = coord_origin;

	/* Establish output bidirectional connection. */
	vp->sess = vsl_connect(vsl_name);
	rc = loc_service_get_id(vsl_name, &vp->dsid, 0);
	if (vp->sess == NULL || rc != EOK) {
		free(vp);
		return NULL;
	}

	/* Claim the given visualizer. */
	rc = visualizer_claim(vp->sess, 0);
	if (rc != EOK) {
		async_hangup(vp->sess);
		free(vp);
		printf("%s: Unable to claim visualizer (%s)\n", NAME, str_error(rc));
		return NULL;
	}

	/* Retrieve the default mode. */
	rc = visualizer_get_default_mode(vp->sess, &vp->mode);
	if (rc != EOK) {
		visualizer_yield(vp->sess);
		async_hangup(vp->sess);
		free(vp);
		printf("%s: Unable to retrieve mode (%s)\n", NAME, str_error(rc));
		return NULL;
	}

	/* Create surface with respect to the retrieved mode. */
	vp->surface = surface_create(vp->mode.screen_width, vp->mode.screen_height,
	    NULL, SURFACE_FLAG_SHARED);
	if (vp->surface == NULL) {
		visualizer_yield(vp->sess);
		async_hangup(vp->sess);
		free(vp);
		printf("%s: Unable to create surface (%s)\n", NAME, str_error(rc));
		return NULL;
	}

	/* Try to set the mode and share out the surface. */
	rc = visualizer_set_mode(vp->sess,
		vp->mode.index, vp->mode.version, surface_direct_access(vp->surface));
	if (rc != EOK) {
		visualizer_yield(vp->sess);
		surface_destroy(vp->surface);
		async_hangup(vp->sess);
		free(vp);
		printf("%s: Unable to set mode (%s)\n", NAME, str_error(rc));
		return NULL;
	}

	return vp;
}

static void comp_recalc_transform(window_t *win)
{
	transform_t translate;
	transform_identity(&translate);
	transform_translate(&translate, win->dx, win->dy);

	transform_t scale;
	transform_identity(&scale);
	if (win->fx != 1 || win->fy != 1) {
		transform_scale(&scale, win->fx, win->fy);
	}

	transform_t rotate;
	transform_identity(&rotate);
	if (win->angle != 0) {
		transform_rotate(&rotate, win->angle);
	}

	transform_t transform;
	transform_t temp;
	transform_identity(&transform);
	temp = transform;
	transform_multiply(&transform, &temp, &translate);
	temp = transform;
	transform_multiply(&transform, &temp, &rotate);
	temp = transform;
	transform_multiply(&transform, &temp, &scale);
	

	win->transform = transform;
}

static void comp_window_animate(pointer_t *pointer, window_t *win,
    sysarg_t *dmg_x, sysarg_t *dmg_y, sysarg_t *dmg_width, sysarg_t *dmg_height)
{
	/* window_list_mtx locked by caller */
	/* pointer_list_mtx locked by caller */

	int dx = pointer->accum.x;
	int dy = pointer->accum.y;
	pointer->accum.x = 0;
	pointer->accum.y = 0;

	bool move = (pointer->grab_flags & GF_MOVE_X) || (pointer->grab_flags & GF_MOVE_Y);
	bool scale = (pointer->grab_flags & GF_SCALE_X) || (pointer->grab_flags & GF_SCALE_Y);
	bool resize = (pointer->grab_flags & GF_RESIZE_X) || (pointer->grab_flags & GF_RESIZE_Y);

	sysarg_t width, height;
	surface_get_resolution(win->surface, &width, &height);

	if (move) {
		double cx = 0;
		double cy = 0;
		if (pointer->grab_flags & GF_MOVE_X) {
			cx = 1;
		}
		if (pointer->grab_flags & GF_MOVE_Y) {
			cy = 1;
		}

		if ((scale || resize) && (win->angle != 0)) {
			transform_t rotate;
			transform_identity(&rotate);
			transform_rotate(&rotate, win->angle);
			transform_apply_linear(&rotate, &cx, &cy);
		}
		
		cx = (cx < 0) ? (-1 * cx) : cx; 
		cy = (cy < 0) ? (-1 * cy) : cy;

		win->dx += (cx * dx);
		win->dy += (cy * dy);
	}

	if (scale || resize) {
		double _dx = dx;
		double _dy = dy;
		if (win->angle != 0) {
			transform_t unrotate;
			transform_identity(&unrotate);
			transform_rotate(&unrotate, -win->angle);
			transform_apply_linear(&unrotate, &_dx, &_dy);
		}
		_dx = (pointer->grab_flags & GF_MOVE_X) ? -_dx : _dx;
		_dy = (pointer->grab_flags & GF_MOVE_Y) ? -_dy : _dy;

		if ((pointer->grab_flags & GF_SCALE_X) || (pointer->grab_flags & GF_RESIZE_X)) {
			double fx = 1.0 + (_dx / ((width - 1) * win->fx));
			if (fx > 0) {
#if ANIMATE_WINDOW_TRANSFORMS == 0
				if (scale) win->fx *= fx;
#endif
#if ANIMATE_WINDOW_TRANSFORMS == 1
				win->fx *= fx;
#endif
				scale_back_x *= fx;
			}
		}

		if ((pointer->grab_flags & GF_SCALE_Y) || (pointer->grab_flags & GF_RESIZE_Y)) {
			double fy = 1.0 + (_dy / ((height - 1) * win->fy));
			if (fy > 0) {
#if ANIMATE_WINDOW_TRANSFORMS == 0
				if (scale) win->fy *= fy;
#endif
#if ANIMATE_WINDOW_TRANSFORMS == 1
				win->fy *= fy;
#endif
				scale_back_y *= fy;
			}
		}
	}

	sysarg_t x1, y1, width1, height1;
	sysarg_t x2, y2, width2, height2;
	comp_coord_bounding_rect(0, 0, width, height, win->transform,
	    &x1, &y1, &width1, &height1);
	comp_recalc_transform(win);
	comp_coord_bounding_rect(0, 0, width, height, win->transform,
	    &x2, &y2, &width2, &height2);
	rectangle_union(x1, y1, width1, height1, x2, y2, width2, height2,
	    dmg_x, dmg_y, dmg_width, dmg_height);
}

#if ANIMATE_WINDOW_TRANSFORMS == 0
static void comp_ghost_animate(pointer_t *pointer,
    desktop_rect_t *rect1, desktop_rect_t *rect2, desktop_rect_t *rect3, desktop_rect_t *rect4)
{
	/* window_list_mtx locked by caller */
	/* pointer_list_mtx locked by caller */

	int dx = pointer->accum_ghost.x;
	int dy = pointer->accum_ghost.y;
	pointer->accum_ghost.x = 0;
	pointer->accum_ghost.y = 0;

	bool move = (pointer->grab_flags & GF_MOVE_X) || (pointer->grab_flags & GF_MOVE_Y);
	bool scale = (pointer->grab_flags & GF_SCALE_X) || (pointer->grab_flags & GF_SCALE_Y);
	bool resize = (pointer->grab_flags & GF_RESIZE_X) || (pointer->grab_flags & GF_RESIZE_Y);

	sysarg_t width, height;
	surface_get_resolution(pointer->ghost.surface, &width, &height);

	if (move) {
		double cx = 0;
		double cy = 0;
		if (pointer->grab_flags & GF_MOVE_X) {
			cx = 1;
		}
		if (pointer->grab_flags & GF_MOVE_Y) {
			cy = 1;
		}

		if (scale || resize) {
			transform_t rotate;
			transform_identity(&rotate);
			transform_rotate(&rotate, pointer->ghost.angle);
			transform_apply_linear(&rotate, &cx, &cy);
		}

		cx = (cx < 0) ? (-1 * cx) : cx;
		cy = (cy < 0) ? (-1 * cy) : cy;

		pointer->ghost.dx += (cx * dx);
		pointer->ghost.dy += (cy * dy);
	}

	if (scale || resize) {
		double _dx = dx;
		double _dy = dy;
		transform_t unrotate;
		transform_identity(&unrotate);
		transform_rotate(&unrotate, -pointer->ghost.angle);
		transform_apply_linear(&unrotate, &_dx, &_dy);
		_dx = (pointer->grab_flags & GF_MOVE_X) ? -_dx : _dx;
		_dy = (pointer->grab_flags & GF_MOVE_Y) ? -_dy : _dy;

		if ((pointer->grab_flags & GF_SCALE_X) || (pointer->grab_flags & GF_RESIZE_X)) {
			double fx = 1.0 + (_dx / ((width - 1) * pointer->ghost.fx));
			pointer->ghost.fx *= fx;
		}

		if ((pointer->grab_flags & GF_SCALE_Y) || (pointer->grab_flags & GF_RESIZE_Y)) {
			double fy = 1.0 + (_dy / ((height - 1) * pointer->ghost.fy));
			pointer->ghost.fy *= fy;
		}
	}

	sysarg_t x1, y1, width1, height1;
	sysarg_t x2, y2, width2, height2;
	comp_coord_bounding_rect(0, 0, width, height, pointer->ghost.transform,
	    &x1, &y1, &width1, &height1);
	comp_recalc_transform(&pointer->ghost);
	comp_coord_bounding_rect(0, 0, width, height, pointer->ghost.transform,
	    &x2, &y2, &width2, &height2);

	sysarg_t x_u, y_u, w_u, h_u;
	rectangle_union(x1, y1, width1, height1, x2, y2, width2, height2,
	    &x_u, &y_u, &w_u, &h_u);

	sysarg_t x_i, y_i, w_i, h_i;
	rectangle_intersect(x1, y1, width1, height1, x2, y2, width2, height2,
	    &x_i, &y_i, &w_i, &h_i);

	if (w_i == 0 || h_i == 0) {
		rect1->x = x_u; rect2->x = 0; rect3->x = 0; rect4->x = 0;
		rect1->y = y_u; rect2->y = 0; rect3->y = 0; rect4->y = 0;
		rect1->w = w_u; rect2->w = 0; rect3->w = 0; rect4->w = 0;
		rect1->h = h_u; rect2->h = 0; rect3->h = 0; rect4->h = 0;
	} else {
		rect1->x = x_u;
		rect1->y = y_u;
		rect1->w = x_i - x_u + 1;
		rect1->h = h_u;

		rect2->x = x_u;
		rect2->y = y_u;
		rect2->w = w_u;
		rect2->h = y_i - y_u + 1;

		rect3->x = x_i + w_i - 1;
		rect3->y = y_u;
		rect3->w = w_u - w_i - x_i + x_u + 1;
		rect3->h = h_u;
		
		rect4->x = x_u;
		rect4->y = y_i + h_i - 1;
		rect4->w = w_u;
		rect4->h = h_u - h_i - y_i + y_u + 1;
	}
}
#endif

static int comp_abs_move(input_t *input, unsigned x , unsigned y,
    unsigned max_x, unsigned max_y)
{
	/* XXX TODO Use absolute coordinates directly */
	
	pointer_t *pointer = input_pointer(input);
	
	sysarg_t width, height;
	
	fibril_mutex_lock(&viewport_list_mtx);
	if (list_empty(&viewport_list)) {
		printf("No viewport found\n");
		fibril_mutex_unlock(&viewport_list_mtx);
		return EOK; /* XXX */
	}
	link_t *link = list_first(&viewport_list);
	viewport_t *vp = list_get_instance(link, viewport_t, link);
	surface_get_resolution(vp->surface, &width, &height);
	desktop_point_t vp_pos = vp->pos;
	fibril_mutex_unlock(&viewport_list_mtx);

	desktop_point_t pos_in_viewport;
	pos_in_viewport.x = x * width / max_x;
	pos_in_viewport.y = y * height / max_y;
	
	/* Calculate offset from pointer */
	fibril_mutex_lock(&pointer_list_mtx);
	desktop_vector_t delta;
	delta.x = (vp_pos.x + pos_in_viewport.x) - pointer->pos.x;
	delta.y = (vp_pos.y + pos_in_viewport.y) - pointer->pos.y;
	fibril_mutex_unlock(&pointer_list_mtx);
	
	return comp_mouse_move(input, delta.x, delta.y);
}

static int comp_mouse_move(input_t *input, int dx, int dy)
{
	pointer_t *pointer = input_pointer(input);

	/* Update pointer position. */
	fibril_mutex_lock(&pointer_list_mtx);
	desktop_point_t old_pos = pointer->pos;
	sysarg_t cursor_width;
	sysarg_t cursor_height;
	surface_get_resolution(pointer->cursor.states[pointer->state], 
	     &cursor_width, &cursor_height);
	pointer->pos.x += dx;
	pointer->pos.y += dy;
	fibril_mutex_unlock(&pointer_list_mtx);
	comp_damage(old_pos.x, old_pos.y, cursor_width, cursor_height);
	comp_damage(old_pos.x + dx, old_pos.y + dy, cursor_width, cursor_height);

	fibril_mutex_lock(&window_list_mtx);
	fibril_mutex_lock(&pointer_list_mtx);
	window_t *top = (window_t *) list_first(&window_list);
	if (top && top->surface) {

		if (pointer->grab_flags == GF_EMPTY) {
			/* Notify top-level window about move event. */
			bool within_client = false;
			sysarg_t point_x, point_y;
			sysarg_t width, height;
			surface_get_resolution(top->surface, &width, &height);
			within_client = comp_coord_to_client(pointer->pos.x, pointer->pos.y,
			    top->transform, width, height, &point_x, &point_y);

			window_event_t *event = NULL;
			if (within_client) {
				event = (window_event_t *) malloc(sizeof(window_event_t));
				if (event) {
					link_initialize(&event->link);
					event->type = ET_POSITION_EVENT;
					event->data.pos.pos_id = pointer->id;
					event->data.pos.type = POS_UPDATE;
					event->data.pos.btn_num = pointer->btn_num;
					event->data.pos.hpos = point_x;
					event->data.pos.vpos = point_y;
				}
			}

			fibril_mutex_unlock(&pointer_list_mtx);
			fibril_mutex_unlock(&window_list_mtx);

			if (event) {
				comp_post_event_top(event);
			}

		} else {
			/* Pointer is grabbed by top-level window action. */
			pointer->accum.x += dx;
			pointer->accum.y += dy;
			pointer->accum_ghost.x += dx;
			pointer->accum_ghost.y += dy;
#if ANIMATE_WINDOW_TRANSFORMS == 0
			if (pointer->ghost.surface == NULL) {
				pointer->ghost.surface = top->surface;
				pointer->ghost.dx = top->dx;
				pointer->ghost.dy = top->dy;
				pointer->ghost.fx = top->fx;
				pointer->ghost.fy = top->fy;
				pointer->ghost.angle = top->angle;
				pointer->ghost.transform = top->transform;
			}
			desktop_rect_t dmg_rect1, dmg_rect2, dmg_rect3, dmg_rect4;
			comp_ghost_animate(pointer, &dmg_rect1, &dmg_rect2, &dmg_rect3, &dmg_rect4);
#endif
#if ANIMATE_WINDOW_TRANSFORMS == 1
			sysarg_t x, y, width, height;
			comp_window_animate(pointer, top, &x, &y, &width, &height);
#endif
			fibril_mutex_unlock(&pointer_list_mtx);
			fibril_mutex_unlock(&window_list_mtx);
#if ANIMATE_WINDOW_TRANSFORMS == 0
			comp_damage(dmg_rect1.x, dmg_rect1.y, dmg_rect1.w, dmg_rect1.h);
			comp_damage(dmg_rect2.x, dmg_rect2.y, dmg_rect2.w, dmg_rect2.h);
			comp_damage(dmg_rect3.x, dmg_rect3.y, dmg_rect3.w, dmg_rect3.h);
			comp_damage(dmg_rect4.x, dmg_rect4.y, dmg_rect4.w, dmg_rect4.h);
#endif
#if ANIMATE_WINDOW_TRANSFORMS == 1
			comp_damage(x, y, width, height);
#endif
		}
	} else {
		fibril_mutex_unlock(&pointer_list_mtx);
		fibril_mutex_unlock(&window_list_mtx);
	}

	return EOK;
}

static int comp_mouse_button(input_t *input, int bnum, int bpress)
{
	pointer_t *pointer = input_pointer(input);

	fibril_mutex_lock(&window_list_mtx);
	fibril_mutex_lock(&pointer_list_mtx);
	window_t *win = NULL;
	sysarg_t point_x = 0;
	sysarg_t point_y = 0;
	sysarg_t width, height;
	bool within_client = false;

	/* Determine the window which the mouse click belongs to. */
	list_foreach(window_list, link) {
		win = list_get_instance(link, window_t, link);
		if (win->surface) {
			surface_get_resolution(win->surface, &width, &height);
			within_client = comp_coord_to_client(pointer->pos.x, pointer->pos.y,
			    win->transform, width, height, &point_x, &point_y);
		}
		if (within_client) {
			break;
		}
	}

	/* Check whether the window is top-level window. */
	window_t *top = (window_t *) list_first(&window_list);
	if (!win || !top) {
		fibril_mutex_unlock(&pointer_list_mtx);
		fibril_mutex_unlock(&window_list_mtx);
		return EOK;
	}

	window_event_t *event_top = NULL;
	window_event_t *event_unfocus = NULL;
	window_t *win_unfocus = NULL;
	sysarg_t dmg_x, dmg_y;
	sysarg_t dmg_width = 0;
	sysarg_t dmg_height = 0;
	
#if ANIMATE_WINDOW_TRANSFORMS == 0
	desktop_rect_t dmg_rect1, dmg_rect2, dmg_rect3, dmg_rect4;
#endif

	if (bpress) {
		pointer->btn_pos = pointer->pos;
		pointer->btn_num = bnum;
		pointer->pressed = true;

		/* Bring the window to the foreground. */
		if ((win != top) && within_client) {
			win_unfocus = (window_t *) list_first(&window_list);
			list_remove(&win->link);
			list_prepend(&win->link, &window_list);
			event_unfocus = (window_event_t *) malloc(sizeof(window_event_t));
			if (event_unfocus) {
				link_initialize(&event_unfocus->link);
				event_unfocus->type = ET_WINDOW_UNFOCUS;
			}
			comp_coord_bounding_rect(0, 0, width, height, win->transform,
			    &dmg_x, &dmg_y, &dmg_width, &dmg_height);
		}

		/* Notify top-level window about mouse press. */
		if (within_client) {
			event_top = (window_event_t *) malloc(sizeof(window_event_t));
			if (event_top) {
				link_initialize(&event_top->link);
				event_top->type = ET_POSITION_EVENT;
				event_top->data.pos.pos_id = pointer->id;
				event_top->data.pos.type = POS_PRESS;
				event_top->data.pos.btn_num = bnum;
				event_top->data.pos.hpos = point_x;
				event_top->data.pos.vpos = point_y;
			}
			pointer->grab_flags = GF_EMPTY;
		}

	} else if (pointer->pressed && pointer->btn_num == (unsigned)bnum) {
		pointer->pressed = false;

#if ANIMATE_WINDOW_TRANSFORMS == 0
		sysarg_t pre_x = 0; 
		sysarg_t pre_y = 0;
		sysarg_t pre_width = 0;
		sysarg_t pre_height = 0;

		if (pointer->grab_flags != GF_EMPTY) {
			if (pointer->ghost.surface) {
				comp_ghost_animate(pointer, &dmg_rect1, &dmg_rect2, &dmg_rect3, &dmg_rect4);
				pointer->ghost.surface = NULL;
			}
			comp_window_animate(pointer, top, &pre_x, &pre_y, &pre_width, &pre_height);
			dmg_x = pre_x;
			dmg_y = pre_y;
			dmg_width = pre_width;
			dmg_height = pre_height;
		}
#endif

		if ((pointer->grab_flags & GF_RESIZE_X) || (pointer->grab_flags & GF_RESIZE_Y)) {

			surface_get_resolution(top->surface, &width, &height);
#if ANIMATE_WINDOW_TRANSFORMS == 1
			top->fx *= (1.0 / scale_back_x);
			top->fy *= (1.0 / scale_back_y);
			comp_recalc_transform(top);
#endif

			/* Commit proper resize action. */
			event_top = (window_event_t *) malloc(sizeof(window_event_t));
			if (event_top) {
				link_initialize(&event_top->link);
				event_top->type = ET_WINDOW_RESIZE;

				int dx = (int) (((double) width) * (scale_back_x - 1.0));
				int dy = (int) (((double) height) * (scale_back_y - 1.0));

				if (pointer->grab_flags & GF_RESIZE_X) {
					event_top->data.rsz.width =
						((((int) width) + dx) >= 0) ? (width + dx) : 0;
				} else {
					event_top->data.rsz.width = width;
				}

				if (pointer->grab_flags & GF_RESIZE_Y) {
					event_top->data.rsz.height =
						((((int) height) + dy) >= 0) ? (height + dy) : 0;
				} else {
					event_top->data.rsz.height = height;
				}
			}

			pointer->grab_flags = GF_EMPTY;

		} else if (within_client && (pointer->grab_flags == GF_EMPTY) && (top == win)) {
			
			/* Notify top-level window about mouse release. */
			event_top = (window_event_t *) malloc(sizeof(window_event_t));
			if (event_top) {
				link_initialize(&event_top->link);
				event_top->type = ET_POSITION_EVENT;
				event_top->data.pos.pos_id = pointer->id;
				event_top->data.pos.type = POS_RELEASE;
				event_top->data.pos.btn_num = bnum;
				event_top->data.pos.hpos = point_x;
				event_top->data.pos.vpos = point_y;
			}
			pointer->grab_flags = GF_EMPTY;
			
		} else {
			pointer->grab_flags = GF_EMPTY;
		}

	}

	fibril_mutex_unlock(&pointer_list_mtx);
	fibril_mutex_unlock(&window_list_mtx);

#if ANIMATE_WINDOW_TRANSFORMS == 0
		comp_damage(dmg_rect1.x, dmg_rect1.y, dmg_rect1.w, dmg_rect1.h);
		comp_damage(dmg_rect2.x, dmg_rect2.y, dmg_rect2.w, dmg_rect2.h);
		comp_damage(dmg_rect3.x, dmg_rect3.y, dmg_rect3.w, dmg_rect3.h);
		comp_damage(dmg_rect4.x, dmg_rect4.y, dmg_rect4.w, dmg_rect4.h);
#endif

	if (dmg_width > 0 && dmg_height > 0) {
		comp_damage(dmg_x, dmg_y, dmg_width, dmg_height);
	}

	if (event_unfocus && win_unfocus) {
		comp_post_event_win(event_unfocus, win_unfocus);
	}

	if (event_top) {
		comp_post_event_top(event_top);
	}

	return EOK;
}

static int comp_key_press(input_t *input, kbd_event_type_t type, keycode_t key,
    keymod_t mods, wchar_t c)
{
	bool win_transform = (mods & KM_ALT) && (
	    key == KC_W || key == KC_S || key == KC_A || key == KC_D ||
	    key == KC_Q || key == KC_E || key == KC_R || key == KC_F);
	bool win_resize = (mods & KM_ALT) && (
	    key == KC_T || key == KC_G || key == KC_B || key == KC_N);
	bool win_opacity = (mods & KM_ALT) && (
	    key == KC_C || key == KC_V);
	bool win_close = (mods & KM_ALT) && (key == KC_X);
	bool win_switch = (mods & KM_ALT) && (key == KC_TAB);
	bool viewport_move = (mods & KM_ALT) && (
	    key == KC_I || key == KC_K || key == KC_J || key == KC_L);
	bool viewport_change = (mods & KM_ALT) && (
	    key == KC_O || key == KC_P);
	bool kconsole_switch = (mods & KM_ALT) && (key == KC_M);
	bool compositor_test = (mods & KM_ALT) && (key == KC_H);

	bool filter = (type == KEY_RELEASE) && (win_transform || win_resize ||
	    win_opacity || win_close || win_switch || viewport_move ||
	    viewport_change || kconsole_switch || compositor_test);

	if (filter) {
		/* no-op */
	} else if (win_transform) {
		fibril_mutex_lock(&window_list_mtx);
		window_t *win = (window_t *) list_first(&window_list);
		if (win && win->surface) {
			switch (key) {
			case KC_W:
				win->dy += -20;
				break;
			case KC_S:
				win->dy += 20;
				break;
			case KC_A:
				win->dx += -20;
				break;
			case KC_D:
				win->dx += 20;
				break;
			case KC_Q:
				win->angle += (PI / 2);
				break;
			case KC_E:
				win->angle += -(PI / 2);
				break;
			case KC_R:
				win->fx *= 0.95;
				win->fy *= 0.95;
				break;
			case KC_F:
				win->fx *= 1.05;
				win->fy *= 1.05;
				break;
			default:
				break;
			}

			/* Transform the window and calculate damage. */
			sysarg_t x, y, width, height;
			surface_get_resolution(win->surface, &width, &height);
			sysarg_t x1, y1, width1, height1;
			sysarg_t x2, y2, width2, height2;
			comp_coord_bounding_rect(0, 0, width, height, win->transform,
			    &x1, &y1, &width1, &height1);
			comp_recalc_transform(win);
			comp_coord_bounding_rect(0, 0, width, height, win->transform,
			    &x2, &y2, &width2, &height2);
			rectangle_union(x1, y1, width1, height1, x2, y2, width2, height2,
			    &x, &y, &width, &height);
			fibril_mutex_unlock(&window_list_mtx);

			comp_damage(x, y, width, height);
		} else {
			fibril_mutex_unlock(&window_list_mtx);
		}
	} else if (win_resize) {
		fibril_mutex_lock(&window_list_mtx);
		window_t *win = (window_t *) list_first(&window_list);
		if (win && win->surface) {
			window_event_t *event = (window_event_t *) malloc(sizeof(window_event_t));
			if (event == NULL) {
				fibril_mutex_unlock(&window_list_mtx);
				return ENOMEM;
			}

			sysarg_t width, height;
			surface_get_resolution(win->surface, &width, &height);

			link_initialize(&event->link);
			event->type = ET_WINDOW_RESIZE;

			switch (key) {
			case KC_T:
				event->data.rsz.width = width;
				event->data.rsz.height = (height >= 20) ? height - 20 : 0;
				break;
			case KC_G:
				event->data.rsz.width = width;
				event->data.rsz.height = height + 20;
				break;
			case KC_B:
				event->data.rsz.width = (width >= 20) ? width - 20 : 0;;
				event->data.rsz.height = height;
				break;
			case KC_N:
				event->data.rsz.width = width + 20;
				event->data.rsz.height = height;
				break;
			default:
				event->data.rsz.width = 0;
				event->data.rsz.height = 0;
				break;
			}

			fibril_mutex_unlock(&window_list_mtx);
			comp_post_event_top(event);
		} else {
			fibril_mutex_unlock(&window_list_mtx);
		}
	} else if (win_opacity) {
		fibril_mutex_lock(&window_list_mtx);
		window_t *win = (window_t *) list_first(&window_list);
		if (win && win->surface) {
			switch (key) {
			case KC_C:
				if (win->opacity > 0) {
					win->opacity -= 5;
				}
				break;
			case KC_V:
				if (win->opacity < 255) {
					win->opacity += 5;
				}
				break;
			default:
				break;
			}

			/* Calculate damage. */
			sysarg_t x, y, width, height;
			surface_get_resolution(win->surface, &width, &height);
			comp_coord_bounding_rect(0, 0, width, height, win->transform,
			    &x, &y, &width, &height);
			fibril_mutex_unlock(&window_list_mtx);

			comp_damage(x, y, width, height);
		} else {
			fibril_mutex_unlock(&window_list_mtx);
		}
	} else if (win_close) {
		window_event_t *event = (window_event_t *) malloc(sizeof(window_event_t));
		if (event == NULL)
			return ENOMEM;

		link_initialize(&event->link);
		event->type = ET_WINDOW_CLOSE;

		comp_post_event_top(event);
	} else if (win_switch) {
		fibril_mutex_lock(&window_list_mtx);
		if (!list_empty(&window_list)) {
			window_t *win1 = (window_t *) list_first(&window_list);
			list_remove(&win1->link);
			list_append(&win1->link, &window_list);
			window_t *win2 = (window_t *) list_first(&window_list);

			window_event_t *event1 = (window_event_t *) malloc(sizeof(window_event_t));
			if (event1) {
				link_initialize(&event1->link);
				event1->type = ET_WINDOW_UNFOCUS;
			}

			window_event_t *event2 = (window_event_t *) malloc(sizeof(window_event_t));
			if (event2) {
				link_initialize(&event2->link);
				event2->type = ET_WINDOW_FOCUS;
			}

			sysarg_t x1 = 0;
			sysarg_t y1 = 0;
			sysarg_t width1 = 0;
			sysarg_t height1 = 0;
			if (win1->surface) {
				sysarg_t width, height;
				surface_get_resolution(win1->surface, &width, &height);
				comp_coord_bounding_rect(0, 0, width, height, win1->transform,
				    &x1, &y1, &width1, &height1);
			}

			sysarg_t x2 = 0;
			sysarg_t y2 = 0;
			sysarg_t width2 = 0;
			sysarg_t height2 = 0;
			if (win2->surface) {
				sysarg_t width, height;
				surface_get_resolution(win2->surface, &width, &height);
				comp_coord_bounding_rect(0, 0, width, height, win2->transform,
				    &x2, &y2, &width2, &height2);
			}

			sysarg_t x, y, width, height;
			rectangle_union(x1, y1, width1, height1, x2, y2, width2, height2,
			    &x, &y, &width, &height);

			fibril_mutex_unlock(&window_list_mtx);

			if (event1 && win1) {
				comp_post_event_win(event1, win1);
			}

			if (event2 && win2) {
				comp_post_event_win(event2, win2);
			}

			comp_damage(x, y, width, height);
		} else {
			fibril_mutex_unlock(&window_list_mtx);
		}
	} else if (viewport_move) {
		fibril_mutex_lock(&viewport_list_mtx);
		viewport_t *vp = (viewport_t *) list_first(&viewport_list);
		if (vp) {
			switch (key) {
			case KC_I:
				vp->pos.x += 0;
				vp->pos.y += -20;
				break;
			case KC_K:
				vp->pos.x += 0;
				vp->pos.y += 20;
				break;
			case KC_J:
				vp->pos.x += -20;
				vp->pos.y += 0;
				break;
			case KC_L:
				vp->pos.x += 20;
				vp->pos.y += 0;
				break;
			default:
				vp->pos.x += 0;
				vp->pos.y += 0;
				break;
			}
			
			sysarg_t x = vp->pos.x;
			sysarg_t y = vp->pos.y;
			sysarg_t width, height;
			surface_get_resolution(vp->surface, &width, &height);
			fibril_mutex_unlock(&viewport_list_mtx);

			comp_damage(x, y, width, height);
		} else {
			fibril_mutex_unlock(&viewport_list_mtx);
		}
	} else if (viewport_change) {
		fibril_mutex_lock(&viewport_list_mtx);

		viewport_t *vp;
		switch (key) {
		case KC_O:
			vp = (viewport_t *) list_first(&viewport_list);
			if (vp) {
				list_remove(&vp->link);
				list_append(&vp->link, &viewport_list);
			}
			break;
		case KC_P:
			vp = (viewport_t *) list_last(&viewport_list);
			if (vp) {
				list_remove(&vp->link);
				list_prepend(&vp->link, &viewport_list);
			}
			break;
		default:
			break;
		}

		fibril_mutex_unlock(&viewport_list_mtx);
	} else if (kconsole_switch) {
		__SYSCALL0(SYS_DEBUG_ACTIVATE_CONSOLE);
	} else if (compositor_test) {
		fibril_mutex_lock(&window_list_mtx);

		window_t *red_win = window_create(0, 0);
		red_win->surface = surface_create(250, 150, NULL, 0);
		pixel_t red_pix = PIXEL(255, 240, 0, 0);
		for (sysarg_t y = 0; y <  150; ++y) {
			for (sysarg_t x = 0; x < 250; ++x) {
				surface_put_pixel(red_win->surface, x, y, red_pix);
			}
		}
		list_prepend(&red_win->link, &window_list);

		window_t *blue_win = window_create(0, 0);
		blue_win->surface = surface_create(200, 100, NULL, 0);
		pixel_t blue_pix = PIXEL(255, 0, 0, 240);
		for (sysarg_t y = 0; y <  100; ++y) {
			for (sysarg_t x = 0; x < 200; ++x) {
				surface_put_pixel(blue_win->surface, x, y, blue_pix);
			}
		}
		list_prepend(&blue_win->link, &window_list);

		window_t *helenos_win = window_create(0, 0);
		helenos_win->surface = decode_tga((void *) helenos_tga, helenos_tga_size, 0);
		list_prepend(&helenos_win->link, &window_list);

		window_t *nameic_win = window_create(0, 0);
		nameic_win->surface = decode_tga((void *) nameic_tga, nameic_tga_size, 0);
		list_prepend(&nameic_win->link, &window_list);

		fibril_mutex_unlock(&window_list_mtx);
		comp_damage(0, 0, UINT32_MAX, UINT32_MAX);
	} else {
		window_event_t *event = (window_event_t *) malloc(sizeof(window_event_t));
		if (event == NULL)
			return ENOMEM;

		link_initialize(&event->link);
		event->type = ET_KEYBOARD_EVENT;
		event->data.kbd.type = type;
		event->data.kbd.key = key;
		event->data.kbd.mods = mods;
		event->data.kbd.c = c;

		comp_post_event_top(event);
	}

	return EOK;
}

static int input_connect(const char *svc)
{
	async_sess_t *sess;
	service_id_t dsid;

	int rc = loc_service_get_id(svc, &dsid, 0);
	if (rc != EOK) {
		printf("%s: Input service %s not found\n", NAME, svc);
		return rc;
	}

	sess = loc_service_connect(EXCHANGE_ATOMIC, dsid, 0);
	if (sess == NULL) {
		printf("%s: Unable to connect to input service %s\n", NAME,
		    svc);
		return EIO;
	}

	fibril_mutex_lock(&pointer_list_mtx);
	pointer_t *pointer = pointer_create();
	if (pointer != NULL) {
		pointer->id = pointer_id++;
		list_append(&pointer->link, &pointer_list);
	}
	fibril_mutex_unlock(&pointer_list_mtx);

	if (pointer == NULL) {
		printf("%s: Cannot create pointer.\n", NAME);
		async_hangup(sess);
		return ENOMEM;
	}

	rc = input_open(sess, &input_ev_ops, pointer, &input);
	if (rc != EOK) {
		async_hangup(sess);
		printf("%s: Unable to communicate with service %s (%s)\n",
		    NAME, svc, str_error(rc));
		return rc;
	}

	return EOK;
}

static void input_disconnect(void)
{
	pointer_t *pointer = input->user;
	input_close(input);
	pointer_destroy(pointer);
}

static void interrupt_received(ipc_callid_t callid, ipc_call_t *call)
{
	comp_damage(0, 0, UINT32_MAX, UINT32_MAX);
}

static int compositor_srv_init(char *input_svc, char *name)
{
	/* Coordinates of the central pixel. */
	coord_origin = UINT32_MAX / 4;
	
	/* Color of the viewport background. Must be opaque. */
	bg_color = PIXEL(255, 75, 70, 75);
	
	/* Register compositor server. */
	async_set_client_connection(client_connection);
	int rc = loc_server_register(NAME);
	if (rc != EOK) {
		printf("%s: Unable to register server (%s)\n", NAME, str_error(rc));
		return -1;
	}
	
	/* Register interrupt handler to switch back from kconsole. */
	async_set_interrupt_received(interrupt_received);
	rc = event_subscribe(EVENT_KCONSOLE, 0);
	if (rc != EOK) {
		printf("%s: Failed to register kconsole notifications (%s)\n",
		    NAME, str_error(rc));
	}
	
	server_name = name;
	
	char svc[LOC_NAME_MAXLEN + 1];
	snprintf(svc, LOC_NAME_MAXLEN, "%s/%s", NAMESPACE, server_name);
	
	service_id_t service_id;
	rc = loc_service_register(svc, &service_id);
	if (rc != EOK) {
		printf("%s: Unable to register service %s\n", NAME, svc);
		return rc;
	}
	
	/* Prepare window registrator (entrypoint for clients). */
	char winreg[LOC_NAME_MAXLEN + 1];
	snprintf(winreg, LOC_NAME_MAXLEN, "%s%s/winreg", NAMESPACE, server_name);
	if (loc_service_register(winreg, &winreg_id) != EOK) {
		printf("%s: Unable to register service %s\n", NAME, winreg);
		return -1;
	}

	/* Establish input bidirectional connection. */
	rc = input_connect(input_svc);
	if (rc != EOK)
		return rc;

	/* Create viewports and connect them to visualizers. */
	category_id_t cat_id;
	rc = loc_category_get_id("visualizer", &cat_id, IPC_FLAG_BLOCKING);
	if (rc != EOK) {
		input_disconnect();
		return -1;
	}
	
	service_id_t *svcs;
	size_t svcs_cnt = 0;
	rc = loc_category_get_svcs(cat_id, &svcs, &svcs_cnt);
	if (rc != EOK || svcs_cnt == 0) {
		input_disconnect();
		return -1;
	}
	
	for (size_t i = 0; i < svcs_cnt; ++i) {
		char *svc_name;
		rc = loc_service_get_name(svcs[i], &svc_name);
		if (rc == EOK) {
			viewport_t *vp = viewport_create(svc_name);
			if (vp != NULL) {
				list_append(&vp->link, &viewport_list);
			}
		}
	}
	
	if (list_empty(&viewport_list)) {
		input_disconnect();
		return -1;
	}
	
	comp_damage(0, 0, UINT32_MAX, UINT32_MAX);
	
	return EOK;
}

static void usage(char *name)
{
	printf("Usage: %s <input_dev> <server_name>\n", name);
}

int main(int argc, char *argv[])
{
	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}
	
	printf("%s: HelenOS Compositor server\n", NAME);
	
	int rc = compositor_srv_init(argv[1], argv[2]);
	if (rc != EOK)
		return rc;
	
	printf("%s: Accepting connections\n", NAME);
	task_retval(0);
	async_manager();
	
	/* Never reached */
	return 0;
}

/** @}
 */
