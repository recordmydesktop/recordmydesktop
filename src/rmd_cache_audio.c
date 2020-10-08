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
#include "rmd_cache_audio.h"

#include "rmd_jack.h"
#include "rmd_threads.h"
#include "rmd_types.h"

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>



void *rmdCacheAudioBuffer(ProgData *pdata)
{
	size_t	write_size = pdata->periodsize * pdata->sound_framesize;

#ifdef HAVE_LIBJACK
	void	*jackbuf = NULL;

	if (pdata->args.use_jack) {
		write_size = pdata->sound_framesize * pdata->jdata->buffersize;
		jackbuf = malloc(pdata->sound_framesize * pdata->jdata->buffersize);
	}
#endif

	rmdThreadsSetName("rmdCacheAudio");

//We are simply going to throw sound on the disk.
//It's sound is tiny compared to that of image, so
//compressing would reducethe overall size by only an
//insignificant fraction.

	while (pdata->running) {
		SndBuffer	*buff = NULL;

		pthread_mutex_lock(&pdata->pause_mutex);
		while (pdata->paused)
			pthread_cond_wait(&pdata->pause_cond, &pdata->pause_mutex);
		pthread_mutex_unlock(&pdata->pause_mutex);

		if (!pdata->args.use_jack) {
			pthread_mutex_lock(&pdata->sound_buffer_mutex);
			while (!pdata->sound_buffer && pdata->running)
				pthread_cond_wait(&pdata->sound_data_read, &pdata->sound_buffer_mutex);

			buff = pdata->sound_buffer;
			if (buff)
				pdata->sound_buffer = buff->next;
			pthread_mutex_unlock(&pdata->sound_buffer_mutex);

			if (!pdata->running)
				break;

			fwrite(buff->data, 1, write_size, pdata->cache_data->afp);
			free(buff->data);
			free(buff);
		} else {
#ifdef HAVE_LIBJACK
			pthread_mutex_lock(&pdata->sound_buffer_mutex);
			while (	pdata->running &&
				jack_ringbuffer_read_space(pdata->jdata->sound_buffer) < write_size)
				pthread_cond_wait(&pdata->sound_data_read, &pdata->sound_buffer_mutex);

			if (pdata->running)
				jack_ringbuffer_read(pdata->jdata->sound_buffer, jackbuf, write_size);
			pthread_mutex_unlock(&pdata->sound_buffer_mutex);

			if (!pdata->running)
				break;

			fwrite(jackbuf, 1, write_size, pdata->cache_data->afp);
#endif
		}
	}

	fclose(pdata->cache_data->afp);
	pthread_exit(&errno);
}
