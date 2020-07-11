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
#include "rmd_poll_events.h"

#include "rmd_frame.h"
#include "rmd_macro.h"
#include "rmd_rectinsert.h"
#include "rmd_types.h"

#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xdamage.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>


static int clip_event_area(XDamageNotifyEvent *e, XRectangle *cliprect, XRectangle *res)
{

#if 0
	printf("got area %x,%x %x,%x\n",
		e->area.x,
		e->area.y,
		e->area.width,
		e->area.height);
#endif

	if (	e->area.x <= cliprect->x &&
		e->area.y <= cliprect->y &&
		e->area.width >= cliprect->width &&
		e->area.height >= cliprect->height) {

		/* area completely covers cliprect, cliprect becomes the area */
		res->x = cliprect->x;
		res->y = cliprect->y;
		res->width = cliprect->width;
		res->height = cliprect->height;

	} else if (	e->area.x + e->area.width < cliprect->x ||
			e->area.x + e->area.width > cliprect->x + cliprect->width ||
			e->area.y + e->area.height < cliprect->y ||
			e->area.y + e->area.height > cliprect->y + cliprect->height) {

		/* area has at least one axis with zero overlap, so they can't overlap */
		return 0;

	} else {

		/* areas partially overlap */
		res->x = MAX(e->area.x, cliprect->x);
		res->width = MIN(e->area.x + e->area.width, cliprect->x + cliprect->width) - res->x;

		res->y = MAX(e->area.y, cliprect->y);
		res->height = MIN(e->area.y + e->area.height, cliprect->y + cliprect->height) - res->y;

		if (!res->width || !res->height)
			return 0;
	}

#if 0
	printf("clipped to %x,%x %x,%x\n",
		res->x,
		res->y,
		res->width,
		res->height);
#endif

	return 1;
}


/* Try align xrect to even boundaries relative to cliprect,
 * this is done for the UV routines which operate at 2x2 rgb pixel granularity.
 */
static void uv_align(XRectangle *cliprect, XRectangle *xrect) {
	XRectangle	rel;

	rel.x = xrect->x - cliprect->x;
	rel.y = xrect->y - cliprect->y;

	if (rel.x % 2) {
		rel.x -= 1;
		rel.width = xrect->width + 1;
	} else {
		rel.width = xrect->width;
	}

	if (rel.y % 2) {
		rel.y -= 1;
		rel.height = xrect->height + 1;
	} else {
		rel.height = xrect->height;
	}

	/* XXX: note when cliprect prevents an even width, we can't do anything
	 * about it.  rmd could force even-sized recording windows to prevent
	 * this, but that would be kind of annoying.  For now it's assumed
	 * the UV routines will handle the exception - which might temporarily
	 * mean they just lop the odd row/column off the edge.
	 */
	if (rel.width % 2  && rel.width + rel.x < cliprect->x + cliprect->width)
		rel.width++;

	if (rel.height % 2 && rel.height + rel.y < cliprect->y + cliprect->height)
		rel.height++;
}


void rmdInitEventsPolling(ProgData *pdata) {
	Window root_return,
		   parent_return,
		   *children;
	unsigned int i,
				 nchildren;

	XSelectInput (pdata->dpy,pdata->specs.root, SubstructureNotifyMask);

	if (!pdata->args.full_shots) {
		XQueryTree (	pdata->dpy,
				pdata->specs.root,
				&root_return,
				&parent_return,
				&children,
				&nchildren);

		for (i = 0; i < nchildren; i++) {
			XWindowAttributes attribs;
			if (XGetWindowAttributes (pdata->dpy,children[i],&attribs)) {
				if (!attribs.override_redirect && attribs.depth==pdata->specs.depth)
					XDamageCreate(	pdata->dpy,
							children[i],
							XDamageReportRawRectangles);
			}
		}
		XFree(children);
		XDamageCreate(	pdata->dpy,
				pdata->specs.root,
				XDamageReportRawRectangles);
	}
}


void rmdEventLoop(ProgData *pdata) {
	int inserts = 0;

	XEvent event;

	while (XPending(pdata->dpy)) {
		XNextEvent(pdata->dpy,&event);
		if (event.type == KeyPress) {
			XKeyEvent *e = (XKeyEvent *)(&event);
			if (e->keycode == pdata->pause_key.key) {
				int i = 0;
				int found = 0;
				for (i=0; i < pdata->pause_key.modnum; i++) {
					if (pdata->pause_key.mask[i] == e->state) {
						found = 1;
						break;
					}
				}
				if (found) {
					raise(SIGUSR1);
					continue;
				}
			}
			if (e->keycode == pdata->stop_key.key) {
				int i = 0;
				int found = 0;
				for (i = 0; i < pdata->stop_key.modnum; i++) {
					if (pdata->stop_key.mask[i] == e->state) {
						found = 1;
						break;
					}
				}
				if (found) {
					raise(SIGINT);
					continue;
				}
			}
		} else if (event.type == Expose) {
			
			if (event.xexpose.count != 0)
				continue;
			else if (!pdata->args.noframe) {
				rmdDrawFrame(	pdata->dpy,
						pdata->specs.screen,  
						pdata->shaped_w,
						pdata->brwin.rrect.width,
						pdata->brwin.rrect.height);
			}

		} else if (!pdata->args.full_shots) {
			if (event.type == MapNotify ) {
				XWindowAttributes attribs;

				if (!((XMapEvent *)(&event))->override_redirect&&
					XGetWindowAttributes(	pdata->dpy,
								event.xcreatewindow.window,
								&attribs)) {

					if (!attribs.override_redirect && attribs.depth == pdata->specs.depth)
						XDamageCreate(	pdata->dpy,
								event.xcreatewindow.window,
								XDamageReportRawRectangles);
				}
			} else if (event.type == pdata->damage_event + XDamageNotify ) {
				XDamageNotifyEvent	*e = (XDamageNotifyEvent *)&event;
				XRectangle		xrect;

				if (clip_event_area(e, &pdata->brwin.rrect, &xrect)) {
					uv_align(&pdata->brwin.rrect, &xrect);
					inserts += rmdRectInsert(&pdata->rect_root, &xrect);
				}
			}
		}
	}
}
