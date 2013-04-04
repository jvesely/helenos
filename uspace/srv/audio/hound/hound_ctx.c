/*
 * Copyright (c) 2013 Jan Vesely
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
 * @addtogroup audio
 * @brief HelenOS sound server.
 * @{
 */
/** @file
 */

#include <malloc.h>

#include "hound_ctx.h"
#include "log.h"

hound_ctx_t *hound_record_ctx_get(const char *name)
{
	return NULL;
}

hound_ctx_t *hound_playback_ctx_get(const char *name)
{
	hound_ctx_t *ctx = malloc(sizeof(hound_ctx_t));
	if (ctx) {
		link_initialize(&ctx->link);
		list_initialize(&ctx->streams);
		ctx->sink = NULL;
		ctx->source = malloc(sizeof(audio_source_t));
		if (!ctx->source) {
			free(ctx);
			return NULL;
		}
		const int ret = audio_source_init(ctx->source, name, ctx, NULL,
		    NULL, &AUDIO_FORMAT_ANY);
		if (ret != EOK) {
			free(ctx->source);
			free(ctx);
			return NULL;
		}
	}
	return ctx;
}

void hound_ctx_destroy(hound_ctx_t *ctx)
{
	assert(ctx);
	assert(!link_in_use(&ctx->link));
	if (ctx->source)
		audio_source_fini(ctx->source);
	if (ctx->sink)
		audio_sink_fini(ctx->sink);
	//TODO remove streams
	free(ctx->source);
	free(ctx->sink);
	free(ctx);
}

hound_context_id_t hound_ctx_get_id(hound_ctx_t *ctx)
{
	assert(ctx);
	return (hound_context_id_t)ctx;
}

bool hound_ctx_is_record(hound_ctx_t *ctx)
{
	assert(ctx);
	return ctx->source == NULL;
}

/*
 * STREAMS
 */

typedef struct {
	const void *data;
	size_t size;
	link_t link;
} audio_data_t;

static inline audio_data_t * audio_data_list_instance(link_t *l)
{
	return l ? list_get_instance(l, audio_data_t, link) : NULL;
}

hound_ctx_stream_t *hound_ctx_create_stream(hound_ctx_t *ctx, int flags,
	pcm_format_t format, size_t buffer_size)
{
	assert(ctx);
	hound_ctx_stream_t *stream = malloc(sizeof(hound_ctx_stream_t));
	if (stream) {
		list_initialize(&stream->fifo);
		link_initialize(&stream->link);
		stream->ctx = ctx;
		stream->flags = flags;
		stream->format = format;
		stream->allowed_size = buffer_size;
		stream->current_size = 0;
		list_append(&stream->link, &ctx->streams);
		log_verbose("CTX: %p added stream; flags:%#x ch: %u r:%u f:%s",
		    ctx, flags, format.channels, format.sampling_rate,
		    pcm_sample_format_str(format.sample_format));
	}
	return stream;
}

void hound_ctx_destroy_stream(hound_ctx_stream_t *stream)
{
	if (stream) {
		//TODO consider DRAIN FLAG
		list_remove(&stream->link);
		if (!list_empty(&stream->fifo))
			log_warning("Destroying stream with non empty buffer");
		while (!list_empty(&stream->fifo)) {
			link_t *l = list_first(&stream->fifo);
			audio_data_t *data = audio_data_list_instance(l);
			list_remove(l);
			free(data->data);
			free(data);
		}
		log_verbose("CTX: %p remove stream (%zu/%zu); "
		    "flags:%#x ch: %u r:%u f:%s",
		    stream->ctx, stream->current_size, stream->allowed_size,
		    stream->flags, stream->format.channels,
		    stream->format.sampling_rate,
		    pcm_sample_format_str(stream->format.sample_format));
		free(stream);
	}
}


int hound_ctx_stream_write(hound_ctx_stream_t *stream, const void *data,
    size_t size)
{
	assert(stream);
        log_verbose("%p:, %zu", stream, size);

	if (stream->allowed_size && size > stream->allowed_size)
		return EINVAL;

	if (stream->allowed_size &&
	    (stream->current_size + size > stream->allowed_size))
		return EBUSY;

	audio_data_t *adata = malloc(sizeof(audio_data_t));
	if (adata) {
		adata->data = data;
		adata->size = size;
		link_initialize(&adata->link);
		list_append(&adata->link, &stream->fifo);
		stream->current_size += size;
		return EOK;
	}
	log_warning("Failed to enqueue %zu bytes of data.", size);
	return ENOMEM;
}

int hound_ctx_stream_read(hound_ctx_stream_t *stream, void *data, size_t size)
{
        log_verbose("%p:, %zu", stream, size);
	return ENOTSUP;
}

/**
 * @}
 */
