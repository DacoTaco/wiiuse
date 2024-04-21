/*
 *	wiiuse
 *
 *	Written By:
 *		Michael Laforest	< para >
 *		Email: < thepara (--AT--) g m a i l [--DOT--] com >
 *
 *	Copyright 2006-2007
 *
 *	This file is part of wiiuse.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	$Header$
 *
 */

/**
 *	@file
 *	@brief Classic controller expansion device.
 */

#include "classic.h"
#include "dynamics.h" /* for calc_joystick_state */
#include "events.h"   /* for handshake_expansion */

#include <stdlib.h> /* for malloc */
#include <string.h> /* for memset */


static void fix_bad_calibration_values(struct joystick_t* js, short right_stick);
static void classic_ctrl_pressed_buttons(struct classic_ctrl_t* cc, short now);

/**
 *	@brief Handle the handshake data from the classic controller.
 *
 *	@param cc		A pointer to a classic_ctrl_t structure.
 *	@param data		The data read in from the device.
 *	@param len		The length of the data block, in bytes.
 *
 *	@return	Returns 1 if handshake was successful, 0 if not.
 */
#define HANDSHAKE_BYTES_USED 12
int classic_ctrl_handshake(struct wiimote_t *wm, struct classic_ctrl_t *cc, byte *data, unsigned short len)
{
	int offset 		  = 0;
    cc->btns          = 0;
    cc->btns_held     = 0;
    cc->btns_released = 0;
    cc->r_shoulder    = 0;
    cc->l_shoulder    = 0;
	
	/* is this a wiiu pro? */
	if (len > 223 && data[223] == 0x20) 
	{
		cc->ljs.max.x = cc->ljs.max.y = 208;
		cc->ljs.min.x = cc->ljs.min.y = 48;
		cc->ljs.center.x = cc->ljs.center.y = 0x80;
		cc->rjs = cc->ljs;
		cc->type = 2;
		
		goto done_handshake;
	}

    if (data[0] == 0xFF || len < HANDSHAKE_BYTES_USED)
    {
        /*
         *	Sometimes the data returned here is not correct.
         *	This might happen because the wiimote is lagging
         *	behind our initialization sequence.
         *	To fix this just request the handshake again.
         *
         *	Other times it's just the first 16 bytes are 0xFF,
         *	but since the next 16 bytes are the same, just use
         *	those.
         */
        if (len < 17 || len < HANDSHAKE_BYTES_USED + 16 || data[16] == 0xFF)
        {
            /* get the calibration data */
            byte *handshake_buf = (byte *)malloc(EXP_HANDSHAKE_LEN * sizeof(byte));

            WIIUSE_DEBUG("Classic controller handshake appears invalid, trying again.");
            wiiuse_read_data_cb(wm, handshake_expansion, handshake_buf, WM_EXP_MEM_CALIBR, EXP_HANDSHAKE_LEN);

            return 0;
        } else
        {
            offset += 16;
        }
    }
	
	/* classic controller pro (no analog triggers) or original classic controller (analog triggers) */
	cc->type = (len > 218 && data[218]);

    /* joystick stuff */
    cc->ljs.max.x    = data[0 + offset] / 4 == 0 ? 64 : data[0 + offset] / 4;
    cc->ljs.min.x    = data[1 + offset] / 4;
    cc->ljs.center.x = data[2 + offset] / 4 == 0 ? 32 : data[2 + offset] / 4;
    cc->ljs.max.y    = data[3 + offset] / 4 == 0 ? 64 : data[3 + offset] / 4;
    cc->ljs.min.y    = data[4 + offset] / 4;
    cc->ljs.center.y = data[5 + offset] / 4 == 0 ? 32 : data[5 + offset] / 4;

    cc->rjs.max.x    = data[6 + offset] / 8 == 0 ? 32 : data[6 + offset] / 8;
    cc->rjs.min.x    = data[7 + offset] / 8;
    cc->rjs.center.x = data[8 + offset] / 8 == 0 ? 16 : data[8 + offset] / 8;
    cc->rjs.max.y    = data[9 + offset] / 8 == 0 ? 32 : data[9 + offset] / 8;
    cc->rjs.min.y    = data[10 + offset] / 8;
    cc->rjs.center.y = data[11 + offset] / 8 == 0 ? 16 : data[11 + offset] / 8;
	
	fix_bad_calibration_values(&cc->ljs, 0);
	fix_bad_calibration_values(&cc->rjs, 1);

done_handshake:
    /* handshake done */
	wm->event = WIIUSE_CLASSIC_CTRL_INSERTED;
    wm->exp.type = EXP_CLASSIC;

#ifdef WIIUSE_WIN32
    wm->timeout = WIIMOTE_DEFAULT_TIMEOUT;
#endif

    return 1;
}

/**
 *	@brief The classic controller disconnected.
 *
 *	@param cc		A pointer to a classic_ctrl_t structure.
 */
void classic_ctrl_disconnected(struct classic_ctrl_t *cc) { memset(cc, 0, sizeof(struct classic_ctrl_t)); }

/**
 *	@brief Handle classic controller event.
 *
 *	@param cc		A pointer to a classic_ctrl_t structure.
 *	@param msg		The message specified in the event packet.
 */
void classic_ctrl_event(struct classic_ctrl_t *cc, byte *msg)
{
    int lx, ly, rx, ry;
    byte l, r;
	
	if (cc->type==2) {
#ifndef GEKKO
		classic_ctrl_pressed_buttons(cc, from_big_endian_uint16_t(msg + 8));
#else
		classic_ctrl_pressed_buttons(cc, BIG_ENDIAN_SHORT(*(short*)(msg + 8)));
#endif

		/* 12-bit little endian values adjusted to 8-bit */
		lx = (msg[0] >> 4) | (msg[1] << 4);
		rx = (msg[2] >> 4) | (msg[3] << 4);
		ly = (msg[4] >> 4) | (msg[5] << 4);
		ry = (msg[6] >> 4) | (msg[7] << 4);

		l = cc->btns & CLASSIC_CTRL_BUTTON_FULL_L ? 0x1F : 0;
		r = cc->btns & CLASSIC_CTRL_BUTTON_FULL_R ? 0x1F : 0;
		
		goto set_state;
	}
	else
	{
#ifndef GEKKO
		classic_ctrl_pressed_buttons(cc, from_big_endian_uint16_t(msg + 4));
#else
		classic_ctrl_pressed_buttons(cc, BIG_ENDIAN_SHORT(*(short*)(msg + 4)));
#endif

		/* left/right buttons */
		l = (((msg[2] & 0x60) >> 2) | ((msg[3] & 0xE0) >> 5));
		r = (msg[3] & 0x1F);

		/* calculate joystick orientation */
		lx = (msg[0] & 0x3F);
		ly = (msg[1] & 0x3F);
		rx = ((msg[0] & 0xC0) >> 3) | ((msg[1] & 0xC0) >> 5) | ((msg[2] & 0x80) >> 7);
		ry = (msg[2] & 0x1F);
	}
	
	/*
	 *	TODO - LR range hardcoded from 0x00 to 0x1F.
	 *	This is probably in the calibration somewhere.
	 */
	cc->ls_raw = l;
	cc->rs_raw = r;
	cc->r_shoulder = ((float)r / 0x1F);
	cc->l_shoulder = ((float)l / 0x1F);

set_state:
	calc_joystick_state(&cc->ljs, (float)lx, (float)ly);
    calc_joystick_state(&cc->rjs, (float)rx, (float)ry);
}

static void fix_bad_calibration_values(struct joystick_t* js, short right_stick) {
	if ((js->min.x >= js->center.x) || (js->max.x <= js->center.x)) {
		js->min.x = 0;
		js->max.x = right_stick ? 32 : 64;
		js->center.x = right_stick ? 16 : 32;
	}
	if ((js->min.y >= js->center.y) || (js->max.y <= js->center.y)) {
		js->min.y = 0;
		js->max.y = right_stick ? 32 : 64;
		js->center.y = right_stick ? 16 : 32;
	}
}

/**
 *	@brief Find what buttons are pressed.
 *
 *	@param cc		A pointer to a classic_ctrl_t structure.
 *	@param msg		The message byte specified in the event packet.
 */
static void classic_ctrl_pressed_buttons(struct classic_ctrl_t *cc, short now)
{
    /* message is inverted (0 is active, 1 is inactive) */
    now = ~now & CLASSIC_CTRL_BUTTON_ALL;

    /* pressed now & were pressed, then held */
    cc->btns_held = (now & cc->btns);

    /* were pressed or were held & not pressed now, then released */
    cc->btns_released = ((cc->btns | cc->btns_held) & ~now);

    /* buttons pressed now */
    cc->btns = now;
}
