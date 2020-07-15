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
#include "rmd_timer.h"

#include "rmd_threads.h"
#include "rmd_types.h"

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>


static struct timespec us_to_timespec(unsigned int us)
{
	return (struct timespec){
		.tv_sec = us / 1000000,
		.tv_nsec = (us % 1000000) * 1000,
	};
}

static void sync_streams(ProgData *pdata, unsigned int *frame_step, struct timespec *delay) {
	int	avd;

	pthread_mutex_lock(&pdata->avd_mutex);
	avd = pdata->avd;
	pthread_mutex_unlock(&pdata->avd_mutex);

	/* There are two knobs available for keeping the video synchronized with the audio:
	 * 1. frame_step; how many frames to encode from this frame (aka dropping frames if > 1)
	 * 2. delay; how long to delay the next get_frame
	 *
	 * When avd is negative, we need more video relative to audio.  That can be achieved
	 * by either sleeping less between frames, or dropping them by having the encoder
	 * encode a given frame multiple times.
	 *
	 * When avd is positive, we need more audio relative to video, so less video.  This
	 * can be achieved by sleeping more between frames.
	 */

	if (avd < 0) {
		int	frames_behind = -avd / pdata->frametime;

		if (frames_behind > 0) {
			/* more than a whole frame behind, drop frames to catch up */
			*frame_step += frames_behind;
		} else {
			/* less than a whole frame behind, just sleep less */
			*delay = us_to_timespec(pdata->frametime + avd);
		}

	} else if (avd > 0) {
		/* sleep longer */
		*delay = us_to_timespec(pdata->frametime + avd);
	}

#if 0
	printf("avd: %i frame_step: %u delay: %lu,%lu\n",
		avd, *frame_step, (*delay).tv_sec, (*delay).tv_nsec);
#endif
}

void *rmdTimer(ProgData *pdata) {

	rmdThreadsSetName("rmdTimer");

	while (pdata->timer_alive) {
		struct timespec	delay;
		unsigned int	frame_step = 1;

		delay.tv_sec = 1.f / pdata->args.fps;
		delay.tv_nsec = 1000000000.f / pdata->args.fps - delay.tv_sec * 1000000000.f;

		pthread_mutex_lock(&pdata->pause_mutex);
		if (pdata->pause_state_changed) {
			pdata->pause_state_changed = FALSE;

			if (!pdata->paused) {
				pdata->paused = TRUE;
				fprintf(stderr,"STATE:PAUSED\n");fflush(stderr);
			} else{
				pdata->paused = FALSE;
				fprintf(stderr,"STATE:RECORDING\n");fflush(stderr);
				pthread_cond_broadcast(&pdata->pause_cond);
			}
		}

		pthread_mutex_unlock(&pdata->pause_mutex);

		if (!pdata->args.nosound)
			sync_streams(pdata, &frame_step, &delay);

		pthread_mutex_lock(&pdata->time_mutex);
		pdata->time_frameno += frame_step;
		pthread_mutex_unlock(&pdata->time_mutex);
		pthread_cond_signal(&pdata->time_cond);

		nanosleep(&delay, NULL);
	}

	pthread_exit(&errno);
}
