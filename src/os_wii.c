
/**
 *	@file
 *	@brief Handles device I/O for Nintendo Wii.
 */

#ifdef WIIUSE_GEKKO

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>   /* for struct timeval */
#include <time.h>       /* for clock_gettime */

#include <lwp_wkspace.inl>

#include "definitions.h"
#include "wiiuse_internal.h"
#include "events.h"
#include "io.h"
#include "os.h"

static s32 __bte_receive(void *arg, void *buffer, u16 len)
{
	struct wiimote_t *wm = (struct wiimote_t*)arg;
	return wiiuse_os_read(wm, buffer, len);
}

static s32 __bte_disconnected(void *arg,struct bte_pcb *pcb,u8 err)
{
	struct wiimote_t *wm = (struct wiimote_t*)arg;
	wiiuse_os_disconnect(wm);
	return 0;
}

static s32 __bte_connected(void *arg, struct bte_pcb *pcb, u8 err)
{
	struct wiimote_t *wm = (struct wiimote_t*)arg;
	return wiiuse_os_connect_single(wm);
}

static void __set_platform_fields(struct wiimote_t *wm, wii_event_cb event_cb)
{
	wm->sock = NULL;
	wm->bdaddr = *BD_ADDR_ANY;
	wm->event_cb = event_cb;
}
void wiiuse_init_platform_fields(struct wiimote_t *wm, wii_event_cb event_cb)
{
	__set_platform_fields(wm, cb);
	bte_arg(wml->sock, wm);
	bte_received(wml->sock, __bte_receive);
	bte_disconnected(wml->sock, __bte_disconnected);
	bte_registerdeviceasync(wml->sock, bdaddr, __wiiuse_connected);
}
void wiiuse_cleanup_platform_fields(struct wiimote_t *wm) { __set_platform_fields(wm, NULL); }
int wiiuse_os_find(struct wiimote_t **wm, int max_wiimotes, int timeout){ return 0; }

int wiiuse_os_connect_single(struct wiimote_t *wm)
{
	if(!wm)
		return 0;
	
	//printf("__wiiuse_connected()\n");
	WIIMOTE_ENABLE_STATE(wm,(WIIMOTE_STATE_CONNECTED|WIIMOTE_STATE_HANDSHAKE));

	wm->handshake_state = 0;
	wiiuse_handshake(wm, NULL, 0);
	
	return 1;
}

int wiiuse_os_connect(struct wiimote_t **wm, int wiimotes)
{
	if(!wm)
	{
		return ERR_OK;
	}
	
	int connected = 0;
    for (int i = 0; i < wiimotes; ++i)
    {
		/* if the device address is not set, skip it */
        if (!WIIMOTE_IS_SET(wm[i], WIIMOTE_STATE_DEV_FOUND))
        {
            continue;
        }

        if (wiiuse_os_connect_single(wm[i]))
        {
            ++connected;
        }
    }

    return connected;
}

void wiiuse_os_disconnect(struct wiimote_t *wm)
{
	if (!wm || WIIMOTE_IS_CONNECTED(wm))
    {
        return;
    }

	//printf("wiimote disconnected\n");
	WIIMOTE_DISABLE_STATE(wm, (WIIMOTE_STATE_IR|WIIMOTE_STATE_IR_INIT));
	WIIMOTE_DISABLE_STATE(wm, (WIIMOTE_STATE_SPEAKER|WIIMOTE_STATE_SPEAKER_INIT));
	WIIMOTE_DISABLE_STATE(wm, (WIIMOTE_STATE_EXP|WIIMOTE_STATE_EXP_HANDSHAKE|WIIMOTE_STATE_EXP_FAILED));
	WIIMOTE_DISABLE_STATE(wm,(WIIMOTE_STATE_CONNECTED|WIIMOTE_STATE_HANDSHAKE|WIIMOTE_STATE_HANDSHAKE_COMPLETE));
	
	if(wm->event_cb) wm->event_cb(wm, WIIUSE_DISCONNECT);

	return;
}

int wiiuse_os_poll(struct wiimote_t **wm, int wiimotes) { return wm != NULL; }

int wiiuse_os_read(struct wiimote_t *wm, byte *buf, int len)
{
    if (!wm || !buf || len==0 || !WIIMOTE_IS_CONNECTED(wm))
        return 0;

	//printf("__wiiuse_receive[%02x]\n",*(char*)buf);
	wm->event = WIIUSE_NONE;

	memcpy(wm->event_buf, buf, len);
	memset(&(wm->event_buf[len]), 0, (MAX_PAYLOAD - len));
	propagate_event(wm, wm->event, buf);

	if(wm->event!=WIIUSE_NONE) {
		if(wm->event_cb) wm->event_cb(wm, wm->event);
	}

	return len;
}
int wiiuse_os_write(struct wiimote_t *wm, byte report_type, byte *buf, int len)
{
	int rc;
    byte write_buffer[MAX_PAYLOAD];

    if (!wm || !wm->sock || !WIIMOTE_IS_CONNECTED(wm))
        return 0;

    write_buffer[0] = report_type;
	memcpy(write_buffer+1, buf, len);
	
	rc = bte_senddata(wm->sock,buf,len);
	if (rc < 0)
        wiiuse_disconnected(wm);

	return rc;
}

unsigned long wiiuse_os_ticks()
{
	struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    unsigned long ms = 1000 * tp.tv_sec + tp.tv_nsec / 1e6;
    return ms;
}

#endif /* ifdef WIIUSE_GEKKO */
