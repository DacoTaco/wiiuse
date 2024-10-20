
/**
 *	@file
 *	@brief Handles device I/O for Nintendo Wii.
 */
 
#include "definitions.h"
#include "wiiuse_internal.h"
#include "events.h"
#include "io.h"
#include "os.h"

#ifdef WIIUSE_GEKKO

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>   /* for struct timeval */
#include <time.h>       /* for clock_gettime */

static vu32* const _ipcReg = (u32*)0xCD000000;
static __inline__ u32 ACR_ReadReg(u32 reg)
{
	return _ipcReg[reg>>2];
}

static __inline__ void ACR_WriteReg(u32 reg,u32 val)
{
	_ipcReg[reg>>2] = val;
}

int __wiiuse_os_connect_single(struct wiimote_t *wm)
{
	if(!wm)
		return 0;
	
	//printf("__wiiuse_connected()\n");
	WIIMOTE_ENABLE_STATE(wm,(WIIMOTE_STATE_CONNECTED|WIIMOTE_STATE_HANDSHAKE));

#ifndef WIIUSE_SYNC_HANDSHAKE
	wm->handshake_state = 0;
#endif
	wiiuse_handshake(wm, NULL, 0);
	
	return 1;
}

static s32 __bte_receive(void *arg, void *buffer, u16 len)
{
	struct wiimote_t *wm = (struct wiimote_t*)arg;

	if(!wm || !buffer || len==0) return 0;

	//printf("__wiiuse_receive[%02x]\n",*(char*)buffer);
	wm->event = WIIUSE_NONE;

	memcpy(wm->event_buf,buffer,len);
	memset(&(wm->event_buf[len]),0,(MAX_PAYLOAD - len));
	
	return 0;
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
	return __wiiuse_os_connect_single(wm);
}

static int __set_platform_fields(struct wiimote_t *wm, struct bte_pcb *sock, struct bd_addr *bdaddr, struct wiimote_t *(*assign_cb)(struct bd_addr *bdaddr))
{
	if(!wm || !bdaddr) 
		return 0;

	wm->bdaddr = *bdaddr;
	wm->sock = sock;
	wm->assign_cb = assign_cb;
	if(wm->sock==NULL) 
		return 0;

	bte_arg(wm->sock, wm);
	bte_received(wm->sock, __bte_receive);
	bte_disconnected(wm->sock, __bte_disconnected);

	return bte_registerdeviceasync(wm->sock, bdaddr, __bte_connected) == 0;
}


void wiiuse_sensorbar_enable(int enable)
{
	u32 val;
	u32 level;

	level = IRQ_Disable();
	val = (ACR_ReadReg(0xc0)&~0x100);
	if(enable) val |= 0x100;
	ACR_WriteReg(0xc0,val);
	IRQ_Restore(level);
}

int wiiuse_register(struct wiimote_t *wm, struct bd_addr *bdaddr, struct wiimote_t *(*assign_cb)(struct bd_addr *bdaddr))
{
	return __set_platform_fields(wm, bte_new(), bdaddr, assign_cb);
}

void wiiuse_init_platform_fields(struct wiimote_t *wm, wii_event_cb event_cb)
{
	wm->event_cb = event_cb;
	__set_platform_fields(wm, NULL, BD_ADDR_ANY, NULL);
}
void wiiuse_cleanup_platform_fields(struct wiimote_t *wm) 
{
	wm->event_cb = NULL;
	__set_platform_fields(wm, NULL, BD_ADDR_ANY, NULL);
}
int wiiuse_os_find(struct wiimote_t **wm, int max_wiimotes, int timeout){ return 0; }

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

        if (__wiiuse_os_connect_single(wm[i]))
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

int wiiuse_os_poll(struct wiimote_t **wm, int wiimotes) 
{ 
	if (!wm)
		return 0;
	
	byte read_buffer[MAX_PAYLOAD];
	int evnt = 0;
	for(int i = 0; i < wiimotes; i++)
	{
		struct wiimote_t* wiimote = wm[i];
		if(wiiuse_os_read(wiimote, read_buffer, sizeof(read_buffer)))
		{
			propagate_event(wiimote, read_buffer[0], read_buffer + 1);
            evnt += (wiimote->event != WIIUSE_NONE);
		}
		else
		{
			/* send out any waiting writes */
            wiiuse_send_next_pending_write_request(wiimote);
            idle_cycle(wiimote);
		}
		
		if(wiimote->event!=WIIUSE_NONE && wiimote->event_cb) {
			wiimote->event_cb(wiimote, wiimote->event);
		}
	}

	return evnt; 
}

int wiiuse_os_read(struct wiimote_t *wm, byte *buf, int len)
{
	if (!wm || !buf || len==0 || !WIIMOTE_IS_CONNECTED(wm))
		return 0;
	
	memset(buf, 0, len);
	memcpy(buf, wm->event_buf, len);
	
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
