
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

void wiiuse_init_platform_fields(struct wiimote_t *wm, wii_event_cb event_cb)
{
    wm[i]->sock = NULL;
	wm[i]->bdaddr = *BD_ADDR_ANY;
	wm[i]->event_cb = event_cb;
}
void wiiuse_cleanup_platform_fields(struct wiimote_t *wm)
{
	wiiuse_init_platform_fields(wm, NULL);
}

int wiiuse_os_find(struct wiimote_t **wm, int max_wiimotes, int timeout)
{
	return ERR_OK;
}

int wiiuse_os_connect(struct wiimote_t **wm, int wiimotes)
{
	if(!wm) {
		return ERR_OK;
	}
	
	for(int i = 0; i < wiimotes; i++)
	{
		if (!wm[i])
        {
            continue;
        }
        
		//printf("__wiiuse_connected()\n");
		WIIMOTE_ENABLE_STATE(wm[i],(WIIMOTE_STATE_CONNECTED|WIIMOTE_STATE_HANDSHAKE));

		wm->handshake_state = 0;
		wiiuse_handshake(wm[i],NULL,0);
	}

	return ERR_OK;
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

	return ERR_OK;
}

int wiiuse_os_poll(struct wiimote_t **wm, int wiimotes){ return ERR_OK }

int wiiuse_os_read(struct wiimote_t *wm, byte *buf, int len)
{
    if (!wm || !buf || len==0 || !WIIMOTE_IS_CONNECTED(wm))
    {
        return ERR_OK;
    }

	//printf("__wiiuse_receive[%02x]\n",*(char*)buf);
	wm->event = WIIUSE_NONE;

	memcpy(wm->event_buf, buf, len);
	memset(&(wm->event_buf[len]), 0, (MAX_PAYLOAD - len));
	propagate_event(wm, wm->event, buf);

	if(wm->event!=WIIUSE_NONE) {
		if(wm->event_cb) wm->event_cb(wm, wm->event);
	}

	return ERR_OK;
}
int wiiuse_os_write(struct wiimote_t *wm, byte report_type, byte *buf, int len)
{
    byte write_buffer[MAX_PAYLOAD];

    if (!wm || !wm->sock || !WIIMOTE_IS_CONNECTED(wm))
    {
        return ERR_CONN;
    }

    write_buffer[0] = report_type;
	memcpy(write_buffer+1, buf, len);

	return bte_senddata(wm->sock,buf,len);
}

unsigned long wiiuse_os_ticks()
{
	struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    unsigned long ms = 1000 * tp.tv_sec + tp.tv_nsec / 1e6;
    return ms;
}

#endif /* ifdef WIIUSE_GEKKO */
