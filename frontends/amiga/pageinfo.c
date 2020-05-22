/*
 * Copyright 2020 Chris Young <chris@unsatisfactorysoftware.co.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Amiga implementation of page info using core windows.
 */

#include <stdint.h>
#include <stdlib.h>

#include <proto/intuition.h>

#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/scroller.h>
#include <gadgets/space.h>
#include <images/label.h>

#include <intuition/icclass.h>
#include <reaction/reaction_macros.h>

#include "utils/log.h"
#include "netsurf/keypress.h"
#include "netsurf/plotters.h"
#include "desktop/page-info.h"
#include "utils/messages.h"
#include "utils/nsoption.h"

#include "amiga/corewindow.h"
#include "amiga/libs.h"
#include "amiga/pageinfo.h"
#include "amiga/schedule.h"
#include "amiga/utf8.h"


/**
 * Amiga page info window context
 */
struct ami_pageinfo_window {
	/** Amiga core window context */
	struct ami_corewindow core;
	/** core pageinfo */
	struct page_info *pi;
};

/**
 * destroy a previously created pageinfo window
 */
static void
ami_pageinfo_destroy(struct ami_corewindow *ami_cw)
{
	nserror res;
	struct ami_pageinfo_window *pageinfo_win = (struct ami_pageinfo_window *)ami_cw;
	res = page_info_destroy(pageinfo_win->pi);
	if (res == NSERROR_OK) {
		ami_corewindow_fini(&pageinfo_win->core); /* closes the window for us */
	}
}

/**
 * close pageinfo window (callback)
 */
static void
ami_pageinfo_close_cb(void *p)
{
	ami_pageinfo_destroy((struct ami_corewindow *)p);
}

/**
 * callback for unknown events on Amiga core window
 * (result & WMHI_CLASSMASK) gives the class of event (eg. WMHI_GADGETUP)
 * (result & WMHI_GADGETMASK) gives the gadget ID (eg. GID_SSLCERT_ACCEPT)
 *
 * \param ami_cw The Amiga core window structure.
 * \param result event as returned by RA_HandleInput()
 * \return TRUE if window closed during event processing
 */
static BOOL
ami_pageinfo_event(struct ami_corewindow *ami_cw, ULONG result)
{
	/* we don't have any events on this window yet */
	return FALSE;
}

/**
 * callback for mouse action for pageinfo on core window
 *
 * \param ami_cw The Amiga core window structure.
 * \param mouse_state netsurf mouse state on event
 * \param x location of event
 * \param y location of event
 * \return NSERROR_OK on success otherwise apropriate error code
 */
static nserror
ami_pageinfo_mouse(struct ami_corewindow *ami_cw,
					browser_mouse_state mouse_state,
					int x, int y)
{
	bool did_something = false;
	struct ami_pageinfo_window *pageinfo_win = (struct ami_pageinfo_window *)ami_cw;

	if(page_info_mouse_action(pageinfo_win->pi, mouse_state, x, y, &did_something) == NSERROR_OK)
		if (did_something == true) {
			/* Something happened so we need to close ourselves */
			ami_schedule(0, ami_pageinfo_close_cb, pageinfo_win);
		}
		
	return NSERROR_OK;
}

/**
 * callback for keypress for pageinfo on core window
 *
 * \param ami_cw The Amiga core window structure.
 * \param nskey The netsurf key code
 * \return NSERROR_OK on success otherwise apropriate error code
 */
static nserror
ami_pageinfo_key(struct ami_corewindow *ami_cw, uint32_t nskey)
{
	struct ami_pageinfo_window *pageinfo_win = (struct ami_pageinfo_window *)ami_cw;

	if (page_info_keypress(pageinfo_win->pi, nskey)) {
			return NSERROR_OK;
	}
	return NSERROR_NOT_IMPLEMENTED;
}

/**
 * callback on draw event for pageinfo on core window
 *
 * \param ami_cw The Amiga core window structure.
 * \param x the x coordinate to draw
 * \param y the y coordinate to draw
 * \param r The rectangle of the window that needs updating.
 * \param ctx The drawing context
 * \return NSERROR_OK on success otherwise apropriate error code
 */
static nserror
ami_pageinfo_draw(struct ami_corewindow *ami_cw, int x, int y, struct rect *r, struct redraw_context *ctx)
{
	struct ami_pageinfo_window *pageinfo_win = (struct ami_pageinfo_window *)ami_cw;

	page_info_redraw(pageinfo_win->pi, x, y, r, ctx);

	return NSERROR_OK;
}

static nserror
ami_pageinfo_create_window(struct ami_pageinfo_window *pageinfo_win)
{
	struct ami_corewindow *ami_cw = (struct ami_corewindow *)&pageinfo_win->core;
	ULONG refresh_mode = WA_SmartRefresh;
	struct Screen *scrn = ami_gui_get_screen();

	if(nsoption_bool(window_simple_refresh) == true) {
		refresh_mode = WA_SimpleRefresh;
	}

	ami_cw->objects[GID_CW_WIN] = WindowObj,
  	    WA_ScreenTitle, ami_gui_get_screen_title(),
       	WA_Title, ami_cw->wintitle,
       	WA_Activate, TRUE,
       	WA_DepthGadget, TRUE,
       	WA_DragBar, TRUE,
       	WA_CloseGadget, FALSE,
       	WA_SizeGadget, TRUE,
		WA_SizeBBottom, TRUE,
		WA_Height, scrn->Height / 2,
		WA_PubScreen, scrn,
		WA_ReportMouse, TRUE,
		refresh_mode, TRUE,
		WA_IDCMP, IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE |
				IDCMP_RAWKEY | IDCMP_IDCMPUPDATE |
				IDCMP_EXTENDEDMOUSE | IDCMP_SIZEVERIFY | IDCMP_REFRESHWINDOW,
		WINDOW_IDCMPHook, &ami_cw->idcmp_hook,
		WINDOW_IDCMPHookBits, IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE |
				IDCMP_SIZEVERIFY | IDCMP_REFRESHWINDOW,
		WINDOW_SharedPort, ami_gui_get_shared_msgport(),
		WINDOW_UserData, pageinfo_win,
		WINDOW_IconifyGadget, FALSE,
		WINDOW_Position, WPOS_CENTERSCREEN,
		WINDOW_ParentGroup, ami_cw->objects[GID_CW_MAIN] = LayoutVObj,
			LAYOUT_AddChild, ami_cw->objects[GID_CW_HSCROLLLAYOUT] = LayoutVObj,
				LAYOUT_AddChild, ami_cw->objects[GID_CW_VSCROLLLAYOUT] = LayoutHObj,
					LAYOUT_AddChild, ami_cw->objects[GID_CW_DRAW] = SpaceObj,
						GA_ID, GID_CW_DRAW,
						SPACE_Transparent, TRUE,
						SPACE_BevelStyle, BVS_DISPLAY,
						GA_RelVerify, TRUE,
		   			SpaceEnd,
					LAYOUT_AddChild, ami_cw->objects[GID_CW_VSCROLL] = ScrollerObj,
						GA_ID, GID_CW_VSCROLL,
						GA_RelVerify, TRUE,
						ICA_TARGET, ICTARGET_IDCMP,
		   			ScrollerEnd,
				LayoutEnd,
				LAYOUT_AddChild, ami_cw->objects[GID_CW_HSCROLL] = ScrollerObj,
					GA_ID, GID_CW_HSCROLL,
					GA_RelVerify, TRUE,
					ICA_TARGET, ICTARGET_IDCMP,
					SCROLLER_Orientation, SORIENT_HORIZ,
	   			ScrollerEnd,
			LayoutEnd,
			CHILD_WeightedHeight, 0,
		EndGroup,
	EndWindow;

	if(ami_cw->objects[GID_CW_WIN] == NULL) {
		return NSERROR_NOMEM;
	}

	return NSERROR_OK;
}

/* exported interface documented in amiga/pageinfo.h */
nserror ami_pageinfo_open(struct browser_window *bw)
{
	struct ami_pageinfo_window *ncwin;
	nserror res;

	ncwin = calloc(1, sizeof(struct ami_pageinfo_window));
	if (ncwin == NULL) {
		return NSERROR_NOMEM;
	}

	ncwin->core.wintitle = ami_utf8_easy((char *)messages_get("PageInfo"));

	res = ami_pageinfo_create_window(ncwin);
	if (res != NSERROR_OK) {
		NSLOG(netsurf, INFO, "Page info init failed");
		ami_utf8_free(ncwin->core.wintitle);
		free(ncwin);
		return res;
	}

	/* initialise Amiga core window */
	ncwin->core.draw = ami_pageinfo_draw;
	ncwin->core.key = ami_pageinfo_key;
	ncwin->core.mouse = ami_pageinfo_mouse;
	ncwin->core.close = ami_pageinfo_destroy;
	ncwin->core.event = ami_pageinfo_event;

	res = ami_corewindow_init(&ncwin->core);
	if (res != NSERROR_OK) {
		ami_utf8_free(ncwin->core.wintitle);
		DisposeObject(ncwin->core.objects[GID_CW_WIN]);
		free(ncwin);
		return res;
	}

	res = page_info_create(ncwin->core.cb_table,
					  (struct core_window *)ncwin,
					  bw,
					  &ncwin->pi);

	if (res != NSERROR_OK) {
		ami_utf8_free(ncwin->core.wintitle);
		DisposeObject(ncwin->core.objects[GID_CW_WIN]);
		free(ncwin);
		return res;
	}

	return NSERROR_OK;
}
