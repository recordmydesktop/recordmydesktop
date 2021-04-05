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
#include "rmd_encode_audio_buffers.h"

#include "rmd_jack.h"
#include "rmd_threads.h"
#include "rmd_types.h"

#include <pthread.h>

#include <stdlib.h>
#include <errno.h>


static void rmdEncodeAudioBuffer(ProgData *pdata, void *buff)
{
	int	sampread = (buff != NULL) ? pdata->periodsize : 0;
	float	**vorbis_buffer = vorbis_analysis_buffer(&pdata->enc_data->m_vo_dsp, sampread);

	if (!pdata->args.use_jack) {
		for (int i = 0, count = 0; i < sampread; i++) {
			for (int j = 0; j < pdata->args.channels; j++) {
				vorbis_buffer[j][i] =	((((signed char *)buff)[count + 1] << 8) |
							(0x00ff & (int)((signed char *)buff)[count]))
							/ 32768.f;
				count += 2;
			}
		}
	} else {
		for (int j = 0, count = 0; j < pdata->args.channels; j++) {
			for (int i = 0; i < sampread; i++) {
				vorbis_buffer[j][i] = ((float *)buff)[count];
				count++;
			}
		}
	}

	vorbis_analysis_wrote(&pdata->enc_data->m_vo_dsp, sampread);
}


void *rmdEncodeAudioBuffers(ProgData *pdata)
{
#ifdef HAVE_LIBJACK
	void	*jackbuf = NULL;

	if (pdata->args.use_jack)
		jackbuf = malloc(pdata->sound_framesize * pdata->jdata->buffersize);
#endif

	rmdThreadsSetName("rmdEncodeAudio");

	pdata->v_encoding_clean = 0;
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

			rmdEncodeAudioBuffer(pdata, buff);

			free(buff->data);
			free(buff);
		} else {
#ifdef HAVE_LIBJACK

			pthread_mutex_lock(&pdata->sound_buffer_mutex);
			while (	pdata->running &&
				jack_ringbuffer_read_space(pdata->jdata->sound_buffer) <
				pdata->sound_framesize * pdata->jdata->buffersize)
				pthread_cond_wait(&pdata->sound_data_read, &pdata->sound_buffer_mutex);

			if (pdata->running)
				jack_ringbuffer_read(	pdata->jdata->sound_buffer,
								jackbuf,
								(pdata->sound_framesize *
								pdata->jdata->buffersize)
							);

			pthread_mutex_unlock(&pdata->sound_buffer_mutex);

			if (!pdata->running)
				break;

			rmdEncodeAudioBuffer(pdata, jackbuf);
#endif
		}

		pthread_mutex_lock(&pdata->libogg_mutex);
		while (vorbis_analysis_blockout(&pdata->enc_data->m_vo_dsp, &pdata->enc_data->m_vo_block) == 1) {

			vorbis_analysis(&pdata->enc_data->m_vo_block, NULL);
			vorbis_bitrate_addblock(&pdata->enc_data->m_vo_block);

			while (vorbis_bitrate_flushpacket(&pdata->enc_data->m_vo_dsp, &pdata->enc_data->m_ogg_pckt2))
				ogg_stream_packetin(&pdata->enc_data->m_ogg_vs, &pdata->enc_data->m_ogg_pckt2);
		}
		pthread_mutex_unlock(&pdata->libogg_mutex);
	}

	pthread_mutex_lock(&pdata->vorbis_lib_mutex);
	pdata->v_encoding_clean = 1;
	pthread_cond_signal(&pdata->vorbis_lib_clean);
	pthread_mutex_unlock(&pdata->vorbis_lib_mutex);
	pthread_exit(&errno);
}


void rmdSyncEncodeAudioBuffer(ProgData *pdata, signed char *buff)
{
	rmdEncodeAudioBuffer(pdata, buff);

	pthread_mutex_lock(&pdata->libogg_mutex);
	while (vorbis_analysis_blockout(&pdata->enc_data->m_vo_dsp, &pdata->enc_data->m_vo_block) == 1) {

		vorbis_analysis(&pdata->enc_data->m_vo_block, NULL);
		vorbis_bitrate_addblock(&pdata->enc_data->m_vo_block);

		while (vorbis_bitrate_flushpacket(&pdata->enc_data->m_vo_dsp, &pdata->enc_data->m_ogg_pckt2))
			ogg_stream_packetin(&pdata->enc_data->m_ogg_vs, &pdata->enc_data->m_ogg_pckt2);
	}
	pthread_mutex_unlock(&pdata->libogg_mutex);

	if (!pdata->running)
		pdata->enc_data->m_ogg_vs.e_o_s = 1;

	pdata->avd -= pdata->periodtime_us;
}
