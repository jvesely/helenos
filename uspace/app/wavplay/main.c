/*
 * Copyright (c) 2012 Jan Vesely
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

/** @addtogroup wavplay
 * @{
 */
/**
 * @file PCM playback audio devices
 */

#include <assert.h>
#include <errno.h>
#include <fibril_synch.h>
#include <str_error.h>
#include <stdio.h>
#include <hound/client.h>
#include <pcm/sample_format.h>
#include <getopt.h>

#include "dplay.h"
#include "wave.h"

#define NAME_MAX 32
char name[NAME_MAX + 1];

typedef struct {
	FILE* source;
	volatile bool playing;
	fibril_mutex_t mutex;
	fibril_condvar_t cv;
	hound_sess_t *server;
} playback_t;

static void playback_initialize(playback_t *pb, hound_sess_t *sess)
{
	assert(pb);
	pb->playing = false;
	pb->source = NULL;
	pb->server = sess;
	fibril_mutex_initialize(&pb->mutex);
	fibril_condvar_initialize(&pb->cv);
}

static void data_callback(void* arg, void *buffer, ssize_t size)
{
	playback_t *pb = arg;
	assert(pb);

	if (size > 0) {
		const ssize_t bytes =
		    fread(buffer, sizeof(uint8_t), size, pb->source);
		printf("%zu bytes ready\n", bytes);
		if (bytes < size) {
			printf(" requested: %zd ready: %zd zero: %zd\n",
				size, bytes, size - bytes);
			bzero(buffer + bytes, size - bytes);
		}
		if (bytes == 0) {
			pb->playing = false;
			printf("The end, nothing more to play.\n");
			fibril_condvar_signal(&pb->cv);
		}
	} else {
		printf("Got error %s.\n", str_error(size));
		pb->playing = false;
		fibril_condvar_signal(&pb->cv);
	}
}

static void play(playback_t *pb, unsigned channels, unsigned rate, pcm_sample_format_t format)
{
	assert(pb);
	/* Create playback client */
	int ret = hound_register_playback(pb->server, name, channels, rate,
	    format, data_callback, pb);
	if (ret != EOK) {
		printf("Failed to register playback: %s\n", str_error(ret));
		return;
	}

	/* Connect */
	ret = hound_create_connection(pb->server, name, DEFAULT_SINK);
	if (ret == EOK) {
		fibril_mutex_lock(&pb->mutex);
		for (pb->playing = true; pb->playing;
		    fibril_condvar_wait(&pb->cv, &pb->mutex));
		fibril_mutex_unlock(&pb->mutex);

		hound_destroy_connection(pb->server, name, DEFAULT_SINK);
	} else
		printf("Failed to connect: %s\n", str_error(ret));

	printf("Unregistering playback\n");
	hound_unregister_playback(pb->server, name);
}

static const struct option opts[] = {
	{"device", required_argument, 0, 'd'},
	{"record", no_argument, 0, 'r'},
	{"help", no_argument, 0, 'h'},
	{0, 0, 0, 0}
};

static void print_help(const char* name)
{
	printf("Usage: %s [options] file\n", name);
	printf("supported options:\n");
	printf("\t -h, --help\t Print this help.\n");
	printf("\t -r, --record\t Start recording instead of playback.\n");
	printf("\t -d, --device\t Use specified device instead of sound "
	    "service. Use location path or special device `default'\n");
}

int main(int argc, char *argv[])
{
	const char *device = "default";
	int idx = 0;
	bool direct = false, record = false;
	optind = 0;
	int ret = 0;
	while (ret != -1) {
		ret = getopt_long(argc, argv, "d:rh", opts, &idx);
		switch (ret) {
		case 'd':
			direct = true;
			device = optarg;
			break;
		case 'r':
			record = true;
			break;
		case 'h':
			print_help(*argv);
			return 0;
		};
	}

	if (optind == argc) {
		printf("Not enough arguments.\n");
		print_help(*argv);
		return 1;
	}
	const char *file = argv[optind];

	printf("%s %s\n", record ? "Recording" : "Playing", file);
	if (record) {
		printf("Recording is not supported yet.\n");
		return 1;
	}
	if (direct)
		return dplay(device, file);

	task_id_t tid = task_get_id();
	snprintf(name, NAME_MAX, "%s%" PRIu64 ":%s", argv[0], tid, file);

	printf("Client name: %s\n", name);

	hound_sess_t *sess = hound_get_session();
	if (!sess) {
		printf("Failed to connect to hound service\n");
		return 1;
	}

	playback_t pb;
	playback_initialize(&pb, sess);
	pb.source = fopen(file, "rb");
	if (pb.source == NULL) {
		printf("Failed to open %s.\n", file);
		hound_release_session(sess);
		return 1;
	}
	wave_header_t header;
	fread(&header, sizeof(header), 1, pb.source);
	unsigned rate, channels;
	pcm_sample_format_t format;
	const char *error;
	ret = wav_parse_header(&header, NULL, NULL, &channels, &rate,
	    &format, &error);
	if (ret != EOK) {
		printf("Error parsing wav header: %s.\n", error);
		fclose(pb.source);
		hound_release_session(sess);
		return 1;
	}

	play(&pb, channels, rate, format);

	printf("Releasing session\n");
	hound_release_session(sess);
	return 0;
}
/**
 * @}
 */
