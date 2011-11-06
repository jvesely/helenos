/*
 * Copyright (c) 2008 Martin Decky
 * Copyright (c) 2006 Ondrej Palkovsky
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

/** @addtogroup genarch
 * @{
 */
/** @file
 */

#include <genarch/fb/font-8x16.h>
#include <genarch/fb/logo-196x66.h>
#include <genarch/fb/fb.h>
#include <console/chardev.h>
#include <console/console.h>
#include <sysinfo/sysinfo.h>
#include <mm/page.h>
#include <mm/slab.h>
#include <align.h>
#include <panic.h>
#include <memstr.h>
#include <config.h>
#include <bitops.h>
#include <print.h>
#include <str.h>
#include <ddi/ddi.h>
#include <typedefs.h>
#include <byteorder.h>

#define BG_COLOR     0x000080
#define FG_COLOR     0xffff00
#define INV_COLOR    0xaaaaaa

#define RED(x, bits)    (((x) >> (8 + 8 + 8 - (bits))) & ((1 << (bits)) - 1))
#define GREEN(x, bits)  (((x) >> (8 + 8 - (bits))) & ((1 << (bits)) - 1))
#define BLUE(x, bits)   (((x) >> (8 - (bits))) & ((1 << (bits)) - 1))

#define COL2X(col)  ((col) * FONT_WIDTH)
#define ROW2Y(row)  ((row) * FONT_SCANLINES)

#define X2COL(x)  ((x) / FONT_WIDTH)
#define Y2ROW(y)  ((y) / FONT_SCANLINES)

#define FB_POS(instance, x, y) \
	((y) * (instance)->scanline + (x) * (instance)->pixelbytes)

#define BB_POS(instance, col, row) \
	((row) * (instance)->cols + (col))

#define GLYPH_POS(instance, glyph, y) \
	((glyph) * (instance)->glyphbytes + (y) * (instance)->glyphscanline)

typedef void (* rgb_conv_t)(void *, uint32_t);

typedef struct {
	SPINLOCK_DECLARE(lock);
	
	parea_t parea;
	
	uint8_t *addr;
	uint16_t *backbuf;
	uint8_t *glyphs;
	uint8_t *bgscan;
	
	rgb_conv_t rgb_conv;
	
	unsigned int xres;
	unsigned int yres;
	
	unsigned int ylogo;
	unsigned int ytrim;
	unsigned int rowtrim;
	
	unsigned int scanline;
	unsigned int glyphscanline;
	
	unsigned int pixelbytes;
	unsigned int glyphbytes;
	unsigned int bgscanbytes;
	
	unsigned int cols;
	unsigned int rows;
	
	unsigned int position;
} fb_instance_t;

static void fb_putchar(outdev_t *dev, wchar_t ch);
static void fb_redraw_internal(fb_instance_t *instance);
static void fb_redraw(outdev_t *dev);

static outdev_operations_t fbdev_ops = {
	.write = fb_putchar,
	.redraw = fb_redraw
};

/*
 * RGB conversion functions.
 *
 * These functions write an RGB value to some memory in some predefined format.
 * The naming convention corresponds to the format created by these functions.
 * The functions use the so called network order (i.e. big endian) with respect
 * to their names.
 */

static void rgb_0888(void *dst, uint32_t rgb)
{
	*((uint32_t *) dst) = host2uint32_t_be((0 << 24) |
	    (RED(rgb, 8) << 16) | (GREEN(rgb, 8) << 8) | (BLUE(rgb, 8)));
}

static void bgr_0888(void *dst, uint32_t rgb)
{
	*((uint32_t *) dst) = host2uint32_t_be((0 << 24) |
	    (BLUE(rgb, 8) << 16) | (GREEN(rgb, 8) << 8) | (RED(rgb, 8)));
}

static void rgb_8880(void *dst, uint32_t rgb)
{
	*((uint32_t *) dst) = host2uint32_t_be((RED(rgb, 8) << 24) |
	    (GREEN(rgb,	8) << 16) | (BLUE(rgb, 8) << 8) | 0);
}

static void bgr_8880(void *dst, uint32_t rgb)
{
	*((uint32_t *) dst) = host2uint32_t_be((BLUE(rgb, 8) << 24) |
	    (GREEN(rgb,	8) << 16) | (RED(rgb, 8) << 8) | 0);
}

static void rgb_888(void *dst, uint32_t rgb)
{
	((uint8_t *) dst)[0] = RED(rgb, 8);
	((uint8_t *) dst)[1] = GREEN(rgb, 8);
	((uint8_t *) dst)[2] = BLUE(rgb, 8);
}

static void bgr_888(void *dst, uint32_t rgb)
{
	((uint8_t *) dst)[0] = BLUE(rgb, 8);
	((uint8_t *) dst)[1] = GREEN(rgb, 8);
	((uint8_t *) dst)[2] = RED(rgb, 8);
}

static void rgb_555_be(void *dst, uint32_t rgb)
{
	*((uint16_t *) dst) = host2uint16_t_be(RED(rgb, 5) << 10 |
	    GREEN(rgb, 5) << 5 | BLUE(rgb, 5));
}

static void rgb_555_le(void *dst, uint32_t rgb)
{
	*((uint16_t *) dst) = host2uint16_t_le(RED(rgb, 5) << 10 |
	    GREEN(rgb, 5) << 5 | BLUE(rgb, 5));
}

static void rgb_565_be(void *dst, uint32_t rgb)
{
	*((uint16_t *) dst) = host2uint16_t_be(RED(rgb, 5) << 11 |
	    GREEN(rgb, 6) << 5 | BLUE(rgb, 5));
}

static void rgb_565_le(void *dst, uint32_t rgb)
{
	*((uint16_t *) dst) = host2uint16_t_le(RED(rgb, 5) << 11 |
	    GREEN(rgb, 6) << 5 | BLUE(rgb, 5));
}

/** BGR 3:2:3
 *
 * Even though we try 3:2:3 color scheme here, an 8-bit framebuffer
 * will most likely use a color palette. The color appearance
 * will be pretty random and depend on the default installed
 * palette. This could be fixed by supporting custom palette
 * and setting it to simulate the 8-bit truecolor.
 *
 * Currently we set the palette on the ia32, amd64, ppc32 and sparc64 port.
 *
 * Note that the byte is being inverted by this function. The reason is
 * that we would like to use a color palette where the white color code
 * is 0 and the black color code is 255, as some machines (Sun Blade 1500)
 * use these codes for black and white and prevent to set codes
 * 0 and 255 to other colors.
 *
 */
static void bgr_323(void *dst, uint32_t rgb)
{
	*((uint8_t *) dst)
	    = ~((RED(rgb, 3) << 5) | (GREEN(rgb, 2) << 3) | BLUE(rgb, 3));
}

/** Hide logo and refresh screen
 *
 */
static void logo_hide(fb_instance_t *instance)
{
	instance->ylogo = 0;
	instance->ytrim = instance->yres;
	instance->rowtrim = instance->rows;
	
	if ((!instance->parea.mapped) || (console_override))
		fb_redraw_internal(instance);
}

/** Draw character at given position
 *
 */
static void glyph_draw(fb_instance_t *instance, uint16_t glyph,
    unsigned int col, unsigned int row, bool overlay)
{
	unsigned int x = COL2X(col);
	unsigned int y = ROW2Y(row);
	unsigned int yd;
	
	if (y >= instance->ytrim)
		logo_hide(instance);
	
	if (!overlay)
		instance->backbuf[BB_POS(instance, col, row)] = glyph;
	
	if ((!instance->parea.mapped) || (console_override)) {
		for (yd = 0; yd < FONT_SCANLINES; yd++)
			memcpy(&instance->addr[FB_POS(instance, x, y + yd + instance->ylogo)],
			    &instance->glyphs[GLYPH_POS(instance, glyph, yd)],
			    instance->glyphscanline);
	}
}

/** Scroll screen down by one row
 *
 *
 */
static void screen_scroll(fb_instance_t *instance)
{
	if (instance->ylogo > 0) {
		logo_hide(instance);
		return;
	}
	
	if ((!instance->parea.mapped) || (console_override)) {
		unsigned int row;
		
		for (row = 0; row < instance->rows; row++) {
			unsigned int y = ROW2Y(row);
			unsigned int yd;
			
			for (yd = 0; yd < FONT_SCANLINES; yd++) {
				unsigned int x;
				unsigned int col;
				
				for (col = 0, x = 0; col < instance->cols;
				    col++, x += FONT_WIDTH) {
					uint16_t glyph;
					
					if (row < instance->rows - 1) {
						if (instance->backbuf[BB_POS(instance, col, row)] ==
						    instance->backbuf[BB_POS(instance, col, row + 1)])
							continue;
						
						glyph = instance->backbuf[BB_POS(instance, col, row + 1)];
					} else
						glyph = 0;
					
					memcpy(&instance->addr[FB_POS(instance, x, y + yd)],
					    &instance->glyphs[GLYPH_POS(instance, glyph, yd)],
					    instance->glyphscanline);
				}
			}
		}
	}
	
	memmove(instance->backbuf, &instance->backbuf[BB_POS(instance, 0, 1)],
	    instance->cols * (instance->rows - 1) * sizeof(uint16_t));
	memsetw(&instance->backbuf[BB_POS(instance, 0, instance->rows - 1)],
	    instance->cols, 0);
}

static void cursor_put(fb_instance_t *instance)
{
	unsigned int col = instance->position % instance->cols;
	unsigned int row = instance->position / instance->cols;
	
	glyph_draw(instance, fb_font_glyph(U_CURSOR), col, row, true);
}

static void cursor_remove(fb_instance_t *instance)
{
	unsigned int col = instance->position % instance->cols;
	unsigned int row = instance->position / instance->cols;
	
	glyph_draw(instance, instance->backbuf[BB_POS(instance, col, row)],
	    col, row, true);
}

/** Render glyphs
 *
 * Convert glyphs from device independent font
 * description to current visual representation.
 *
 */
static void glyphs_render(fb_instance_t *instance)
{
	/* Prerender glyphs */
	uint16_t glyph;
	
	for (glyph = 0; glyph < FONT_GLYPHS; glyph++) {
		uint32_t fg_color;
		
		if (glyph == FONT_GLYPHS - 1)
			fg_color = INV_COLOR;
		else
			fg_color = FG_COLOR;
		
		unsigned int y;
		
		for (y = 0; y < FONT_SCANLINES; y++) {
			unsigned int x;
			
			for (x = 0; x < FONT_WIDTH; x++) {
				void *dst =
				    &instance->glyphs[GLYPH_POS(instance, glyph, y) +
				    x * instance->pixelbytes];
				uint32_t rgb = (fb_font[glyph][y] &
				    (1 << (7 - x))) ? fg_color : BG_COLOR;
				instance->rgb_conv(dst, rgb);
			}
		}
	}
	
	/* Prerender background scanline */
	unsigned int x;
	
	for (x = 0; x < instance->xres; x++)
		instance->rgb_conv(&instance->bgscan[x * instance->pixelbytes], BG_COLOR);
}

/** Print character to screen
 *
 * Emulate basic terminal commands.
 *
 */
static void fb_putchar(outdev_t *dev, wchar_t ch)
{
	fb_instance_t *instance = (fb_instance_t *) dev->data;
	spinlock_lock(&instance->lock);
	
	switch (ch) {
	case '\n':
		cursor_remove(instance);
		instance->position += instance->cols;
		instance->position -= instance->position % instance->cols;
		break;
	case '\r':
		cursor_remove(instance);
		instance->position -= instance->position % instance->cols;
		break;
	case '\b':
		cursor_remove(instance);
		if (instance->position % instance->cols)
			instance->position--;
		break;
	case '\t':
		cursor_remove(instance);
		do {
			glyph_draw(instance, fb_font_glyph(' '),
			    instance->position % instance->cols,
			    instance->position / instance->cols, false);
			instance->position++;
		} while ((instance->position % 8)
		    && (instance->position < instance->cols * instance->rows));
		break;
	default:
		glyph_draw(instance, fb_font_glyph(ch),
		    instance->position % instance->cols,
		    instance->position / instance->cols, false);
		instance->position++;
	}
	
	if (instance->position >= instance->cols * instance->rows) {
		instance->position -= instance->cols;
		screen_scroll(instance);
	}
	
	cursor_put(instance);
	
	spinlock_unlock(&instance->lock);
}

static void fb_redraw_internal(fb_instance_t *instance)
{
	if (instance->ylogo > 0) {
		unsigned int y;
		
		for (y = 0; y < LOGO_HEIGHT; y++) {
			unsigned int x;
			
			for (x = 0; x < instance->xres; x++)
				instance->rgb_conv(&instance->addr[FB_POS(instance, x, y)],
				    (x < LOGO_WIDTH) ?
				    fb_logo[y * LOGO_WIDTH + x] :
				    LOGO_COLOR);
		}
	}
	
	unsigned int row;
	
	for (row = 0; row < instance->rowtrim; row++) {
		unsigned int y = instance->ylogo + ROW2Y(row);
		unsigned int yd;
		
		for (yd = 0; yd < FONT_SCANLINES; yd++) {
			unsigned int x;
			unsigned int col;
			
			for (col = 0, x = 0; col < instance->cols;
			    col++, x += FONT_WIDTH) {
				uint16_t glyph =
				    instance->backbuf[BB_POS(instance, col, row)];
				void *dst = &instance->addr[FB_POS(instance, x, y + yd)];
				void *src = &instance->glyphs[GLYPH_POS(instance, glyph, yd)];
				memcpy(dst, src, instance->glyphscanline);
			}
		}
	}
	
	if (COL2X(instance->cols) < instance->xres) {
		unsigned int y;
		unsigned int size =
		    (instance->xres - COL2X(instance->cols)) * instance->pixelbytes;
		
		for (y = instance->ylogo; y < instance->yres; y++)
			memcpy(&instance->addr[FB_POS(instance, COL2X(instance->cols), y)],
			    instance->bgscan, size);
	}
	
	if (ROW2Y(instance->rowtrim) + instance->ylogo < instance->yres) {
		unsigned int y;
		
		for (y = ROW2Y(instance->rowtrim) + instance->ylogo;
		    y < instance->yres; y++)
			memcpy(&instance->addr[FB_POS(instance, 0, y)],
			    instance->bgscan, instance->bgscanbytes);
	}
}

/** Refresh the screen
 *
 */
static void fb_redraw(outdev_t *dev)
{
	fb_instance_t *instance = (fb_instance_t *) dev->data;
	
	spinlock_lock(&instance->lock);
	fb_redraw_internal(instance);
	spinlock_unlock(&instance->lock);
}

/** Initialize framebuffer as a output character device
 *
 */
outdev_t *fb_init(fb_properties_t *props)
{
	ASSERT(props);
	ASSERT(props->x > 0);
	ASSERT(props->y > 0);
	ASSERT(props->scan > 0);
	
	rgb_conv_t rgb_conv;
	unsigned int pixelbytes;
	
	switch (props->visual) {
	case VISUAL_INDIRECT_8:
		rgb_conv = bgr_323;
		pixelbytes = 1;
		break;
	case VISUAL_RGB_5_5_5_LE:
		rgb_conv = rgb_555_le;
		pixelbytes = 2;
		break;
	case VISUAL_RGB_5_5_5_BE:
		rgb_conv = rgb_555_be;
		pixelbytes = 2;
		break;
	case VISUAL_RGB_5_6_5_LE:
		rgb_conv = rgb_565_le;
		pixelbytes = 2;
		break;
	case VISUAL_RGB_5_6_5_BE:
		rgb_conv = rgb_565_be;
		pixelbytes = 2;
		break;
	case VISUAL_RGB_8_8_8:
		rgb_conv = rgb_888;
		pixelbytes = 3;
		break;
	case VISUAL_BGR_8_8_8:
		rgb_conv = bgr_888;
		pixelbytes = 3;
		break;
	case VISUAL_RGB_8_8_8_0:
		rgb_conv = rgb_8880;
		pixelbytes = 4;
		break;
	case VISUAL_RGB_0_8_8_8:
		rgb_conv = rgb_0888;
		pixelbytes = 4;
		break;
	case VISUAL_BGR_0_8_8_8:
		rgb_conv = bgr_0888;
		pixelbytes = 4;
		break;
	case VISUAL_BGR_8_8_8_0:
		rgb_conv = bgr_8880;
		pixelbytes = 4;
		break;
	default:
		LOG("Unsupported visual.");
		return NULL;
	}
	
	outdev_t *fbdev = malloc(sizeof(outdev_t), FRAME_ATOMIC);
	if (!fbdev)
		return NULL;
	
	fb_instance_t *instance = malloc(sizeof(fb_instance_t), FRAME_ATOMIC);
	if (!instance) {
		free(fbdev);
		return NULL;
	}
	
	outdev_initialize("fbdev", fbdev, &fbdev_ops);
	fbdev->data = instance;
	
	spinlock_initialize(&instance->lock, "*fb.instance.lock");
	
	instance->rgb_conv = rgb_conv;
	instance->pixelbytes = pixelbytes;
	instance->xres = props->x;
	instance->yres = props->y;
	instance->scanline = props->scan;
	instance->position = 0;
	
	instance->cols = X2COL(instance->xres);
	instance->rows = Y2ROW(instance->yres);
	
	if (instance->yres > LOGO_HEIGHT) {
		instance->ylogo = LOGO_HEIGHT;
		instance->rowtrim = instance->rows - Y2ROW(instance->ylogo);
		if (instance->ylogo % FONT_SCANLINES > 0)
			instance->rowtrim--;
		instance->ytrim = ROW2Y(instance->rowtrim);
	} else {
		instance->ylogo = 0;
		instance->ytrim = instance->yres;
		instance->rowtrim = instance->rows;
	}
	
	instance->glyphscanline = FONT_WIDTH * instance->pixelbytes;
	instance->glyphbytes = ROW2Y(instance->glyphscanline);
	instance->bgscanbytes = instance->xres * instance->pixelbytes;
	
	size_t fbsize = instance->scanline * instance->yres;
	size_t bbsize = instance->cols * instance->rows * sizeof(uint16_t);
	size_t glyphsize = FONT_GLYPHS * instance->glyphbytes;
	
	instance->addr = (uint8_t *) hw_map((uintptr_t) props->addr, fbsize);
	if (!instance->addr) {
		LOG("Unable to map framebuffer.");
		free(instance);
		free(fbdev);
		return NULL;
	}
	
	instance->backbuf = (uint16_t *) malloc(bbsize, 0);
	if (!instance->backbuf) {
		LOG("Unable to allocate backbuffer.");
		free(instance);
		free(fbdev);
		return NULL;
	}
	
	instance->glyphs = (uint8_t *) malloc(glyphsize, 0);
	if (!instance->glyphs) {
		LOG("Unable to allocate glyphs.");
		free(instance->backbuf);
		free(instance);
		free(fbdev);
		return NULL;
	}
	
	instance->bgscan = malloc(instance->bgscanbytes, 0);
	if (!instance->bgscan) {
		LOG("Unable to allocate background pixel.");
		free(instance->glyphs);
		free(instance->backbuf);
		free(instance);
		free(fbdev);
		return NULL;
	}
	
	memsetw(instance->backbuf, instance->cols * instance->rows, 0);
	glyphs_render(instance);
	
	link_initialize(&instance->parea.link);
	instance->parea.pbase = props->addr;
	instance->parea.frames = SIZE2FRAMES(fbsize);
	instance->parea.unpriv = false;
	instance->parea.mapped = false;
	ddi_parea_register(&instance->parea);
	
	if (!fb_exported) {
		/*
		 * We export the kernel framebuffer for uspace usage.
		 * This is used in the case the uspace framebuffer
		 * driver is not self-sufficient.
		 */
		sysinfo_set_item_val("fb", NULL, true);
		sysinfo_set_item_val("fb.kind", NULL, 1);
		sysinfo_set_item_val("fb.width", NULL, instance->xres);
		sysinfo_set_item_val("fb.height", NULL, instance->yres);
		sysinfo_set_item_val("fb.scanline", NULL, instance->scanline);
		sysinfo_set_item_val("fb.visual", NULL, props->visual);
		sysinfo_set_item_val("fb.address.physical", NULL, props->addr);
		
		fb_exported = true;
	}
	
	fb_redraw(fbdev);
	return fbdev;
}

/** @}
 */
