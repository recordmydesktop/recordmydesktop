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
#include "rmd_setbrwindow.h"

#include "rmd_types.h"


boolean rmdSetBRWindow(	Display *dpy,
			BRWindow *brwin,
			DisplaySpecs *specs,
			ProgArgs *args)
{
	//before we start recording we have to make sure the ranges are valid
	if (args->windowid == 0) {//root window
		//first set it up
		brwin->windowid		= specs->root;
		brwin->winrect.x	= 0;
		brwin->winrect.y	= 0;
		brwin->winrect.width	= specs->width;
		brwin->winrect.height	= specs->height;

		brwin->rrect.x	= args->x;
		brwin->rrect.y	= args->y;
		brwin->rrect.width	= args->width ? args->width : brwin->winrect.width - args->x;
		brwin->rrect.height	= args->height ? args->height : brwin->winrect.height - args->y;

		//and then check validity
		if (	brwin->rrect.x + brwin->rrect.width > specs->width ||
			brwin->rrect.y + brwin->rrect.height > specs->height) {

			fprintf(stderr,	"Window size specification out of bounds!"
					"(current resolution:%dx%d)\n",
					specs->width, specs->height);
			return FALSE;
		}
	} else {
		int			transl_x, transl_y;
		Window			wchid;
		XWindowAttributes	attribs;

		XGetWindowAttributes(dpy, args->windowid, &attribs);

		if (attribs.map_state == IsUnviewable || attribs.map_state == IsUnmapped) {
			fprintf(stderr, "Window must be mapped and visible!\n");
			return FALSE;
		}

		XTranslateCoordinates(	dpy,
					specs->root,
					args->windowid,
					attribs.x,
					attribs.y,
					&transl_x,
					&transl_y,
					&wchid);

		brwin->winrect.x	= attribs.x - transl_x;
		brwin->winrect.y	= attribs.y - transl_y;
		brwin->winrect.width	= attribs.width;
		brwin->winrect.height	= attribs.height;

		/* XXX FIXME: this check is partial at best, surely windows can be off the low
		 * sides of the screen too...
		 */
		if (	brwin->winrect.x + brwin->winrect.width > specs->width ||
			brwin->winrect.y + brwin->winrect.height > specs->height) {

			fprintf(stderr,"Window must be on visible screen area!\n");
			return FALSE;
		}

		brwin->rrect.x	= brwin->winrect.x + args->x;
		brwin->rrect.y	= brwin->winrect.y + args->y;
		brwin->rrect.width	= args->width ? args->width : brwin->winrect.width - args->x;
		brwin->rrect.height	= args->height ? args->height : brwin->winrect.height - args->y;

		if (	args->x + brwin->rrect.width > brwin->winrect.width ||
			args->y + brwin->rrect.height > brwin->winrect.height) {
			fprintf(stderr, "Specified Area is larger than window!\n");
			return FALSE;
		}
	}

	fprintf(stderr,	"Initial recording window is set to:\n"
			"X:%d   Y:%d	Width:%d	Height:%d\n",
			brwin->rrect.x, brwin->rrect.y,
			brwin->rrect.width, brwin->rrect.height);

	return TRUE;
}
