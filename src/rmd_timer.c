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


void *rmdTimer(ProgData *pdata){
	struct timespec	delay;

	rmdThreadsSetName("rmdTimer");

	delay.tv_sec = 1.f / pdata->args.fps;
	delay.tv_nsec = 1000000000.f / pdata->args.fps - delay.tv_sec * 1000000000.f;

	while (pdata->timer_alive) {

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

		if (!pdata->paused) {
			pthread_mutex_unlock(&pdata->pause_mutex);

			/* FIXME TODO: detect dropped frames by delta between {time,capture}_frameno */
			pdata->frames_total++;
		} else
			pthread_mutex_unlock(&pdata->pause_mutex);

		pthread_mutex_lock(&pdata->time_mutex);
		pdata->time_frameno++;
		pthread_mutex_unlock(&pdata->time_mutex);
		pthread_cond_signal(&pdata->time_cond);

		nanosleep(&delay, NULL);
	}

	pthread_exit(&errno);
}
