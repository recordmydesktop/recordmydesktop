/******************************************************************************
*                      recordMyDesktop - rmd_yuv_utils.c                      *
*******************************************************************************
*                                                                             *
*            Copyright (C) 2006,2007,2008 John Varouhakis                     *
*            Copyright (C) 2008 Luca Bonavita                                 *
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
#include "rmd_yuv_utils.h"

#include "rmd_math.h"


static unsigned char	Yr[256], Yg[256], Yb[256],
			Ur[256], Ug[256], UbVr[256],
			Vg[256], Vb[256];

// FIXME: These globals are modified in other source files! We keep
// thsee here for now. These are the cache blocks. They need to be
// accesible in the dbuf macros
unsigned char	*yblocks,
		*ublocks,
		*vblocks;

void rmdMakeMatrices (void)
{
	int i;

 	/* assuming 8-bit precision */
 	float Yscale = 219.0, Yoffset = 16.0;
 	float Cscale = 224.0, Coffset = 128.0;
 	float RGBscale = 255.0;

 	float	r, g, b;
 	float	yr, yg, yb;
 	float	ur, ug, ub;
 	float	 vg, vb;	/* vr intentionally missing */

 	/* as for ITU-R BT-601-6 specifications: */
 	r = 0.299;
 	b = 0.114;
 	g = 1.0 - r - b;

 	/*	as a note, here are the coefficients
 		as for ITU-R BT-709 specifications:
 		r=0.2126;	b=0.0722;	g=1.0-r-b; */

 	yr = r * Yscale / RGBscale;
 	yg = g * Yscale / RGBscale;
 	yb = b * Yscale / RGBscale;
 	ur = ( -0.5 * r / ( 1 - b ) ) * Cscale / RGBscale;
 	ug = ( -0.5 * g / ( 1 - b ) ) * Cscale / RGBscale;
 	ub = ( 0.5 * Cscale / RGBscale);
 	/* vr = ub so UbVr = ub*i = vr*i */
 	vg = ( -0.5 * g / ( 1 - r ) ) * Cscale / RGBscale;
 	vb = ( -0.5 * b / ( 1 - r ) ) * Cscale / RGBscale;

	 for (i = 0; i < 256; i++) {
		 Yr[i] = (unsigned char) rmdRoundf( Yoffset + yr * i );
		 Yg[i] = (unsigned char) rmdRoundf( yg * i );
		 Yb[i] = (unsigned char) rmdRoundf( yb * i );

		 Ur[i] = (unsigned char) rmdRoundf( Coffset + ur * i );
		 Ug[i] = (unsigned char) rmdRoundf( ug * i );
		 UbVr[i] = (unsigned char) rmdRoundf( ub * i );

		 Vg[i] = (unsigned char) rmdRoundf( vg * i );
		 Vb[i] = (unsigned char) rmdRoundf( Coffset + vb * i );
	}
}

static inline int blocknum(int xv, int yv, int widthv, int blocksize)
{
	return ((yv/blocksize) * (widthv/blocksize) + (xv/blocksize));
}

/* These at least make some sense as macros since they need duplication for
 * the multiple depths, so I've just moved and reformatted them here for now.
 */

#define UPDATE_Y_PLANE(	data,									\
			x_tm,									\
			y_tm,									\
			width_tm,								\
			height_tm,								\
			yuv,									\
			__depth__) {								\
												\
	register unsigned char		*yuv_Y = (yuv)->y + x_tm + y_tm * (yuv)->y_stride,	\
					*_yr = Yr, *_yg = Yg, *_yb = Yb;			\
	register u_int##__depth__##_t	*datapi = (u_int##__depth__##_t *)data;			\
												\
	for (int k = 0; k < height_tm; k++) {							\
		for (int i = 0; i < width_tm; i++) {						\
			register u_int##__depth__##_t	t_val = *datapi;			\
												\
			*yuv_Y =	_yr[__RVALUE_##__depth__(t_val)] +			\
					_yg[__GVALUE_##__depth__(t_val)] +			\
					_yb[__BVALUE_##__depth__(t_val)];			\
			datapi++;								\
			yuv_Y++;								\
		}										\
												\
		yuv_Y += (yuv)->y_stride - width_tm; 						\
	}											\
}

//when adding the r values, we go beyond
//the (16 bit)range of the t_val variable, but we are performing
//32 bit arithmetics, so there's no problem.
//(This note is useless, I'm just adding because
//the addition of the A components in CALC_TVAL_AVG_32,
//now removed as uneeded, produced an overflow which would have caused
//color distrtion, where it one of the R,G or B components)
#define CALC_TVAL_AVG_16(t_val, datapi, datapi_next) {						\
	register u_int16_t	t1, t2, t3, t4;							\
												\
	t1 = *datapi;										\
	t2 = *(datapi + 1);									\
	t3 = *datapi_next;									\
	t4 = *(datapi_next + 1);								\
												\
	t_val =	((((t1 & __R16_MASK) + (t2 & __R16_MASK) +					\
		(t3 & __R16_MASK) + (t4 & __R16_MASK)) / 4) & __R16_MASK) +			\
		((((t1 & __G16_MASK) + (t2 & __G16_MASK)+					\
		(t3 & __G16_MASK) + (t4 & __G16_MASK)) / 4) & __G16_MASK) +			\
		((((t1 & __B16_MASK) + (t2 & __B16_MASK) +					\
		(t3 & __B16_MASK) + (t4 & __B16_MASK)) / 4) & __B16_MASK);			\
}

//the 4 most significant bytes represent the A component which
//does not need to be added on t_val, as it is always unused
#define CALC_TVAL_AVG_32(t_val, datapi, datapi_next) {						\
	register unsigned int	t1, t2, t3, t4;							\
												\
	t1 = *datapi;										\
	t2 = *(datapi + 1);									\
	t3 = *datapi_next;									\
	t4 = *(datapi_next + 1);								\
												\
	t_val =	((((t1 & 0x00ff0000) + (t2 & 0x00ff0000) +					\
		  (t3 & 0x00ff0000) + (t4 & 0x00ff0000)) / 4) & 0x00ff0000) +			\
		((((t1 & 0x0000ff00) + (t2 & 0x0000ff00) +					\
		  (t3 & 0x0000ff00) + (t4 & 0x0000ff00)) / 4) & 0x0000ff00) +			\
		((((t1 & 0x000000ff) + (t2 & 0x000000ff) +					\
		  (t3 & 0x000000ff) + (t4 & 0x000000ff)) / 4) & 0x000000ff);			\
}

#define UPDATE_A_UV_PIXEL(	yuv_U,								\
				yuv_V,								\
				t_val,								\
				datapi,								\
				datapi_next,							\
				_ur,_ug,_ubvr,_vg,_vb,						\
				sampling,							\
				__depth__)							\
												\
	if (sampling == __PXL_AVERAGE) {							\
		CALC_TVAL_AVG_##__depth__(t_val, datapi, datapi_next)				\
	} else											\
		t_val = *(datapi);								\
												\
	*(yuv_U) =	_ur[__RVALUE_##__depth__(t_val)] +					\
			_ug[__GVALUE_##__depth__(t_val)] +					\
			_ubvr[__BVALUE_##__depth__(t_val)];					\
												\
	*(yuv_V) =	_ubvr[__RVALUE_##__depth__(t_val)] +					\
			_vg[__GVALUE_##__depth__(t_val)] +					\
			_vb[__BVALUE_##__depth__(t_val)];

#define UPDATE_UV_PLANES(	data,								\
				x_tm,								\
				y_tm,								\
				width_tm,							\
				height_tm,							\
				yuv,								\
				sampling,							\
				__depth__) {							\
												\
	register u_int##__depth__##_t	t_val;							\
	register unsigned char		*yuv_U =	(yuv)->u + x_tm / 2 +			\
							(y_tm * (yuv)->uv_width) / 2,		\
					*yuv_V =	(yuv)->v + x_tm / 2 +			\
							(y_tm * (yuv)->uv_width) / 2,		\
					*_ur = Ur, *_ug = Ug, *_ubvr = UbVr,			\
					*_vg = Vg, *_vb = Vb;					\
	register u_int##__depth__##_t	*datapi = (u_int##__depth__##_t *)data,			\
					*datapi_next = NULL;					\
	int				w_odd = width_tm % 2, h_odd = height_tm % 2;		\
												\
	if (sampling == __PXL_AVERAGE)								\
		datapi_next = datapi + width_tm;						\
												\
	for (int k = 0; k < height_tm - h_odd; k += 2) {					\
		for (int i = 0; i < width_tm - w_odd; i += 2) {					\
			UPDATE_A_UV_PIXEL(	yuv_U,						\
						yuv_V,						\
						t_val,						\
						datapi,						\
						datapi_next,					\
						_ur, _ug, _ubvr, _vg, _vb,			\
						sampling,					\
						__depth__);					\
												\
			datapi += 2;								\
			if (sampling == __PXL_AVERAGE)						\
				datapi_next += 2;						\
			yuv_U++;								\
			yuv_V++;								\
		}										\
												\
		yuv_U += ((yuv)->y_stride - (width_tm - w_odd * 2)) >> 1;			\
		yuv_V += ((yuv)->y_stride - (width_tm - w_odd * 2)) >> 1;			\
												\
		datapi += width_tm + w_odd;							\
		if (sampling == __PXL_AVERAGE)							\
			datapi_next += width_tm + w_odd;					\
	}											\
}

#define UPDATE_Y_PLANE_DBUF(	data,								\
				data_back,							\
				x_tm,								\
				y_tm,								\
				width_tm,							\
				height_tm,							\
				yuv,								\
				__depth__) {							\
												\
	register u_int##__depth__##_t	t_val;							\
	register unsigned char		*yuv_Y = (yuv)->y + x_tm + y_tm * (yuv)->y_stride,	\
					*_yr = Yr, *_yg = Yg, *_yb = Yb;			\
	register u_int##__depth__##_t	*datapi = (u_int##__depth__##_t *)data,			\
					*datapi_back = (u_int##__depth__##_t *)data_back;	\
												\
	for (int k = 0; k < height_tm; k++) {							\
		for (int i = 0; i < width_tm; i++) {						\
			if (*datapi != *datapi_back) {						\
				t_val = *datapi;						\
				*yuv_Y =	_yr[__RVALUE_##__depth__(t_val)] +		\
						_yg[__GVALUE_##__depth__(t_val)] +		\
						_yb[__BVALUE_##__depth__(t_val)];		\
				yblocks[blocknum(x_tm + i, y_tm + k, (yuv)->y_width, Y_UNIT_WIDTH)] = 1;\
			}									\
			datapi++;								\
			datapi_back++;								\
			yuv_Y++;								\
		}										\
		yuv_Y += (yuv)->y_stride - width_tm;						\
	}											\
}

#define UPDATE_UV_PLANES_DBUF(	data,								\
				data_back,							\
				x_tm,								\
				y_tm,								\
				width_tm,							\
				height_tm,							\
				yuv,								\
				sampling,							\
				__depth__) {							\
												\
	register u_int##__depth__##_t	t_val;							\
	register unsigned char		*yuv_U =	(yuv)->u + x_tm / 2 +			\
							(y_tm * (yuv)->uv_width) / 2,		\
					*yuv_V =	(yuv)->v + x_tm / 2 +			\
							(y_tm * (yuv)->uv_width) / 2,		\
					*_ur = Ur, *_ug	= Ug, *_ubvr = UbVr,			\
					*_vg = Vg, *_vb = Vb;					\
												\
	register u_int##__depth__##_t	*datapi = (u_int##__depth__##_t *)data,			\
					*datapi_next = NULL,					\
					*datapi_back = (u_int##__depth__##_t *)data_back,	\
					*datapi_back_next = NULL;				\
	int				w_odd = width_tm % 2, h_odd = height_tm % 2;		\
												\
	if (sampling == __PXL_AVERAGE) {							\
		datapi_next = datapi + width_tm;						\
		datapi_back_next = datapi_back + width_tm;					\
												\
		for (int k = 0; k < height_tm - h_odd; k += 2) {				\
			for (int i = 0; i < width_tm - w_odd; i += 2) {				\
				if (	(*datapi != *datapi_back ||				\
					(*(datapi + 1) != *(datapi_back + 1)) ||		\
					(*datapi_next != *datapi_back_next) ||			\
					(*(datapi_next + 1) != *(datapi_back_next + 1)))) {	\
												\
					UPDATE_A_UV_PIXEL(	yuv_U,				\
								yuv_V,				\
								t_val,				\
								datapi,				\
								datapi_next,			\
								_ur,_ug,_ubvr,_vg,_vb,		\
								sampling,			\
								__depth__);			\
												\
					ublocks[blocknum(x_tm + i, y_tm + k, (yuv)->y_width, Y_UNIT_WIDTH)] = 1;	\
					vblocks[blocknum(x_tm + i, y_tm + k, (yuv)->y_width, Y_UNIT_WIDTH)] = 1;	\
				}								\
												\
				datapi += 2;							\
				datapi_back += 2;						\
				datapi_next += 2;						\
				datapi_back_next += 2;						\
												\
				yuv_U++;							\
				yuv_V++;							\
			}									\
												\
			yuv_U += ((yuv)->y_stride - (width_tm - w_odd * 2)) >> 1;		\
			yuv_V += ((yuv)->y_stride - (width_tm - w_odd * 2)) >> 1;		\
												\
			datapi += width_tm + w_odd;						\
			datapi_back += width_tm + w_odd;					\
			datapi_next += width_tm + w_odd;					\
			datapi_back_next += width_tm + w_odd;					\
		}										\
	} else {										\
		for (int k = 0; k < height_tm - h_odd; k += 2) {				\
			for (int i = 0; i < width_tm - w_odd; i += 2) {				\
				if ((*datapi != *datapi_back)) {				\
					UPDATE_A_UV_PIXEL(	yuv_U,				\
								yuv_V,				\
								t_val,				\
								datapi,				\
								datapi_next,			\
								_ur, _ug, _ubvr, _vg, _vb,	\
								sampling,			\
								__depth__);			\
												\
					ublocks[blocknum(x_tm + i, y_tm + k, (yuv)->y_width, Y_UNIT_WIDTH)] = 1;	\
					vblocks[blocknum(x_tm + i, y_tm + k, (yuv)->y_width, Y_UNIT_WIDTH)] = 1;	\
				}								\
												\
				datapi += 2;							\
				datapi_back += 2;						\
												\
				yuv_U++;							\
				yuv_V++;							\
			}									\
												\
			yuv_U += ((yuv)->y_stride - (width_tm - w_odd * 2)) >> 1;		\
			yuv_V += ((yuv)->y_stride - (width_tm - w_odd * 2)) >> 1;		\
												\
			datapi += width_tm + w_odd;						\
			datapi_back += width_tm + w_odd;					\
		}										\
	}											\
}

void rmdUpdateYuvBuffer(	yuv_buffer *yuv,
				unsigned char *data,
				unsigned char *data_back,
				int x_tm,
				int y_tm,
				int width_tm,
				int height_tm,
				int sampling,
				int depth)
{
	if (data_back == NULL) {
		switch (depth) {
		case 24:
		case 32:
			UPDATE_Y_PLANE(data, x_tm, y_tm, width_tm, height_tm, yuv, 32);
			UPDATE_UV_PLANES(data, x_tm, y_tm, width_tm, height_tm, yuv, sampling, 32);
			break;
		case 16:
			UPDATE_Y_PLANE(data, x_tm, y_tm, width_tm, height_tm, yuv, 16);
			UPDATE_UV_PLANES(data, x_tm, y_tm, width_tm, height_tm, yuv, sampling, 16);
			break;
		default:
			assert(0);
		}
	} else {
		switch (depth) {
		case 24:
		case 32:
			UPDATE_Y_PLANE_DBUF(data, data_back, x_tm, y_tm, width_tm, height_tm, yuv, 32);
			UPDATE_UV_PLANES_DBUF(data, data_back, x_tm, y_tm, width_tm, height_tm, yuv, sampling, 32);
			break;
		case 16:
			UPDATE_Y_PLANE_DBUF(data, data_back, x_tm, y_tm, width_tm, height_tm, yuv, 16);
			UPDATE_UV_PLANES_DBUF(data, data_back, x_tm, y_tm, width_tm, height_tm, yuv, sampling, 16);
			break;
		default:
			assert(0);
		}
	}
}

void rmdDummyPointerToYuv(	yuv_buffer *yuv,
				unsigned char *data_tm,
				int x_tm,
				int y_tm,
				int width_tm,
				int height_tm,
				int x_offset,
				int y_offset,
				unsigned char no_pixel)
{
	int i, k, j = 0;
	int x_2 = x_tm / 2, y_2 = y_tm / 2, y_width_2 = yuv->y_width/2;

	for (k = y_offset; k < y_offset + height_tm; k++) {
		for (i = x_offset; i < x_offset + width_tm; i++) {
			j = k * 16 + i;

			if (data_tm[j * 4] != no_pixel) {
				yuv->y[x_tm + (i - x_offset) + ((k - y_offset) + y_tm) * yuv->y_width] =
					Yr[data_tm[j * 4 + __RBYTE]] +
					Yg[data_tm[j * 4 + __GBYTE]] +
					Yb[data_tm[j * 4 + __BBYTE]];

				if ((k % 2) && (i % 2)) {
					yuv->u[x_2 + (i - x_offset) / 2 + ((k - y_offset) / 2 + y_2) * y_width_2] =
						Ur[data_tm[(k * width_tm + i) * 4 + __RBYTE]] +
						Ug[data_tm[(k * width_tm + i) * 4 + __GBYTE]] +
						UbVr[data_tm[(k * width_tm + i) * 4 + __BBYTE]];
					yuv->v[x_2 + (i - x_offset) / 2 + ((k - y_offset) / 2 + y_2) * y_width_2] =
						UbVr[data_tm[(k * width_tm + i) * 4 + __RBYTE]] +
						Vg[data_tm[(k * width_tm + i) * 4 + __GBYTE]] +
						Vb[data_tm[(k * width_tm + i) * 4 + __BBYTE]] ;
				}
			}
		}
	}
}

static inline unsigned char avg_4_pixels(	unsigned char *data_array,
						int width_img,
						int k_tm,
						int i_tm,
						int offset)
{
    return 	((data_array[(k_tm*width_img+i_tm)*RMD_ULONG_SIZE_T+offset]+
		data_array[((k_tm-1)*width_img+i_tm)*RMD_ULONG_SIZE_T+offset]+
		data_array[(k_tm*width_img+i_tm-1)*RMD_ULONG_SIZE_T+offset]+
		data_array[((k_tm-1)*width_img+i_tm-1)*RMD_ULONG_SIZE_T+offset])/4);
}

void rmdXFixesPointerToYuv(	yuv_buffer *yuv,
				unsigned char *data_tm,
				int x_tm,
				int y_tm,
				int width_tm,
				int height_tm,
				int x_offset,
				int y_offset,
				int column_discard_stride)
{
	unsigned char	avg0, avg1, avg2, avg3;
	int		x_2 = x_tm / 2, y_2 = y_tm / 2;

	for (int k = y_offset; k < y_offset + height_tm; k++) {
		for (int i = x_offset;i < x_offset + width_tm; i++) {
			int	j = k * (width_tm + column_discard_stride) + i;

			yuv->y[x_tm + (i - x_offset) + (k + y_tm - y_offset) * yuv->y_width] =
				(yuv->y[x_tm + (i - x_offset) + (k - y_offset + y_tm) * yuv->y_width] *
				(UCHAR_MAX - data_tm[(j * RMD_ULONG_SIZE_T) + __ABYTE]) +
				( ( Yr[data_tm[(j * RMD_ULONG_SIZE_T) + __RBYTE]] +
					Yg[data_tm[(j * RMD_ULONG_SIZE_T) + __GBYTE]] +
					Yb[data_tm[(j * RMD_ULONG_SIZE_T) + __BBYTE]] ) %
				  ( UCHAR_MAX + 1 ) ) *
				data_tm[(j * RMD_ULONG_SIZE_T) + __ABYTE]) / UCHAR_MAX;

			if ((k % 2) && (i % 2)) {
				int	idx = x_2 + (i - x_offset) / 2 + ((k - y_offset) / 2 + y_2) * yuv->uv_width;

				avg3 = avg_4_pixels(	data_tm,
							(width_tm + column_discard_stride),
							k, i, __ABYTE);
				avg2 = avg_4_pixels(	data_tm,
							(width_tm + column_discard_stride),
							k, i, __RBYTE);
				avg1 = avg_4_pixels(	data_tm,
							(width_tm + column_discard_stride),
							k, i, __GBYTE);
				avg0 = avg_4_pixels(	data_tm,
							(width_tm + column_discard_stride),
							k, i, __BBYTE);

				yuv->u[idx] =
					(yuv->u[idx] * (UCHAR_MAX - avg3) +
					((Ur[avg2] + Ug[avg1] + UbVr[avg0]) % (UCHAR_MAX + 1))
					* avg3) / UCHAR_MAX;

				yuv->v[idx]=
					(yuv->u[idx] * (UCHAR_MAX - avg3) +
					((UbVr[avg2] + Vg[avg1] + Vb[avg0]) % (UCHAR_MAX + 1))
					* avg3) / UCHAR_MAX;
			}
		}
	}
}
