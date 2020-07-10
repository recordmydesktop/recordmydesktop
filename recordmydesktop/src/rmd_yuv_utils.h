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

#ifndef YUV_UTILS_H
#define YUV_UTILS_H 1

#include "rmd_macro.h"
#include "rmd_types.h"


// The macros work directly on this data (for performance reasons I
// suppose) so we keep this global
extern unsigned char Yr[256], Yg[256], Yb[256],
                     Ur[256], Ug[256], UbVr[256],
                     Vg[256], Vb[256];


// We keep these global for now. FIXME: Isolate them.
extern unsigned char	*yblocks,
			*ublocks,
			*vblocks;


/**
* Fill Yr,Yg,Yb,Ur,Ug.Ub,Vr,Vg,Vb arrays(globals) with values.
*/
void rmdMakeMatrices(void);

/* update yuv from data and optionally data_back */
void rmdUpdateYuvBuffer(	yuv_buffer *yuv,
				unsigned char *data,
				unsigned char *data_back,
				int x_tm,
				int y_tm,
				int width_tm,
				int height_tm,
				int sampling_type,
				int color_depth);

void rmdDummyPointerToYuv(	yuv_buffer *yuv,
				unsigned char *data_tm,
				int x_tm,
				int y_tm,
				int width_tm,
				int height_tm,
				int x_offset,
				int y_offset,
				unsigned char no_pixel);

void rmdXFixesPointerToYuv(	yuv_buffer *yuv,
				unsigned char *data_tm,
				int x_tm,
				int y_tm,
				int width_tm,
				int height_tm,
				int x_offset,
				int y_offset,
				int column_discard_stride);

#endif
