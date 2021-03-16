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
#include "rmd_cache_frame.h"

#include "rmd_yuv_utils.h"
#include "rmd_cache.h"
#include "rmd_threads.h"
#include "rmd_types.h"

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>


#define BYTES_PER_MB          (1024 * 1024)
#define CACHE_OUT_BUFFER_SIZE (4 * 1024)


static int rmdFlushBlock(
			unsigned char *buf,
			int blockno,
			int width,
			int height,
			int blockwidth,
			CacheFile *icf,
			int flush)
{

	int	bytes_written = 0,
		block_i = (!blockwidth) ? 0 : (blockno / (width / blockwidth)),//place on the grid
		block_k = (!blockwidth) ? 0 : (blockno % (width / blockwidth));
	register unsigned char *buf_reg = (&buf[(block_i * width + block_k) * blockwidth]);
	static unsigned char out_buffer[CACHE_OUT_BUFFER_SIZE];
	static unsigned int out_buffer_bytes = 0;

	if (out_buffer_bytes + pow(blockwidth, 2) >= CACHE_OUT_BUFFER_SIZE || (flush && out_buffer_bytes)) {
		rmdCacheFileWrite(icf, (void *)out_buffer, out_buffer_bytes); /* XXX: errors! */
		bytes_written = out_buffer_bytes;
		out_buffer_bytes = 0;
	}

	if (!flush) {
		register unsigned char *out_buf_reg = &out_buffer[out_buffer_bytes];

		for (int j = 0;j < blockwidth; j++) {

			for(int i = 0;i < blockwidth; i++)
				(*out_buf_reg++) = (*buf_reg++);

			out_buffer_bytes += blockwidth;
			buf_reg += width - blockwidth;
		}
	}

	return bytes_written;
}

void *rmdCacheImageBuffer(ProgData *pdata)
{

	int		index_entry_size = sizeof(u_int32_t),
			blocknum_x = pdata->enc_data->yuv.y_width / Y_UNIT_WIDTH,
			blocknum_y = pdata->enc_data->yuv.y_height / Y_UNIT_WIDTH,
			firstrun = 1,
			frameno = 0,
			nbytes = 0;
	u_int32_t	ynum, unum, vnum,
			y_short_blocks[blocknum_x * blocknum_y],
			u_short_blocks[blocknum_x * blocknum_y],
			v_short_blocks[blocknum_x * blocknum_y];
	unsigned long long int total_bytes = 0;
	unsigned long long int total_received_bytes = 0;
	unsigned int	capture_frameno = 0;
	CacheFile	*icf;

	rmdThreadsSetName("rmdCacheImages");

	icf = pdata->cache_data->icf;
	if (!icf)
		exit(13);

	while (pdata->running) {
		FrameHeader	fheader;

		ynum = unum = vnum = 0;

		pthread_mutex_lock(&pdata->img_buff_ready_mutex);
		while (pdata->running && capture_frameno >= pdata->capture_frameno)
			pthread_cond_wait(&pdata->image_buffer_ready, &pdata->img_buff_ready_mutex);

		capture_frameno = pdata->capture_frameno;
		pthread_mutex_unlock(&pdata->img_buff_ready_mutex);

		pthread_mutex_lock(&pdata->pause_mutex);
		while (pdata->paused)
			pthread_cond_wait(&pdata->pause_cond, &pdata->pause_mutex);
		pthread_mutex_unlock(&pdata->pause_mutex);

		pthread_mutex_lock(&pdata->yuv_mutex);

		//find and flush different blocks
		if (firstrun) {
			firstrun = 0;
			for(int j = 0; j < blocknum_x * blocknum_y; j++) {
					yblocks[ynum] = 1;
					y_short_blocks[ynum] = j;
					ynum++;
					ublocks[unum] = 1;
					u_short_blocks[unum] = j;
					unum++;
					vblocks[vnum] = 1;
					v_short_blocks[vnum] = j;
					vnum++;
			}
		} else {
			/**COMPRESS ARRAYS*/
			for(int j = 0; j < blocknum_x * blocknum_y; j++) {
				if (yblocks[j]) {
					y_short_blocks[ynum] = j;
					ynum++;
				}
				if (ublocks[j]) {
					u_short_blocks[unum] = j;
					unum++;
				}
				if (vblocks[j]) {
					v_short_blocks[vnum] = j;
					vnum++;
				}
			}
		}

		/**WRITE FRAME TO DISK*/
		if (icf->gzfp) {
			if (ynum * 4 + unum + vnum > (blocknum_x * blocknum_y * 6) / 10)
				gzsetparams(icf->gzfp, 1, Z_FILTERED);
			else
				gzsetparams(icf->gzfp, 0, Z_FILTERED);
		}

		strncpy(fheader.frame_prefix, "FRAM", 4);
		fheader.capture_frameno = capture_frameno;
		frameno++;

		fheader.Ynum = ynum;
		fheader.Unum = unum;
		fheader.Vnum = vnum;

		nbytes += rmdCacheFileWrite(icf, (void*)&fheader, sizeof(FrameHeader));
		//flush indexes
		if (ynum)
			nbytes += rmdCacheFileWrite(icf, (void*)y_short_blocks, ynum * index_entry_size);

		if (unum)
			nbytes += rmdCacheFileWrite(icf, (void*)u_short_blocks, unum * index_entry_size);

		if (vnum)
			nbytes += rmdCacheFileWrite(icf, (void*)v_short_blocks, vnum * index_entry_size);

		//flush the blocks for each buffer
		if (ynum) {
			for(int j = 0; j < ynum; j++)
				nbytes += rmdFlushBlock(pdata->enc_data->yuv.y,
							y_short_blocks[j],
							pdata->enc_data->yuv.y_width,
							pdata->enc_data->yuv.y_height,
							Y_UNIT_WIDTH,
							icf,
							0);
		}

		if (unum) {
			for(int j = 0; j < unum; j++)
				nbytes += rmdFlushBlock(pdata->enc_data->yuv.u,
							u_short_blocks[j],
							pdata->enc_data->yuv.uv_width,
							pdata->enc_data->yuv.uv_height,
							UV_UNIT_WIDTH,
							icf,
							0);
		}

		if (vnum) {
			for(int j = 0; j < vnum; j++)
				nbytes += rmdFlushBlock(pdata->enc_data->yuv.v,
							v_short_blocks[j],
							pdata->enc_data->yuv.uv_width,
							pdata->enc_data->yuv.uv_height,
							UV_UNIT_WIDTH,
							icf,
							0);
		}

		//release main buffer
		pthread_mutex_unlock(&pdata->yuv_mutex);

		nbytes += rmdFlushBlock(NULL, 0, 0, 0, 0, icf, 1);
	}
	total_bytes += nbytes;

	{
		unsigned int bytes_per_pixel = pdata->specs.depth >= 24 ? 4 : 2;
		unsigned int pixels_per_frame = pdata->enc_data->yuv.y_width * pdata->enc_data->yuv.y_height;

		total_received_bytes = ((unsigned long long int)frameno) * bytes_per_pixel * pixels_per_frame;
	}

	if (total_received_bytes) {
		double percent_of_data_left = (total_bytes / (double)total_received_bytes) * 100;

		fprintf(stderr, "\n"
				"*********************************************\n"
				"\n"
				"Cached %llu MB, from %llu MB that were received.\n"
				"Average cache compression ratio: %.1f %%\n"
				"\n"
				"*********************************************\n",
				total_bytes / BYTES_PER_MB,
				total_received_bytes / BYTES_PER_MB,
				100 - percent_of_data_left);

	}

	fprintf(stderr, "Saved %d frames in a total of %d requests\n",
			frameno,
			capture_frameno);

	rmdCacheFileClose(pdata->cache_data->icf);

	pthread_exit(&errno);
}
