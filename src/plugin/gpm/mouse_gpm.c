/*
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */


/* mouse support for GPM */

#include <gpm.h>
#include <fcntl.h>

#include "emu.h"
#include "video.h"
#include "mouse.h"
#include "utilities.h"
#include "init.h"
#include "vc.h"

static void gpm_getevent(int fd, void *arg)
{
	static unsigned char buttons;
	Gpm_Event ev;
	fd_set mfds;
	int type;
	struct timeval tv = {};

	FD_ZERO(&mfds);
	FD_SET(gpm_fd, &mfds);
	if (select(gpm_fd + 1, &mfds, NULL, NULL, &tv) <= 0)
		return;
	Gpm_GetEvent((void*)&ev);
//	ioselect_complete(gpm_fd);
	type = GPM_BARE_EVENTS(ev.type);
	m_printf("MOUSE: Get GPM Event, %d\n", type);
	switch (type) {
	case GPM_MOVE:
	case GPM_DRAG:
		mouse_move_absolute(ev.x - 1, ev.y - 1, gpm_mx, gpm_my, 1,
			MOUSE_GPM);
		if (ev.wdy)
			mouse_move_wheel(-ev.wdy, MOUSE_GPM);
		break;
	case GPM_UP:
		if (ev.buttons & GPM_B_LEFT) buttons &= ~GPM_B_LEFT;
		if (ev.buttons & GPM_B_MIDDLE) buttons &= ~GPM_B_MIDDLE;
		if (ev.buttons & GPM_B_RIGHT) buttons &= ~GPM_B_RIGHT;
		ev.buttons = buttons;
		/* fall through */
	case GPM_DOWN:
		mouse_move_buttons(!!(ev.buttons & GPM_B_LEFT),
				   !!(ev.buttons & GPM_B_MIDDLE),
				   !!(ev.buttons & GPM_B_RIGHT),
				   MOUSE_GPM);
		buttons = ev.buttons;
		/* fall through */
	default:
		break;
	}
}

static int gpm_init(void)
{
	int fd, ret;
	mouse_t *mice = &config.mouse;
	Gpm_Connect conn;

	if (config.vga || !on_console())
		return FALSE;

	conn.eventMask	 = ~0;
	conn.defaultMask = GPM_MOVE;
	conn.minMod	 = 0;
	conn.maxMod	 = 0;

	fd = Gpm_Open(&conn, 0);
	if (fd < 0)
		return FALSE;

	mice->type = MOUSE_GPM;
	ret = fcntl(fd, F_GETFL);
	if (ret == -1) {
		m_printf("GPM MOUSE: fcntl F_GETFL failed\n");
		Gpm_Close();
		return FALSE;
	}
	ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
	if (ret == -1) {
		m_printf("GPM MOUSE: fcntl F_SETFL failed\n");
		Gpm_Close();
		return FALSE;
	}

	add_to_io_select_threaded(fd, gpm_getevent, NULL);
	m_printf("GPM MOUSE: Using GPM Mouse\n");
	return TRUE;
}

static void gpm_close(void)
{
	remove_from_io_select(gpm_fd);
	Gpm_Close();
	m_printf("GPM MOUSE: Mouse tracking deinitialized\n");
}

static struct mouse_client Mouse_gpm = {
	"gpm",		/* name */
	gpm_init,	/* init */
	gpm_close,	/* close */
	NULL		/* set_cursor */
};

CONSTRUCTOR(static void init(void))
{
	register_mouse_client(&Mouse_gpm);
}
