/*
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

/** @addtogroup hdaudio
 * @{
 */
/** @file High Definition Audio PCM interface
 */

#include <async.h>
#include <audio_pcm_iface.h>
#include <ddf/log.h>
#include <errno.h>
#include <pcm/sample_format.h>
#include <stdbool.h>

#include "codec.h"
#include "hdactl.h"
#include "hdaudio.h"
#include "spec/fmt.h"
#include "stream.h"

static int hda_get_info_str(ddf_fun_t *, const char **);
static unsigned hda_query_cap(ddf_fun_t *, audio_cap_t);
static int hda_test_format(ddf_fun_t *, unsigned *, unsigned *,
    pcm_sample_format_t *);
static int hda_get_buffer(ddf_fun_t *, void **, size_t *);
static int hda_get_buffer_position(ddf_fun_t *, size_t *);
static int hda_set_event_session(ddf_fun_t *, async_sess_t *);
static async_sess_t *hda_get_event_session(ddf_fun_t *);
static int hda_release_buffer(ddf_fun_t *);
static int hda_start_playback(ddf_fun_t *, unsigned, unsigned, unsigned,
    pcm_sample_format_t);
static int hda_stop_playback(ddf_fun_t *, bool);
static int hda_start_capture(ddf_fun_t *, unsigned, unsigned, unsigned,
    pcm_sample_format_t);
static int hda_stop_capture(ddf_fun_t *, bool);

audio_pcm_iface_t hda_pcm_iface = {
	.get_info_str = hda_get_info_str,
	.test_format = hda_test_format,
	.query_cap = hda_query_cap,

	.get_buffer = hda_get_buffer,
	.release_buffer = hda_release_buffer,
	.set_event_session = hda_set_event_session,
	.get_event_session = hda_get_event_session,
	.get_buffer_pos = hda_get_buffer_position,

	.start_playback = hda_start_playback,
	.stop_playback = hda_stop_playback,

	.start_capture = hda_start_capture,
	.stop_capture = hda_stop_capture
};

enum {
	max_buffer_size = 65536 /* XXX this is completely arbitrary */
};

static hda_t *fun_to_hda(ddf_fun_t *fun)
{
	return (hda_t *)ddf_dev_data_get(ddf_fun_get_dev(fun));
}

static int hda_get_info_str(ddf_fun_t *fun, const char **name)
{
	ddf_msg(LVL_NOTE, "hda_get_info_str()");
	if (name)
		*name = "High Definition Audio";
	return EOK;
}

static unsigned hda_query_cap(ddf_fun_t *fun, audio_cap_t cap)
{
	ddf_msg(LVL_NOTE, "hda_query_cap(%d)", cap);
	switch (cap) {
	case AUDIO_CAP_PLAYBACK:
	case AUDIO_CAP_INTERRUPT:
		return 1;
	case AUDIO_CAP_BUFFER_POS:
	case AUDIO_CAP_CAPTURE:
		return 0;
	case AUDIO_CAP_MAX_BUFFER:
		return max_buffer_size;
	case AUDIO_CAP_INTERRUPT_MIN_FRAMES:
		return 128;
	case AUDIO_CAP_INTERRUPT_MAX_FRAMES:
		return 16384;
	default:
		return ENOTSUP;
	}
}

static int hda_test_format(ddf_fun_t *fun, unsigned *channels,
    unsigned *rate, pcm_sample_format_t *format)
{
	int rc = EOK;

	ddf_msg(LVL_NOTE, "hda_test_format(%u, %u, %d)\n",
	    *channels, *rate, *format);

	if (*channels != 1) {
		*channels = 1;
		rc = ELIMIT;
	}

	if (*format != PCM_SAMPLE_SINT16_LE) {
		*format = PCM_SAMPLE_SINT16_LE;
		rc = ELIMIT;
	}

	if (*rate != 48000) {
		*rate = 48000;
		rc = ELIMIT;
	}

	return rc;
}

static int hda_get_buffer(ddf_fun_t *fun, void **buffer, size_t *size)
{
	hda_t *hda = fun_to_hda(fun);

	ddf_msg(LVL_NOTE, "hda_get_buffer(): hda=%p", hda);
	if (hda->pcm_stream != NULL)
		return EBUSY;

	/* XXX Choose appropriate parameters */
	uint32_t fmt;
	/* 48 kHz, 16-bits, 1 channel */
	fmt = fmt_bits_16 << fmt_bits_l;

	ddf_msg(LVL_NOTE, "hda_get_buffer() - create stream");
	hda->pcm_stream = hda_stream_create(hda, sdir_output, fmt);
	if (hda->pcm_stream == NULL)
		return EIO;

	ddf_msg(LVL_NOTE, "hda_get_buffer() - fill info");
	/* XXX This is only one buffer */
	*buffer = hda->pcm_stream->buf[0];
	*size = hda->pcm_stream->bufsize;

	ddf_msg(LVL_NOTE, "hda_get_buffer() retturing EOK, buffer=%p, size=%zu",
	    *buffer, *size);
	return EOK;
}

static int hda_get_buffer_position(ddf_fun_t *fun, size_t *pos)
{
	ddf_msg(LVL_NOTE, "hda_get_buffer_position()");
	return ENOTSUP;
}

static int hda_set_event_session(ddf_fun_t *fun, async_sess_t *sess)
{
	ddf_msg(LVL_NOTE, "hda_set_event_session()");
	return ENOTSUP;
}

static async_sess_t *hda_get_event_session(ddf_fun_t *fun)
{
	ddf_msg(LVL_NOTE, "hda_get_event_session()");
	return NULL;
}

static int hda_release_buffer(ddf_fun_t *fun)
{
	hda_t *hda = fun_to_hda(fun);

	ddf_msg(LVL_NOTE, "hda_release_buffer()");
	if (hda->pcm_stream == NULL)
		return EINVAL;

	hda_stream_destroy(hda->pcm_stream);
	hda->pcm_stream = NULL;
	return EOK;
}

static int hda_start_playback(ddf_fun_t *fun, unsigned frames,
    unsigned channels, unsigned rate, pcm_sample_format_t format)
{
	hda_t *hda = fun_to_hda(fun);
	int rc;

	ddf_msg(LVL_NOTE, "hda_start_playback()");

	rc = hda_out_converter_setup(hda->ctl->codec, hda->pcm_stream->sid);
	if (rc != EOK)
		return rc;

	hda_stream_start(hda->pcm_stream);
	return EOK;
}

static int hda_stop_playback(ddf_fun_t *fun, bool immediate)
{
	ddf_msg(LVL_NOTE, "hda_stop_playback()");
	return ENOTSUP;
}

static int hda_start_capture(ddf_fun_t *fun, unsigned frames, unsigned channels,
    unsigned rate, pcm_sample_format_t format)
{
	ddf_msg(LVL_NOTE, "hda_start_capture()");
	return ENOTSUP;
}

static int hda_stop_capture(ddf_fun_t *fun, bool immediate)
{
	ddf_msg(LVL_NOTE, "hda_stop_capture()");
	return ENOTSUP;
}

/** @}
 */
