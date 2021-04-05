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
#include "rmd_load_cache.h"

#include "rmd_cache.h"
#include "rmd_encode_audio_buffers.h"
#include "rmd_encode_image_buffer.h"
#include "rmd_macro.h"
#include "rmd_threads.h"
#include "rmd_types.h"

#include <pthread.h>
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//The number of bytes for every
//sub-block of the y,u and v planes.
//Since the blocks are square
//these are obviously the squares
//of the widths(specified above),
//but the definitions bellow are only
//for convenience anyway.
#define Y_UNIT_BYTES	0x0100
#define UV_UNIT_BYTES   0x0040


//The frame after retrieval.
//Based on the Header information
//we can read the correct amount of bytes.
typedef struct _CachedFrame{
	FrameHeader *header;
	u_int32_t	*YBlocks,	 //identifying number on the grid,
			*UBlocks,	 //starting at top left
			*VBlocks;	 //	   >>	  >>
	unsigned char	*YData,		//pointer to data for the blocks that have changed,
			*UData,		//which have to be remapped
			*VData;		//on the buffer when reading
}CachedFrame;


static void rmdLoadBlock(
			unsigned char *dest,
			unsigned char *source,
			int blockno,
			int width,
			int height,
			int blockwidth)
{
	int	block_i = blockno / (width / blockwidth),//place on the grid
		block_k = blockno % (width / blockwidth);

	for (int j = 0; j < blockwidth; j++)//we copy rows
		memcpy(	&dest[(block_i * width + block_k) * blockwidth + j * width],
			&source[j * blockwidth],
			blockwidth);
}

//returns number of bytes
static int rmdReadZF(void * buffer, size_t size, size_t nmemb, CacheFile *icf)
{
	return rmdCacheFileRead(icf, buffer, size * nmemb);
}

static int rmdReadFrame(CachedFrame *frame, CacheFile *icf)
{
	int	index_entry_size = sizeof(u_int32_t);

	if (frame->header->Ynum > 0) {
		if (rmdReadZF(	frame->YBlocks,
				index_entry_size,
				frame->header->Ynum,
				icf) != index_entry_size * frame->header->Ynum) {
			return -1;
		}
	}

	if (frame->header->Unum > 0) {
		if (rmdReadZF(	frame->UBlocks,
				index_entry_size,
				frame->header->Unum,
				icf) != index_entry_size * frame->header->Unum) {
			return -1;
		}
	}

	if (frame->header->Vnum > 0) {
		if (rmdReadZF(	frame->VBlocks,
				index_entry_size,
				frame->header->Vnum,
				icf) != index_entry_size * frame->header->Vnum) {
			return -1;
		}
	}

	if (frame->header->Ynum > 0) {
		if (rmdReadZF(	frame->YData,
				Y_UNIT_BYTES,
				frame->header->Ynum,
				icf) != Y_UNIT_BYTES * frame->header->Ynum) {
			return -2;
		}
	}

	if (frame->header->Unum > 0) {
		if (rmdReadZF(	frame->UData,
				UV_UNIT_BYTES,
				frame->header->Unum,
				icf) != UV_UNIT_BYTES * frame->header->Unum) {
			return -2;
		}
	}

	if (frame->header->Vnum > 0) {
		if (rmdReadZF(	frame->VData,
				UV_UNIT_BYTES,
				frame->header->Vnum,
				icf) != UV_UNIT_BYTES * frame->header->Vnum) {
			return -2;
		}
	}

	return 0;
}

static int read_header(FrameHeader *fheader, CacheFile *icf)
{
	if (!(rmdCacheFileRead(icf, fheader, sizeof(FrameHeader)) == sizeof(FrameHeader)))
		return 0;

	return !strncmp(fheader->frame_prefix, "FRAM", 4);
}

void *rmdLoadCache(ProgData *pdata)
{
	yuv_buffer	*yuv = &pdata->enc_data->yuv;
	CacheFile	*icf;
	FILE		*afp = pdata->cache_data->afp;
	FrameHeader	fheader;
	CachedFrame	frame;
	int		audio_end = 0,
			thread_exit = 0,	//0 success, -1 couldn't find files,1 couldn't remove
			blocknum_x = pdata->enc_data->yuv.y_width / Y_UNIT_WIDTH,
			blocknum_y = pdata->enc_data->yuv.y_height / Y_UNIT_WIDTH,
			blockszy = Y_UNIT_BYTES,//size of y plane block in bytes
			blockszuv = UV_UNIT_BYTES;//size of u,v plane blocks in bytes
	signed char	*sound_data = (signed char *)malloc(pdata->periodsize * pdata->sound_framesize);

	u_int32_t	YBlocks[(yuv->y_width * yuv->y_height) / Y_UNIT_BYTES],
			UBlocks[(yuv->uv_width * yuv->uv_height) / UV_UNIT_BYTES],
			VBlocks[(yuv->uv_width * yuv->uv_height) / UV_UNIT_BYTES];
	unsigned int	last_capture_frameno = 0;

	rmdThreadsSetName("rmdEncodeCache");

	// We allocate the frame that we will use
	frame.header	= &fheader;
	frame.YBlocks	= YBlocks;
	frame.UBlocks	= UBlocks;
	frame.VBlocks	= VBlocks;
	frame.YData	= malloc(yuv->y_width  * yuv->y_height);
	frame.UData	= malloc(yuv->uv_width * yuv->uv_height);
	frame.VData	= malloc(yuv->uv_width * yuv->uv_height);

	//and the we open our files
	icf = rmdCacheFileOpen(pdata, pdata->cache_data->imgdata, RMD_CACHE_FILE_MODE_READ);
	if (!icf) {
		thread_exit = -1;
		pthread_exit(&thread_exit);
	}

	if (!pdata->args.nosound) {
		afp = fopen(pdata->cache_data->audiodata, "rb");
		if (afp == NULL) {
			thread_exit = -1;
			pthread_exit(&thread_exit);
		}
	}

	//this will be used now to define if we proccess audio or video
	//on any given loop.
	pdata->avd = 0;
	//If sound finishes first,we go on with the video.
	//If video ends we will do one more run to flush audio in the ogg file
	while (pdata->running) {

		//video load and encoding
		if (pdata->avd <= 0 || pdata->args.nosound || audio_end) {

			if (read_header(frame.header, icf)) {

				if (pdata->capture_frameno)
					fprintf(stdout,	"\r[%d%%] ",
							((frame.header->capture_frameno) * 100) / pdata->capture_frameno);
				else
					fprintf(stdout, "\r[%d frames rendered] ",
							frame.header->capture_frameno);
				if (icf->chapter)
					fprintf(stdout, "\t[Cache File %d]", icf->chapter);
				fflush(stdout);

				if (	(frame.header->Ynum > blocknum_x * blocknum_y) ||
					(frame.header->Unum > blocknum_x * blocknum_y) ||
					(frame.header->Vnum > blocknum_x * blocknum_y) ||
					rmdReadFrame(&frame, icf) < 0) {

					raise(SIGINT);
					continue;
				}

				//load the blocks for each buffer
				if (frame.header->Ynum)
					for (int j = 0; j < frame.header->Ynum; j++)
						rmdLoadBlock(	yuv->y,
								&frame.YData[j * blockszy],
								frame.YBlocks[j],
								yuv->y_width,
								yuv->y_height,
								Y_UNIT_WIDTH);
				if (frame.header->Unum)
					for (int j = 0; j < frame.header->Unum; j++)
						rmdLoadBlock(	yuv->u,
								&frame.UData[j * blockszuv],
								frame.UBlocks[j],
								yuv->uv_width,
								yuv->uv_height,
								UV_UNIT_WIDTH);
				if (frame.header->Vnum)
					for (int j = 0; j < frame.header->Vnum; j++)
						rmdLoadBlock(	yuv->v,
								&frame.VData[j * blockszuv],
								frame.VBlocks[j],
								yuv->uv_width,
								yuv->uv_height,
								UV_UNIT_WIDTH);

				rmdSyncEncodeImageBuffer(pdata, fheader.capture_frameno - last_capture_frameno);

				last_capture_frameno = fheader.capture_frameno;
			} else {
				raise(SIGINT);
			}
		//audio load and encoding
		} else {
			if (!audio_end) {
				int nbytes = fread(sound_data, 1, pdata->periodsize*
								 pdata->sound_framesize, afp);
				if (nbytes <= 0)
					audio_end = 1;
				else
					rmdSyncEncodeAudioBuffer(pdata, sound_data);
			}
		}
	}

	pthread_mutex_lock(&pdata->theora_lib_mutex);
	pdata->th_encoding_clean = 1;
	pthread_cond_signal(&pdata->theora_lib_clean);
	pthread_mutex_unlock(&pdata->theora_lib_mutex);

	pthread_mutex_lock(&pdata->vorbis_lib_mutex);
	pdata->v_encoding_clean = 1;
	pthread_cond_signal(&pdata->vorbis_lib_clean);
	pthread_mutex_unlock(&pdata->vorbis_lib_mutex);
	fprintf(stdout, "\n");

	// Clear frame
	free(frame.YData);
	free(frame.UData);
	free(frame.VData);

	free(sound_data);

	rmdCacheFileClose(icf);

	if (!pdata->args.nosound)
		fclose(afp);

	pthread_exit(&thread_exit);
}
