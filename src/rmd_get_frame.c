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
#include "rmd_get_frame.h"

#include "rmd_cache.h"
#include "rmd_frame.h"
#include "rmd_getzpixmap.h"
#include "rmd_poll_events.h"
#include "rmd_rectinsert.h"
#include "rmd_threads.h"
#include "rmd_update_image.h"
#include "rmd_yuv_utils.h"
#include "rmd_types.h"

#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XShm.h>

#include <limits.h>
#include <pthread.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>


/* clip_event_area dejavu */
static void clip_dummy_pointer_area(XRectangle *area, XRectangle *clip, XRectangle *res)
{
	res->x =	(((area->x + area->width >= clip->x) &&
			  (area->x <= clip->x + clip->width)) ?
			 ((area->x <= clip->x) ? clip->x : area->x) : -1);

	res->y =	(((area->y + area->height >= clip->y) &&
			  (area->y <= clip->y + clip->height)) ?
			 ((area->y <= clip->y) ? clip->y : area->y) : -1);

	res->width =	(area->x <= clip->x) ? area->width - (clip->x - area->x) :
			(area->x <= clip->x + clip->width) ?
			(clip->width - area->x + clip->x < area->width) ?
			clip->width - area->x + clip->x : area->width : 0;

	res->height =	(area->y <= clip->y) ? area->height - (clip->y - area->y) :
			(area->y <= clip->y + clip->height) ?
			(clip->height - area->y + clip->y < area->height) ?
			clip->height - area->y + clip->y : area->height : 0;

	if (res->x + res->width > clip->x + clip->width)
		res->width = clip->x + clip->width - res->x;

	if (res->y + res->height > clip->y + clip->height)
		res->height = clip->y + clip->height - res->y;
}

#define MARK_BUFFER_AREA_C(	data,							\
				x_tm,							\
				y_tm,							\
				width_tm,						\
				height_tm,						\
				buffer_width,						\
				__depth__) {						\
											\
	register u_int##__depth__##_t *datapi = 					\
			((u_int##__depth__##_t *)data) + y_tm * buffer_width + x_tm;	\
											\
	for(int k = 0; k < height_tm; k++) {						\
		for(int i = 0; i < width_tm; i++) {					\
			*datapi += 1;							\
			datapi++;							\
		}									\
		datapi += buffer_width - width_tm;					\
	}										\
}

static void mark_buffer_area(	unsigned char *data,
				int x_tm, int y_tm,
				int width_tm, int height_tm,
				int buffer_width, int depth) {

	if ((depth == 24) || (depth == 32)) {
		MARK_BUFFER_AREA_C(	data,
					x_tm,
					y_tm,
					width_tm,
					height_tm,
					buffer_width,
					32);
	} else {
		MARK_BUFFER_AREA_C(	data,
					x_tm,
					y_tm,
					width_tm,
					height_tm,
					buffer_width,
					16);
	}
}

//besides taking the first screenshot, this functions primary purpose is to 
//initialize the structures and memory.
static int rmdFirstFrame(ProgData *pdata, Image *image) {
	const XRectangle	*rrect = &pdata->brwin.rrect;

	if (pdata->args.noshared) {
		image->ximage = XGetImage(	pdata->dpy,
						pdata->specs.root,
						rrect->x,
						rrect->y,
						rrect->width,
						rrect->height,
						AllPlanes,
						ZPixmap);
	} else {
		image->ximage = XShmCreateImage(pdata->dpy,
						pdata->specs.visual,
						pdata->specs.depth,
						ZPixmap,
						NULL,
						&image->shm_info,
						rrect->width,
						rrect->height);

		image->shm_info.shmid = shmget(	IPC_PRIVATE,
						image->ximage->bytes_per_line *
						image->ximage->height,
						IPC_CREAT|0777);

		if (image->shm_info.shmid == -1) {
			fprintf(stderr, "Failed to obtain Shared Memory segment!\n");
			return 12;
		}

		image->shm_info.shmaddr = image->ximage->data = shmat(image->shm_info.shmid, NULL, 0);
		image->shm_info.readOnly = False;
		shmctl(image->shm_info.shmid, IPC_RMID, NULL);

		if (!XShmAttach(pdata->dpy, &image->shm_info)) {
			fprintf(stderr, "Failed to attach shared memory to proccess.\n");
			return 12;
		}

		XShmGetImage(	pdata->dpy,
				pdata->specs.root,
				image->ximage,
				rrect->x,
				rrect->y,
				AllPlanes);
	}

	rmdUpdateYuvBuffer(	&pdata->enc_data->yuv,
				(unsigned char *)image->ximage->data,
				NULL,
				0,
				0,
				rrect->width,
				rrect->height,
				pdata->args.no_quick_subsample,
				pdata->specs.depth);

	return 0;
}

//make a deep copy
static void rmdBRWinCpy(BRWindow *target, BRWindow *source) {
	target->winrect = source->winrect;
	target->rrect = source->rrect;
	target->windowid = source->windowid;
}

//recenters the capture area to the mouse
//without exiting the display bounding box
static void rmdMoveCaptureArea(	XRectangle *rect,
				int cursor_x,
				int cursor_y,
				int width,
				int height) {
	int t_x = 0, t_y = 0;

	t_x = cursor_x - rect->width / 2;
	t_x = (t_x >> 1) << 1;
	rect->x = (t_x < 0) ? 0 : ((t_x + rect->width > width) ? width - rect->width : t_x);
	t_y = cursor_y - rect->height / 2;
	t_y = (t_y >> 1) << 1;
	rect->y = (t_y < 0 ) ? 0 : ((t_y + rect->height > height) ? height - rect->height : t_y);
}


static void rmdBlocksReset(unsigned int blocks_w, unsigned int blocks_h) {
	memset(yblocks, 0, blocks_w * blocks_h * sizeof(*yblocks));
	memset(ublocks, 0, blocks_w * blocks_h * sizeof(*ublocks));
	memset(vblocks, 0, blocks_w * blocks_h * sizeof(*vblocks));
}


/**
*   Extract cache blocks from damage list
*
* \param root Root entry of the list with damaged areas
*
* \param x_offset left x of the recording area
*
* \param y_offset upper y of the recording area
*
* \param blocks_w Width of image in blocks
*
* \param blocks_h Height of image in blocks
*/
static void rmdBlocksFromList(	RectArea   **root,
				unsigned int x_offset,
				unsigned int y_offset,
				unsigned int blocks_w,
				unsigned int blocks_h) {

	rmdBlocksReset(blocks_w, blocks_h);

	for (RectArea *temp = *root; temp; temp = temp->next) {
		int	row_start, row_end, column_start, column_end;

		column_start	= ((int)(temp->rect.x - x_offset)) / Y_UNIT_WIDTH;
		column_end	= ((int)(temp->rect.x + (temp->rect.width - 1) - x_offset)) / Y_UNIT_WIDTH;
		row_start	= ((int)(temp->rect.y - y_offset)) / Y_UNIT_WIDTH;
		row_end		= ((int)(temp->rect.y + (temp->rect.height - 1) - y_offset)) / Y_UNIT_WIDTH;

		for (int i = row_start; i < row_end + 1; i++) {
			for (int j = column_start; j < column_end + 1; j++) {

				int	blockno = i * blocks_w + j;

				yblocks[blockno] = 1;
				ublocks[blockno] = 1;
				vblocks[blockno] = 1;
			}
		}
	}
}

void *rmdGetFrame(ProgData *pdata) {
	int	blocks_w = pdata->enc_data->yuv.y_width / Y_UNIT_WIDTH,
		blocks_h = pdata->enc_data->yuv.y_height / Y_UNIT_WIDTH;
	unsigned int msk_ret;
	XRectangle mouse_pos_abs, mouse_pos_rel, mouse_pos_temp;
	BRWindow temp_brwin;
	Window root_ret, child_ret; //Frame
	XFixesCursorImage *xcim = NULL;
	Image image = {}, image_back = {};	//the image that holds
						//the current full screenshot
	int init_img1 = 0, init_img2 = 0, img_sel, d_buff;

	rmdThreadsSetName("rmdGetFrame");

	img_sel = d_buff = pdata->args.full_shots;

	if ((init_img1 = rmdFirstFrame(pdata, &image) != 0)) {
		if (pdata->args.encOnTheFly) {
			if (remove(pdata->args.filename)) {
				perror("Error while removing file:\n");
			} else {
				fprintf(stderr,	"SIGABRT received, file %s removed\n",
						pdata->args.filename);
			}
		} else {
			rmdPurgeCache(pdata->cache_data, !pdata->args.nosound);
		}

		exit(init_img1);
	}

	if (d_buff) {
		if ((init_img2 = rmdFirstFrame(pdata, &image_back) != 0)) {
			if (pdata->args.encOnTheFly) {
				if (remove(pdata->args.filename)) {
					perror("Error while removing file:\n");
				} else{
					fprintf(stderr,	"SIGABRT received, file %s removed\n",
							pdata->args.filename);
				}
			} else {
				rmdPurgeCache(pdata->cache_data, !pdata->args.nosound);
			}

			exit(init_img2);
		}
	}

	if (!pdata->args.noframe) {
		pdata->shaped_w = rmdFrameInit(	pdata->dpy,
						pdata->specs.screen,
						pdata->specs.root,
						pdata->brwin.rrect.x,
						pdata->brwin.rrect.y,
						pdata->brwin.rrect.width,
						pdata->brwin.rrect.height);

		XSelectInput(pdata->dpy, pdata->shaped_w, ExposureMask);
	}

	mouse_pos_abs.x = mouse_pos_temp.x = 0;
	mouse_pos_abs.y = mouse_pos_temp.y = 0;
	mouse_pos_abs.width = mouse_pos_temp.width = pdata->dummy_p_size;
	mouse_pos_abs.height = mouse_pos_temp.height = pdata->dummy_p_size;
	
	//This is the the place where we call XSelectInput
	//and arrange so that we listen for damage on all 
	//windows
	rmdInitEventsPolling(pdata);

	while (pdata->running) {
		unsigned	time_frameno;

		pthread_mutex_lock(&pdata->time_mutex);
		while (pdata->capture_frameno >= pdata->time_frameno) {
			pthread_cond_wait(&pdata->time_cond, &pdata->time_mutex);

			if (pdata->capture_frameno >= pdata->time_frameno) {
				/* *Likely* paused, but could be a spurious wakeup.
				 * either way, it's harmless to service the event loop
				 * before waiting again.  This is how the timer keeps
				 * the event loop serviced by signaling us without
				 * advancing time_frameno.
				 */
				pthread_mutex_unlock(&pdata->time_mutex);
				rmdEventLoop(pdata);
				pthread_mutex_lock(&pdata->time_mutex);
			}
		}

		time_frameno = pdata->time_frameno;
		pthread_mutex_unlock(&pdata->time_mutex);

		//read all events and construct list with damage 
		//events (if not full_shots)
		rmdEventLoop(pdata);

		//switch back and front buffers (full_shots only)
		if (d_buff)
			img_sel = img_sel ? 0 : 1;

		rmdBRWinCpy(&temp_brwin, &pdata->brwin);


		if (	pdata->args.xfixes_cursor ||
			pdata->args.have_dummy_cursor ||
			pdata->args.follow_mouse) {


			// Pointer sequence:
			// * Mark previous position as dirty with rmdRectInsert()
			// * Update to new position
			// * Mark new position as dirty with rmdRectInsert()
			if (	!pdata->args.full_shots &&
				mouse_pos_temp.x >= 0 &&
				mouse_pos_temp.y >= 0 &&
				mouse_pos_temp.width > 0 &&
				mouse_pos_temp.height > 0) {
				rmdRectInsert(&pdata->rect_root, &mouse_pos_temp);
			}

			if (pdata->args.xfixes_cursor) {
				xcim = XFixesGetCursorImage(pdata->dpy);
				mouse_pos_abs.x = xcim->x - xcim->xhot;
				mouse_pos_abs.y = xcim->y - xcim->yhot;
				mouse_pos_abs.width = xcim->width;
				mouse_pos_abs.height = xcim->height;
			} else {
				XQueryPointer(	pdata->dpy,
						pdata->specs.root,
						&root_ret, &child_ret,
						(int *)&mouse_pos_abs.x,
						(int *)&mouse_pos_abs.y,
						(int *)&mouse_pos_rel.x,
						(int *)&mouse_pos_rel.y,
						&msk_ret);
			}
			
			clip_dummy_pointer_area(&mouse_pos_abs, &temp_brwin.rrect, &mouse_pos_temp);
			if (	mouse_pos_temp.x >= 0 &&
				mouse_pos_temp.y >= 0 &&
				mouse_pos_temp.width > 0 &&
				mouse_pos_temp.height > 0) {

				//there are 3 capture scenarios:
				// * Xdamage
				// * full-shots with double buffering 
				// * full-shots on a single buffer
				//The last one cannot be reached through
				//this code (see above how the d_buf variable is set), but 
				//even if it could, it would not be of interest regarding the 
				//marking of the cursor area. Single buffer means full repaint 
				//on every frame so there is no need for marking at all.

				if (!pdata->args.full_shots) { 
					rmdRectInsert(&pdata->rect_root, &mouse_pos_temp);
				} else if (d_buff) {
					unsigned char *back_buff= img_sel ?
								((unsigned char*)image.ximage->data) :
								((unsigned char*)image_back.ximage->data);

					mark_buffer_area(
						back_buff,
						mouse_pos_temp.x - temp_brwin.rrect.x,
						mouse_pos_temp.y - temp_brwin.rrect.y,
						mouse_pos_temp.width, mouse_pos_temp.height, temp_brwin.rrect.width,
						pdata->specs.depth
					);
				}
			}
		}

		if (pdata->args.follow_mouse) {
			rmdMoveCaptureArea(	&pdata->brwin.rrect,
						mouse_pos_abs.x + pdata->args.xfixes_cursor ? xcim->xhot : 0,
						mouse_pos_abs.y + pdata->args.xfixes_cursor ? xcim->yhot : 0,
						pdata->specs.width,
						pdata->specs.height);

			if (!pdata->args.noframe)
				rmdMoveFrame(	pdata->dpy,
						pdata->shaped_w,
						temp_brwin.rrect.x,
						temp_brwin.rrect.y);
		}

		if (!pdata->args.full_shots) {
			pthread_mutex_lock(&pdata->yuv_mutex);
			rmdUpdateImage(	pdata->dpy,
					&pdata->enc_data->yuv,
					&pdata->specs,
					&pdata->rect_root,
					&temp_brwin,
					pdata->enc_data,
					&image,
					pdata->args.noshared,
					pdata->shm_opcode,
					pdata->args.no_quick_subsample);

			rmdBlocksFromList(	&pdata->rect_root,
						temp_brwin.rrect.x,
						temp_brwin.rrect.y,
						blocks_w,
						blocks_h);

			pthread_mutex_unlock(&pdata->yuv_mutex);
		} else {
			unsigned char *front_buff = !img_sel ?	((unsigned char*)image.ximage->data):
								((unsigned char*)image_back.ximage->data);
			unsigned char *back_buff = !d_buff ? NULL : (img_sel ?
								((unsigned char*)image.ximage->data):
								((unsigned char*)image_back.ximage->data));

			if (pdata->args.noshared) {
				rmdGetZPixmap(	pdata->dpy,
						pdata->specs.root,
						image.ximage->data,
						temp_brwin.rrect.x,
						temp_brwin.rrect.y,
						temp_brwin.rrect.width,
						temp_brwin.rrect.height);
			} else {
				XShmGetImage(	pdata->dpy,
						pdata->specs.root,
						((!img_sel) ? image.ximage : image_back.ximage),
						temp_brwin.rrect.x,
						temp_brwin.rrect.y,
						AllPlanes);
			}

			pthread_mutex_lock(&pdata->yuv_mutex);
			rmdBlocksReset(blocks_w, blocks_h);
			rmdUpdateYuvBuffer(	&pdata->enc_data->yuv,
						front_buff,
						back_buff,
						0,
						0,
						temp_brwin.rrect.width,
						temp_brwin.rrect.height,
						pdata->args.no_quick_subsample,
						pdata->specs.depth);

			pthread_mutex_unlock(&pdata->yuv_mutex);
		}

		if (pdata->args.xfixes_cursor || pdata->args.have_dummy_cursor) {
			int mouse_xoffset, mouse_yoffset;

			//avoid segfaults
			clip_dummy_pointer_area(&mouse_pos_abs, &temp_brwin.rrect, &mouse_pos_temp);
			mouse_xoffset = mouse_pos_temp.x - mouse_pos_abs.x;
			mouse_yoffset = mouse_pos_temp.y - mouse_pos_abs.y;

			if ((mouse_xoffset < 0) || (mouse_xoffset > mouse_pos_abs.width))
				mouse_xoffset = 0;

			if ((mouse_yoffset < 0) || (mouse_yoffset > mouse_pos_abs.height))
				mouse_yoffset = 0;

			//draw the cursor
			if (	(mouse_pos_temp.x >= 0) &&
				(mouse_pos_temp.y >= 0) &&
				(mouse_pos_temp.width > 0) &&
				(mouse_pos_temp.height > 0)) {

					if (pdata->args.xfixes_cursor) {
						rmdXFixesPointerToYuv(
							&pdata->enc_data->yuv,
							((unsigned char*)xcim->pixels),
							mouse_pos_temp.x - temp_brwin.rrect.x,
							mouse_pos_temp.y - temp_brwin.rrect.y,
							mouse_pos_temp.width,
							mouse_pos_temp.height,
							mouse_xoffset,
							mouse_yoffset,
							xcim->width-mouse_pos_temp.width
						);
					} else {
						rmdDummyPointerToYuv(
							&pdata->enc_data->yuv,
							pdata->dummy_pointer,
							mouse_pos_temp.x - temp_brwin.rrect.x,
							mouse_pos_temp.y - temp_brwin.rrect.y,
							mouse_pos_temp.width,
							mouse_pos_temp.height,
							mouse_xoffset,
							mouse_yoffset,
							pdata->npxl
						);
					}

				if (d_buff) {
					//make previous cursor position dirty
					//on the currently front buffer (which 
					//will be the back buffer next time it's 
					//used) 
					unsigned char *front_buff = !img_sel ?
								((unsigned char*)image.ximage->data) :
								((unsigned char*)image_back.ximage->data);

					mark_buffer_area(
						front_buff,
						mouse_pos_temp.x - temp_brwin.rrect.x,
						mouse_pos_temp.y - temp_brwin.rrect.y,
						mouse_pos_temp.width,
						mouse_pos_temp.height,
						temp_brwin.rrect.width,
						pdata->specs.depth
					);
				}

			}

			if (pdata->args.xfixes_cursor) {
				XFree(xcim);
				xcim = NULL;
			}
		}

		if (!pdata->args.full_shots)
			rmdClearList(&pdata->rect_root);

		/* notify the encoder of the new frame */
		pthread_mutex_lock(&pdata->img_buff_ready_mutex);
		pdata->capture_frameno = time_frameno;
		pthread_cond_signal(&pdata->image_buffer_ready);
		pthread_mutex_unlock(&pdata->img_buff_ready_mutex);
	}

	if (!pdata->args.noframe)
		XDestroyWindow(pdata->dpy, pdata->shaped_w);

	if (!pdata->args.noshared) {
		XShmDetach(pdata->dpy, &image.shm_info);
		shmdt(image.shm_info.shmaddr);
		if (d_buff) {
			XShmDetach(pdata->dpy, &image_back.shm_info);
			shmdt(image_back.shm_info.shmaddr);
		}
	}

	pthread_exit(&errno);
}
