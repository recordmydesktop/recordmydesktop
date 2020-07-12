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
#include "rmd_flush_to_ogg.h"

#include "rmd_encode_image_buffer.h"
#include "rmd_encode_sound_buffer.h"
#include "rmd_threads.h"
#include "rmd_types.h"

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>


//we copy the page because the next call to ogg_stream_pageout
//will invalidate it. But we must have pages from
//both streams at every time in
//order to do correct multiplexing
static void page_copy(ogg_page *new, ogg_page *old) {

	new->header_len = old->header_len;
	new->header = malloc(new->header_len);
	new->body_len = old->body_len;
	new->body = malloc(new->body_len);

	memcpy(new->header, old->header, new->header_len);
	memcpy(new->body, old->body, new->body_len);
}

//free our copy
static void page_free(ogg_page *pg) {
	pg->header_len = pg->body_len = 0;
	free(pg->header);
	free(pg->body);
}

void *rmdFlushToOgg(ProgData *pdata) {
	int		videoflag = 0, audioflag = 0;
	double		video_bytesout = 0, audio_bytesout = 0;
	ogg_page	videopage,	//owned by libogg
			videopage_copy,	//owned by the application
			audiopage,	//owned by libogg
			audiopage_copy;	//owned by the application

	double		audiotime = 0;
	double		videotime = 0;
	int		th_st_fin = 0,
			v_st_fin = (pdata->args.nosound);

	rmdThreadsSetName("rmdFlushToOgg");

	while (!(th_st_fin && v_st_fin)) {
		int audio_or_video = 0;

		if (pdata->running) {
			pthread_mutex_lock(&pdata->libogg_mutex);
			if (!videoflag) {
				videoflag = ogg_stream_pageout(&pdata->enc_data->m_ogg_ts, &videopage);
				videotime = videoflag ? theora_granule_time(
								&pdata->enc_data->m_th_st,
								ogg_page_granulepos(&videopage)
							) : -1;

				if (videoflag)
					page_copy(&videopage_copy, &videopage);
			}

			if (!pdata->args.nosound && !audioflag) {
				audioflag = ogg_stream_pageout(&pdata->enc_data->m_ogg_vs, &audiopage);
				audiotime = audioflag ? vorbis_granule_time(
								&pdata->enc_data->m_vo_dsp,
								ogg_page_granulepos(&audiopage)
							) : -1;

				if (audioflag)
					page_copy(&audiopage_copy, &audiopage);
			}
			pthread_mutex_unlock(&pdata->libogg_mutex);
		} else {
			if (!th_st_fin && !videoflag) {
				pthread_mutex_lock(&pdata->libogg_mutex);
				videoflag = ogg_stream_flush(&pdata->enc_data->m_ogg_ts, &videopage);
				videotime = videoflag ? theora_granule_time(
								&pdata->enc_data->m_th_st,
								ogg_page_granulepos(&videopage)
							) : -1;

				if (videoflag)
					page_copy(&videopage_copy, &videopage);
				pthread_mutex_unlock(&pdata->libogg_mutex);

				//we need the last page to properly close the stream
				if (!videoflag) {
					pthread_mutex_lock(&pdata->theora_lib_mutex);
					while (!pdata->th_encoding_clean)
						pthread_cond_wait(&pdata->theora_lib_clean, &pdata->theora_lib_mutex);
					pthread_mutex_unlock(&pdata->theora_lib_mutex);
					rmdSyncEncodeImageBuffer(pdata);
				}
			}

			if (!pdata->args.nosound && !v_st_fin && !audioflag) {
				pthread_mutex_lock(&pdata->libogg_mutex);
				audioflag = ogg_stream_flush(&pdata->enc_data->m_ogg_vs, &audiopage);
				audiotime = audioflag ? vorbis_granule_time(
								&pdata->enc_data->m_vo_dsp,
								ogg_page_granulepos(&audiopage)
							) : -1;

				if (audioflag)
					page_copy(&audiopage_copy, &audiopage);
				pthread_mutex_unlock(&pdata->libogg_mutex);

				//we need the last page to properly close the stream
				if (!audioflag) {
					pthread_mutex_lock(&pdata->vorbis_lib_mutex);
					while (!pdata->v_encoding_clean)
						pthread_cond_wait(&pdata->vorbis_lib_clean, &pdata->vorbis_lib_mutex);
					pthread_mutex_unlock(&pdata->vorbis_lib_mutex);
					rmdSyncEncodeSoundBuffer(pdata, NULL);
				}
			}
		}

#if 0
		/* I don't understand what this is about, if these are finished we want to lose
		 * their pages?
		 */
		if (th_st_fin)
			videoflag=0;

		if (v_st_fin)
			audioflag=0;
#endif
		if ((!audioflag && !v_st_fin && !pdata->args.nosound) || (!videoflag && !th_st_fin)) {
			nanosleep(&(struct timespec){ .tv_nsec = 10000000 }, NULL);
			continue;
		}

		if (!audioflag) {
			audio_or_video = 1;
		} else if (!videoflag) {
			audio_or_video = 0;
		} else {
			if (audiotime < videotime)
				audio_or_video = 0;
			else
				audio_or_video = 1;
		}

		if (audio_or_video == 1) {
			video_bytesout += fwrite(	videopage_copy.header, 1,
							videopage_copy.header_len,
							pdata->enc_data->fp);

			video_bytesout += fwrite(	videopage_copy.body, 1,
							videopage_copy.body_len,
							pdata->enc_data->fp);
			videoflag = 0;

			if (!pdata->running) {
				pthread_mutex_lock(&pdata->libogg_mutex);
				if (ogg_page_eos(&videopage_copy))
					th_st_fin = 1;
				pthread_mutex_unlock(&pdata->libogg_mutex);
			}

			page_free(&videopage_copy);
		} else {
			audio_bytesout += fwrite(	audiopage_copy.header, 1,
							audiopage_copy.header_len,
							pdata->enc_data->fp);

			audio_bytesout += fwrite(	audiopage_copy.body, 1,
							audiopage_copy.body_len,
							pdata->enc_data->fp);
			audioflag = 0;

			if (!pdata->running) {
				pthread_mutex_lock(&pdata->libogg_mutex);
				if (ogg_page_eos(&audiopage_copy))
					v_st_fin = 1;
				pthread_mutex_unlock(&pdata->libogg_mutex);
			}

			page_free(&audiopage_copy);
		}
	}

	pthread_mutex_lock(&pdata->libogg_mutex);
	ogg_stream_clear(&pdata->enc_data->m_ogg_ts);

	if (!pdata->args.nosound)
		ogg_stream_clear(&pdata->enc_data->m_ogg_vs);

	pthread_mutex_unlock(&pdata->libogg_mutex);

	theora_clear(&pdata->enc_data->m_th_st);

	if (pdata->enc_data->fp)
		fclose(pdata->enc_data->fp);

	fprintf(stderr,	"\r   \nDone.\nWritten %.0f bytes\n"
			"(%.0f of which were video data and %.0f audio data)\n\n",
			video_bytesout + audio_bytesout,
			video_bytesout, audio_bytesout);

	pthread_exit(&errno);
}
