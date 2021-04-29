/******************************************************************************
*                            recordMyDesktop                                  *
*******************************************************************************
*                                                                             *
*            Copyright (C) 2006,2007,2008 John Varouhakis                     *
*                                                                             *
*                                                                             *
*   This program is free software; you can redistribute it and/or modify      *
*   it under the terms of the GNU General Public License as published by      *
*   the Free Software Foundation; either version 2 of the License, or         *
*   (at your option) any later version.                                       *
*                                                                             *
*   This program is distributed in the hope that it will be useful,           *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
*   GNU General Public License for more details.                              *
*                                                                             *
*   You should have received a copy of the GNU General Public License         *
*   along with this program; if not, write to the Free Software               *
*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA  *
*                                                                             *
*                                                                             *
*                                                                             *
*   For further information contact me at johnvarouhakis@gmail.com            *
******************************************************************************/

#include "config.h"
#include "rmd_threads.h"

#include "rmd_cache_audio.h"
#include "rmd_cache_frame.h"
#include "rmd_capture_audio.h"
#include "rmd_encode_audio_buffers.h"
#include "rmd_encode_image_buffers.h"
#include "rmd_flush_to_ogg.h"
#include "rmd_get_frames.h"
#include "rmd_jack.h"
#include "rmd_register_callbacks.h"
#include "rmd_types.h"

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>


int rmdThread(pthread_t *thread, void *(*func)(ProgData *), ProgData *pdata)
{
	if (pthread_create(thread, NULL, (void *)func, (void *)pdata) != 0)
		return 1;

	return 0;
}

int rmdThreads(ProgData *pdata)
{
	pthread_t	image_capture_t,
			image_encode_t,
			image_cache_t,
			sound_capture_t,
			sound_encode_t,
			sound_cache_t,
			flush_to_ogg_t;

	if (pdata->args.delay > 0) {
		fprintf(stderr, "Will sleep for %d seconds now.\n", pdata->args.delay);
		nanosleep(&(struct timespec){ .tv_sec = pdata->args.delay }, NULL);
	}

	/*start threads*/
	if (rmdThread(&image_capture_t, rmdGetFrames, pdata)) {
		fprintf(stderr, "Error creating rmdGetFrames thread!!!\n");
		return 1;
	}

	if (pdata->args.encOnTheFly) {
		if (rmdThread(&image_encode_t, rmdEncodeImageBuffers, pdata)) {
			fprintf(stderr, "Error creating rmdEncodeImageBuffers thread!!!\n");
			return 1;
		}
	} else {
		if (rmdThread(&image_cache_t, rmdCacheImageBuffer, pdata)) {
			fprintf(stderr, "Error creating rmdCacheImageBuffer thread!!!\n");
			return 1;
		}
	}

	if (!pdata->args.nosound) {
		if (!pdata->args.use_jack) {
			if (rmdThread(&sound_capture_t, rmdCaptureAudio, pdata)) {
				fprintf(stderr, "Error creating rmdCaptureAudio thread!!!\n");
				return 1;
			}
		}

		if (pdata->args.encOnTheFly) {
			if (rmdThread(&sound_encode_t, rmdEncodeAudioBuffers, pdata)) {
				fprintf(stderr, "Error creating rmdEncodeAudioBuffers thread!!!\n");
				return 1;
			}
		} else {
			if (rmdThread(&sound_cache_t, rmdCacheAudioBuffer, pdata)) {
				fprintf(stderr, "Error creating rmdCacheAudioBuffer thread!!!\n");
				return 1;
			}
		}
	}

	if (pdata->args.encOnTheFly) {
		if (rmdThread(&flush_to_ogg_t, rmdFlushToOgg, pdata)) {
			fprintf(stderr, "Error creating rmdFlushToOgg thread!!!\n");
			return 1;
		}
	}

	rmdRegisterCallbacks(pdata);
	fprintf(stderr,"Capturing!\n");

#ifdef HAVE_LIBJACK
	if (pdata->args.use_jack)
		pdata->jdata->capture_started = 1;
#endif
	//wait all threads to finish

	pthread_join(image_capture_t, NULL);
	fprintf(stderr,"Shutting down.");
	//if no damage events have been received the thread will get stuck
	pthread_mutex_lock(&pdata->theora_lib_mutex);
	while (!pdata->th_encoding_clean) {
		puts("waiting for th_enc");
		pthread_cond_signal(&pdata->image_buffer_ready);
		pthread_mutex_unlock(&pdata->theora_lib_mutex);
		nanosleep(&(struct timespec){ .tv_nsec = 10000000 }, NULL);
		pthread_mutex_lock(&pdata->theora_lib_mutex);
	}
	pthread_mutex_unlock(&pdata->theora_lib_mutex);

	if (pdata->args.encOnTheFly)
		pthread_join(image_encode_t, NULL);
	else
		pthread_join(image_cache_t, NULL);
	fprintf(stderr,".");

	if (!pdata->args.nosound) {
#ifdef HAVE_LIBJACK
		if (pdata->args.use_jack)
			rmdStopJackClient(pdata->jdata);
#endif
		if (!pdata->args.use_jack)
			pthread_join(sound_capture_t,NULL);

		fprintf(stderr,".");
		pthread_mutex_lock(&pdata->vorbis_lib_mutex);
		while (!pdata->v_encoding_clean) {
			puts("waiting for v_enc");
			pthread_cond_signal(&pdata->sound_data_read);
			pthread_mutex_unlock(&pdata->vorbis_lib_mutex);
			nanosleep(&(struct timespec){ .tv_nsec = 10000000 }, NULL);
			pthread_mutex_lock(&pdata->vorbis_lib_mutex);
		}
		pthread_mutex_unlock(&pdata->vorbis_lib_mutex);

		if (pdata->args.encOnTheFly)
			pthread_join(sound_encode_t, NULL);
		else
			pthread_join(sound_cache_t, NULL);
	} else
		fprintf(stderr,"..");

	fprintf(stderr,".");

	return 0;
}

void rmdThreadsSetName(const char *name)
{
	prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
}
