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
#include "rmd_update_image.h"

#include "rmd_getzpixmap.h"
#include "rmd_yuv_utils.h"
#include "rmd_types.h"

#include <X11/Xlibint.h>
#include <X11/extensions/shmproto.h>
#include <X11/extensions/XShm.h>

#include <assert.h>


void rmdUpdateImage(	Display * dpy,
			yuv_buffer *yuv,
			DisplaySpecs *specs,
			RectArea **root,
			BRWindow *brwin,
			EncData *enc,
			Image *image,
			int noshmem,
			int shm_opcode,
			int no_quick_subsample)
{
	RectArea *temp;

	for (temp = *root; temp; temp = temp->next) {

		/* sanity check the clipping, nothing on the reclist
		 * should go outside rrect
		 */
		assert(temp->rect.x >= brwin->rrect.x);
		assert(temp->rect.y >= brwin->rrect.y);
		assert(temp->rect.x + temp->rect.width <= brwin->rrect.x + brwin->rrect.width);
		assert(temp->rect.y + temp->rect.height <= brwin->rrect.y + brwin->rrect.height);

		if (noshmem) {
			rmdGetZPixmap(	dpy,
					specs->root,
					image->ximage->data,
					temp->rect.x,
					temp->rect.y,
					temp->rect.width,
					temp->rect.height);
		} else {
			rmdGetZPixmapSHM(	dpy,
						specs->root,
						&image->shm_info,
						shm_opcode,
						image->ximage->data,
						temp->rect.x,
						temp->rect.y,
						temp->rect.width,
						temp->rect.height);
		}
		rmdUpdateYuvBuffer(
			yuv,
			(unsigned char *)image->ximage->data,
			NULL,
			temp->rect.x - brwin->rrect.x,
			temp->rect.y - brwin->rrect.y,
			temp->rect.width,
			temp->rect.height,
			no_quick_subsample,
			specs->depth
		);
	}
}
