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
 *	@brief Handles device I/O (non-OS specific).
 */

#include "io.h"
#include "events.h" /* for propagate_event */
#include "ir.h"     /* for wiiuse_set_ir_mode */
#include "wiiuse_internal.h"

#include "os.h" /* for wiiuse_os_* */

#include <stdio.h>
#include <stdlib.h> /* for free, malloc */
#include <string.h>
#ifdef WIIUSE_GEKKO
#endif

/**
 *  @brief Find a wiimote or wiimotes.
 *
 *  @param wm     An array of wiimote_t structures.
 *  @param max_wiimotes The number of wiimote structures in \a wm.
 *  @param timeout    The number of seconds before the search times out.
 *
 *  @return The number of wiimotes found.
 *
 *  @see wiiuse_connect()
 *  @see wiiuse_os_find()
 *
 *  This function will only look for wiimote devices.           \n
 *  When a device is found the address in the structures will be set.   \n
 *  You can then call wiiuse_connect() to connect to the found       \n
 *  devices.
 *
 *  This function only delegates to the platform-specific implementation
 *  wiiuse_os_find.
 *
 *  This function is declared in wiiuse.h
 */
int wiiuse_find(struct wiimote_t **wm, int max_wiimotes, int timeout)
{
    return wiiuse_os_find(wm, max_wiimotes, timeout);
}

/**
 *  @brief Connect to a wiimote or wiimotes once an address is known.
 *
 *  @param wm     An array of wiimote_t structures.
 *  @param wiimotes   The number of wiimote structures in \a wm.
 *
 *  @return The number of wiimotes that successfully connected.
 *
 *  @see wiiuse_find()
 *  @see wiiuse_disconnect()
 *  @see wiiuse_os_connect()
 *
 *  Connect to a number of wiimotes when the address is already set
 *  in the wiimote_t structures.  These addresses are normally set
 *  by the wiiuse_find() function, but can also be set manually.
 *
 *  This function only delegates to the platform-specific implementation
 *  wiiuse_os_connect.
 *
 *  This function is declared in wiiuse.h
 */
int wiiuse_connect(struct wiimote_t **wm, int wiimotes) { return wiiuse_os_connect(wm, wiimotes); }

/**
 *  @brief Disconnect a wiimote.
 *
 *  @param wm   Pointer to a wiimote_t structure.
 *
 *  @see wiiuse_connect()
 *  @see wiiuse_os_disconnect()
 *
 *  Note that this will not free the wiimote structure.
 *
 *  This function only delegates to the platform-specific implementation
 *  wiiuse_os_disconnect.
 *
 *  This function is declared in wiiuse.h
 */
void wiiuse_disconnect(struct wiimote_t *wm) { wiiuse_os_disconnect(wm); }

/**
*    @brief Wait until specified report arrives and return it
*
*    @param wm             Pointer to a wiimote_t structure.
*    @param buffer         Pre-allocated memory to store the received data
*    @param bufferLength   size of buffer in bytes
*    @param timeout_ms     timeout in ms, 0 = wait forever
*
*    Synchronous/blocking, this function will not return until it receives the specified
*    report from the Wiimote or timeout occurs.
*
*    Returns 1 on success, -1 on failure.
*
*/
int wiiuse_wait_report(struct wiimote_t *wm, int report, byte *buffer, int bufferLength,
                       unsigned long timeout_ms)
{

    int result            = 1;
    unsigned long elapsed = 0;
    unsigned long start   = wiiuse_os_ticks();

    for (;;)
    {
        if (wiiuse_os_read(wm, buffer, bufferLength) > 0)
        {
            if (buffer[0] == report)
            {
                break;
            } else
            {
                if (buffer[0] != 0x30) /* hack for chatty devices spamming the button report */
                {
                    WIIUSE_DEBUG("(id %i) dropping report 0x%x, waiting for 0x%x", wm->unid, buffer[0],
                                 report);
                }
            }
        }

        elapsed = wiiuse_os_ticks() - start;
        if (elapsed > timeout_ms && timeout_ms > 0)
        {
            result = -1;
            WIIUSE_DEBUG("(id %i) timeout waiting for report 0x%x, aborting!", wm->unid, report);
            break;
        }
        wiiuse_millisleep(10);
    }

    return result;
}

/**
*    @brief Read memory/register data synchronously
*
*    @param wm        Pointer to a wiimote_t structure.
*    @param memory    If set to non-zero, reads EEPROM, otherwise registers
*    @param addr      Address offset to read from
*    @param size      How many bytes to read
*    @param data      Pre-allocated memory to store the received data
*
*    Synchronous/blocking read, this function will not return until it receives the specified
*    amount of data from the Wiimote.
*
*/
void wiiuse_read_data_sync(struct wiimote_t *wm, byte memory, unsigned addr, unsigned short size, byte *data)
{
    byte pkt[6];
    byte buf[MAX_PAYLOAD];
    unsigned n_full_reports;
    unsigned last_report;
    byte *output;
    unsigned int i;
    int done = 0;

    /*
     * address in big endian first, the leading byte will
     * be overwritten (only 3 bytes are sent)
     */
    to_big_endian_uint32_t(pkt, addr);

    /* read from registers or memory */
    pkt[0] = (memory != 0) ? 0x00 : 0x04;

    /* length in big endian */
    to_big_endian_uint16_t(pkt + 4, size);

    done = 0;
    while (!done)
    {
        /* send */
        wiiuse_send(wm, WM_CMD_READ_DATA, pkt, sizeof(pkt));

        /* calculate how many 16B packets we have to get back */
        n_full_reports = size / 16;
        last_report    = size % 16;
        output         = data;

        for (i = 0; i < n_full_reports; ++i)
        {
            int rc = wiiuse_wait_report(wm, WM_RPT_READ, buf, MAX_PAYLOAD, WIIUSE_READ_TIMEOUT);

            if (rc < 0)
                /* oops, time out, abort and retry */
                break;

            memmove(output, buf + 6, 16);
            output += 16;
        }

        /* read the last incomplete packet */
        if (last_report)
        {
            int rc = wiiuse_wait_report(wm, WM_RPT_READ, buf, MAX_PAYLOAD, WIIUSE_READ_TIMEOUT);

            if (rc)
                done = 1;

            memmove(output, buf + 6, last_report);
        } else
            done = 1;
    }
}

/**
 *	@brief Get initialization data from the wiimote.
 *
 *	@param wm		Pointer to a wiimote_t structure.
 *	@param data		unused
 *	@param len		unused
 *
 *	When first called for a wiimote_t structure, a request
 *	is sent to the wiimote for initialization information.
 *	This includes factory set accelerometer data.
 *	The handshake will be concluded when the wiimote responds
 *	with this data.
 */

#ifdef WIIUSE_SYNC_HANDSHAKE

void wiiuse_handshake(struct wiimote_t *wm, byte *data, uint16_t len)
{
    /* send request to wiimote for accelerometer calibration */
    byte buf[MAX_PAYLOAD];
    int i;

    /* step 0 - Reset wiimote */
    {
        // wiiuse_set_leds(wm, WIIMOTE_LED_NONE);

        WIIMOTE_ENABLE_STATE(wm, WIIMOTE_STATE_HANDSHAKE);
        WIIMOTE_ENABLE_STATE(wm, WIIMOTE_STATE_CONNECTED);
        WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_ACC);
        WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_IR);
        WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_RUMBLE);
        WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_EXP);
        WIIMOTE_DISABLE_FLAG(wm, WIIUSE_CONTINUOUS);

        wiiuse_set_report_type(wm);
        wiiuse_millisleep(500);

        /*
          Ensure MP is off, because it will screw up the expansion handshake otherwise.
          We cannot rely on the Wiimote having been powercycled between uses
          because Windows/Mayflash Dolphin Bar and even Linux now allow pairing
          it permanently - thus it remains on and connected between the application
          starts and in an unknown state when we arrive here => problem.

          This won't affect regular expansions (Nunchuck) if MP is not present,
          they get initialized twice in the worst case, which is harmless.
        */

        byte val = 0x55;
        wiiuse_write_data(wm, WM_EXP_MEM_ENABLE1, &val, 1);

        WIIUSE_DEBUG("Wiimote reset!\n");
    }

    /* step 1 - calibration of accelerometers */
    {
        struct accel_t *accel = &wm->accel_calib;

        wiiuse_read_data_sync(wm, 1, WM_MEM_OFFSET_CALIBRATION, 8, buf);

        /* received read data */
        accel->cal_zero.x = buf[0];
        accel->cal_zero.y = buf[1];
        accel->cal_zero.z = buf[2];

        accel->cal_g.x = buf[4] - accel->cal_zero.x;
        accel->cal_g.y = buf[5] - accel->cal_zero.y;
        accel->cal_g.z = buf[6] - accel->cal_zero.z;

        WIIUSE_DEBUG("Calibrated wiimote acc\n");
    }

    /* step 2 - re-enable IR and ask for status */
    {
        WIIMOTE_ENABLE_STATE(wm, WIIMOTE_STATE_HANDSHAKE_COMPLETE);
        WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_HANDSHAKE);

        /* now enable IR if it was set before the handshake completed */
        if (WIIMOTE_IS_SET(wm, WIIMOTE_STATE_IR))
        {
            WIIUSE_DEBUG("Handshake finished, enabling IR.");
            WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_IR);
            wiiuse_set_ir(wm, 1);
        }

        /*
         * try to ask for status 3 times, sometimes the first one gives bad data
         * and doesn't show expansions
         */
        for (i = 0; i < 3; ++i)
        {
            int rc = 0;

            WIIUSE_DEBUG("Asking for status, attempt %d ...\n", i);
            wm->event = WIIUSE_CONNECT;

            wiiuse_status(wm);
            rc = wiiuse_wait_report(wm, WM_RPT_CTRL_STATUS, buf, MAX_PAYLOAD, WIIUSE_READ_TIMEOUT);

            if (rc && buf[3] != 0)
                break;
        }
        propagate_event(wm, WM_RPT_CTRL_STATUS, buf + 1);
    }
}

#else

static void wiiuse_disable_motion_plus1(struct wiimote_t *wm, byte *data, unsigned short len);
static void wiiuse_disable_motion_plus2(struct wiimote_t *wm, byte *data, unsigned short len);

void wiiuse_handshake(struct wiimote_t *wm, byte *data, uint16_t len)
{
    if (!wm)
    {
        return;
    }

    ubyte *buf = NULL;
    struct accel_t *accel = &wm->accel_calib;
	//printf("wiiuse_handshake(%d,%p,%d)\n",wm->handshake_state,data,len);
    switch (wm->handshake_state)
    {
    case 0:
    {
        wm->handshake_state++;

        wiiuse_set_leds(wm,WIIMOTE_LED_NONE,NULL);
        wiiuse_status(wm,wiiuse_handshake);
        return;

    case 1:
    {
        wm->handshake_state++;
#ifdef WIIUSE_GEKKO
        buf = malloc(sizeof(ubyte)*8);
#else
		buf = malloc(sizeof(ubyte)*8);
#endif

        if (len > 2 && data[2]&WM_CTRL_STATUS_BYTE1_ATTACHMENT) {
            wiiuse_read_data(wm,buf,WM_EXP_ID,6,wiiuse_handshake);
            return;
        }
		
		wm->handshake_state++;
        wiiuse_read_data(wm,buf,WM_MEM_OFFSET_CALIBRATION,7,wiiuse_handshake);
    case 2:
    {
        if (BIG_ENDIAN_LONG(*(int*)(&data[2])) == EXP_ID_CODE_CLASSIC_WIIU_PRO) {
            memset(data, 0, 8);
            WIIMOTE_ENABLE_STATE(wm, WIIMOTE_STATE_WIIU_PRO);
            break;
        }
        buf = data;

        wm->handshake_state++;
        wiiuse_read_data(wm,buf,WM_MEM_OFFSET_CALIBRATION,7,wiiuse_handshake);
        return;
    }
	default:
    {
        break;
    }
    }
	
    accel->cal_zero.x = ((data[0]<<2)|((data[3]>>4)&3));
    accel->cal_zero.y = ((data[1]<<2)|((data[3]>>2)&3));
    accel->cal_zero.z = ((data[2]<<2)|(data[3]&3));

    accel->cal_g.x = (((data[4]<<2)|((data[7]>>4)&3)) - accel->cal_zero.x);
    accel->cal_g.y = (((data[5]<<2)|((data[7]>>2)&3)) - accel->cal_zero.y);
    accel->cal_g.z = (((data[6]<<2)|(data[7]&3)) - accel->cal_zero.z);
#ifdef WIIUSE_GEKKO
	free(data);
#else
	free(data);
#endif

    WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_HANDSHAKE);
    WIIMOTE_ENABLE_STATE(wm, WIIMOTE_STATE_HANDSHAKE_COMPLETE);

    wm->event = WIIUSE_CONNECT;
    wiiuse_status(wm,NULL);
}

void wiiuse_handshake_expansion_start(struct wiimote_t *wm)
{
	if(WIIMOTE_IS_SET(wm,WIIMOTE_STATE_EXP) || WIIMOTE_IS_SET(wm,WIIMOTE_STATE_EXP_FAILED) || WIIMOTE_IS_SET(wm,WIIMOTE_STATE_EXP_HANDSHAKE))
		return;

	wm->expansion_state = 0;
	WIIMOTE_ENABLE_STATE(wm, WIIMOTE_STATE_EXP_HANDSHAKE);
	wiiuse_handshake_expansion(wm, NULL, 0);
}

void wiiuse_handshake_expansion(struct wiimote_t *wm, byte *data, uint16_t len)
{
	int id;
	ubyte val;
	ubyte *buf = NULL;

	switch(wm->expansion_state) {
		/* These two initialization writes disable the encryption */
		case 0:
			wm->expansion_state = 1;
			val = 0x55;
			wiiuse_write_data(wm,WM_EXP_MEM_ENABLE1,&val,1,wiiuse_handshake_expansion);
			break;
		case 1:
			wm->expansion_state = 2;
			val = 0x00;
			wiiuse_write_data(wm,WM_EXP_MEM_ENABLE2,&val,1,wiiuse_handshake_expansion);
			break;
		case 2:
			wm->expansion_state = 3;
#ifdef WIIUSE_GEKKO
			buf = malloc(sizeof(ubyte)*EXP_HANDSHAKE_LEN);
#else
			buf = malloc(sizeof(ubyte)*EXP_HANDSHAKE_LEN);
#endif
			wiiuse_read_data(wm,buf,WM_EXP_MEM_CALIBR,EXP_HANDSHAKE_LEN,wiiuse_handshake_expansion);
			break;
		case 3:
			if(!data || !len) return;
			id = BIG_ENDIAN_LONG(*(int*)(&data[220]));

			switch(id) {
				case EXP_ID_CODE_NUNCHUK:
					if(!nunchuk_handshake(wm,&wm->exp.nunchuk,data,len)) return;
					break;
				case EXP_ID_CODE_CLASSIC_CONTROLLER:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_NYKOWING:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_NYKOWING2:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_NYKOWING3:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_GENERIC:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_GENERIC2:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_GENERIC3:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_GENERIC4:
				case EXP_ID_CODE_CLASSIC_CONTROLLER_GENERIC5:
				case EXP_ID_CODE_CLASSIC_WIIU_PRO:
					if(!classic_ctrl_handshake(wm,&wm->exp.classic,data,len)) return;
					break;
				case EXP_ID_CODE_GUITAR:
					if(!guitar_hero_3_handshake(wm,&wm->exp.gh3,data,len)) return;
					break;
 				case EXP_ID_CODE_WIIBOARD:
 					if(!wii_board_handshake(wm,&wm->exp.wb,data,len)) return;
 					break;
				default:
					if(!classic_ctrl_handshake(wm,&wm->exp.classic,data,len)) return;
					/*WIIMOTE_DISABLE_STATE(wm,WIIMOTE_STATE_EXP_HANDSHAKE);
					WIIMOTE_ENABLE_STATE(wm,WIIMOTE_STATE_EXP_FAILED);
					free(data);
					wiiuse_status(wm,NULL);
					return;*/
			}
#ifdef WIIUSE_GEKKO
			free(data);
#else
			free(data);
#endif

			WIIMOTE_DISABLE_STATE(wm,WIIMOTE_STATE_EXP_HANDSHAKE);
			WIIMOTE_ENABLE_STATE(wm,WIIMOTE_STATE_EXP);
			wiiuse_set_ir_mode(wm);
			wiiuse_status(wm,NULL);
			break;
	}
}

void wiiuse_disable_expansion(struct wiimote_t *wm)
{
	if(!WIIMOTE_IS_SET(wm, WIIMOTE_STATE_EXP)) return;

	/* tell the associated module the expansion was removed */
	switch(wm->exp.type) {
		case EXP_NUNCHUK:
			nunchuk_disconnected(&wm->exp.nunchuk);
			wm->event = WIIUSE_NUNCHUK_REMOVED;
			break;
		case EXP_CLASSIC:
			classic_ctrl_disconnected(&wm->exp.classic);
			wm->event = WIIUSE_CLASSIC_CTRL_REMOVED;
			break;
		case EXP_GUITAR_HERO_3:
			guitar_hero_3_disconnected(&wm->exp.gh3);
			wm->event = WIIUSE_GUITAR_HERO_3_CTRL_REMOVED;
			break;
 		case EXP_WII_BOARD:
 			wii_board_disconnected(&wm->exp.wb);
 			wm->event = WIIUSE_WII_BOARD_REMOVED;
 			break;
		case EXP_MOTION_PLUS:
 			motion_plus_disconnected(&wm->exp.mp);
 			wm->event = WIIUSE_MOTION_PLUS_REMOVED;
 			break;

		default:
			break;
	}

	WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_EXP);
	wm->exp.type = EXP_NONE;

	wiiuse_set_ir_mode(wm);
	wiiuse_status(wm,NULL);
}

static void wiiuse_disable_motion_plus1(struct wiimote_t *wm, byte *data, unsigned short len)
{
    byte val = 0x55;
    wiiuse_write_data_cb(wm, WM_EXP_MEM_ENABLE1, &val, 1, wiiuse_disable_motion_plus2);
}

static void wiiuse_disable_motion_plus2(struct wiimote_t *wm, byte *data, unsigned short len)
{
    WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_EXP_FAILED);
    WIIMOTE_DISABLE_STATE(wm, WIIMOTE_STATE_EXP_HANDSHAKE);
    wiiuse_set_ir_mode(wm);

    wm->handshake_state++;
    wiiuse_handshake(wm, NULL, 0);
}

#endif
