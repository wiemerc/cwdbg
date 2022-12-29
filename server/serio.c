//
// serio.c - part of cwdbg, a debugger for the AmigaOS
//           This file contains the routines for the serial communication with the remote host.
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include <devices/serial.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <stdio.h>

#include "serio.h"
#include "util.h"
#include "stdint.h"


#define SLIP_END                0xc0
#define SLIP_ESCAPED_END        0xdc
#define SLIP_ESC                0xdb
#define SLIP_ESCAPED_ESC        0xdd


//
// exported routines
//

SerialConnection *create_serial_conn()
{
    SerialConnection *p_conn;
    struct MsgPort *port = &(((struct Process *) FindTask(NULL))->pr_MsgPort);

    if ((p_conn = AllocVec(sizeof(SerialConnection), MEMF_CLEAR)) == NULL) {
        LOG(CRIT, "Could not allocate memory for serial connection object");
        return NULL;
    }
    if ((p_conn->p_io_request = (struct IOExtSer *) CreateExtIO(port, sizeof(struct IOExtSer))) == NULL) {
        LOG(CRIT, "Could not create IO request for serial device");
        return NULL;
    }
    if (OpenDevice("serial.device", 0l, (struct IORequest *) p_conn->p_io_request, 0l) != 0) {
        LOG(CRIT, "Could not open serial device");
        DeleteExtIO((struct IORequest *) p_conn->p_io_request);
        return NULL;
    }
    /* configure device to terminate read requests on SLIP end-of-frame-markers and disable flow control */
    /* 
        * TODO: configure device for maximum speed:
    p_conn->p_io_request->io_SerFlags     |= SERF_XDISABLED | SERF_RAD_BOOGIE;
    p_conn->p_io_request->io_Baud          = 292000l;
        */
    p_conn->p_io_request->io_SerFlags     |= SERF_XDISABLED;
    p_conn->p_io_request->IOSer.io_Command = SDCMD_SETPARAMS;
    memset(&p_conn->p_io_request->io_TermArray, SLIP_END, 8);
    if (DoIO((struct IORequest *) p_conn->p_io_request) != 0) {
        LOG(CRIT, "Could not configure serial device");
        CloseDevice((struct IORequest *) p_conn->p_io_request);
        DeleteExtIO((struct IORequest *) p_conn->p_io_request);
        return NULL;
    }
    return p_conn;
}


void destroy_serial_conn(SerialConnection *p_conn)
{
    LOG(DEBUG, "Closing serial device");
    CloseDevice((struct IORequest *) p_conn->p_io_request);
    DeleteExtIO((struct IORequest *) p_conn->p_io_request);
    FreeVec(p_conn);
}


int put_data_into_slip_frame(SerialConnection *p_conn, const Buffer *pb_data, Buffer *pb_frame)
{
    const uint8_t *src = pb_data->p_addr;
    uint8_t *dst       = pb_frame->p_addr;
    int nbytes_read, nbytes_written;

    /*
     * The limit for nbytes_written has to be length of the destination buffer - 1
     * because due to the escaping mechanism in SLIP, we can get two bytes in
     * one pass of the loop.
     */
    for (nbytes_read = 0, nbytes_written = 0;
        nbytes_read < pb_data->size && nbytes_written < pb_frame->size - 1; 
        nbytes_read++, nbytes_written++, src++, dst++) {
        if (*src == SLIP_END) {
            *dst = SLIP_ESC;
            ++dst;
            ++nbytes_written;
            *dst = SLIP_ESCAPED_END;
        }
        else if (*src == SLIP_ESC) {
            *dst = SLIP_ESC;
            ++dst;
            ++nbytes_written;
            *dst = SLIP_ESCAPED_ESC;
        }
        else {
            *dst = *src;
        }
    }
    if (nbytes_read < pb_data->size) {
        LOG(ERROR, "Could not copy all bytes to the destination");
        p_conn->errno = ERROR_BUFFER_OVERFLOW;
        return DOSFALSE;
    }

    /* add SLIP end-of-frame marker */
    if (nbytes_written < pb_frame->size) {
        *dst = SLIP_END;
        pb_frame->size = ++nbytes_written;
    }
    else {
        LOG(ERROR, "Could not add SLIP end-of-frame marker");
        p_conn->errno = ERROR_BUFFER_OVERFLOW;
        return NULL;
    }
    p_conn->errno = 0;
    return DOSTRUE;
}


int get_data_from_slip_frame(SerialConnection *p_conn, Buffer *pb_data, const Buffer *pb_frame)
{
    const uint8_t *src = pb_frame->p_addr;
    uint8_t *dst       = pb_data->p_addr;
    int nbytes_read, nbytes_written;
    for (nbytes_read = 0, nbytes_written = 0;
        nbytes_read < pb_frame->size && nbytes_written < pb_data->size;
		nbytes_read++, nbytes_written++, src++, dst++) {
        if (*src == SLIP_ESC) {
            ++src;
            ++nbytes_read;
            if (*src == SLIP_ESCAPED_END)
                *dst = SLIP_END;
            else if (*src == SLIP_ESCAPED_ESC)
                *dst = SLIP_ESC;
            else {
                LOG(ERROR, "Invalid escape sequence found in SLIP frame: 0x%02lx", (uint) *src);
                p_conn->errno = ERROR_BAD_NUMBER;
                break;
            }
        }
        else if (*src == SLIP_END) {
            ++nbytes_read;
            break;
        }
        else
            *dst = *src;
    }
    pb_data->size = nbytes_written;
    if (nbytes_read < pb_frame->size) {
        LOG(ERROR, "Could not copy all bytes to the destination");
        p_conn->errno = ERROR_BUFFER_OVERFLOW;
        return DOSFALSE;
    }
    p_conn->errno = 0;
    return DOSTRUE;
}


int send_slip_frame(SerialConnection *p_conn, const Buffer *pb_frame)
{
    p_conn->p_io_request->io_SerFlags     &= ~SERF_EOFMODE;      /* clear EOF mode */
    p_conn->p_io_request->IOSer.io_Command = CMD_WRITE;
    p_conn->p_io_request->IOSer.io_Length  = pb_frame->size;
    p_conn->p_io_request->IOSer.io_Data    = (void *) pb_frame->p_addr;
    p_conn->errno = DoIO((struct IORequest *) p_conn->p_io_request);
    if (p_conn->errno == 0)
        return DOSTRUE;
    else
        return DOSFALSE;
}


int recv_slip_frame(SerialConnection *p_conn, Buffer *pb_frame)
{
    p_conn->p_io_request->io_SerFlags     |= SERF_EOFMODE;       /* set EOF mode */
    p_conn->p_io_request->IOSer.io_Command = CMD_READ;
    p_conn->p_io_request->IOSer.io_Data    = (void *) pb_frame->p_addr;
    p_conn->p_io_request->IOSer.io_Length  = pb_frame->size;
    p_conn->errno = DoIO((struct IORequest *) p_conn->p_io_request);
    if (p_conn->errno == 0) {
        pb_frame->size = p_conn->p_io_request->IOSer.io_Actual;
        LOG(DEBUG, "Dump of received SLIP frame (%ld bytes):", pb_frame->size);
        dump_memory(pb_frame->p_addr, pb_frame->size);
        return DOSTRUE;
    }
    else
        return DOSFALSE;
}


//
// local routines
//

//
// calculate IP / ICMP checksum (taken from the code for in_cksum() floating on the net)
//
static uint16_t calc_checksum(const uint8_t * bytes, uint32_t len)
{
    uint32_t sum, i;
    uint16_t * p;

    sum = 0;
    p = (uint16_t *) bytes;

    for (i = len; i > 1; i -= 2)                /* sum all 16-bit words */
        sum += *p++;

    if (i == 1)                                 /* add an odd byte if necessary */
        sum += (uint16_t) *((uint8_t *) p);

    sum = (sum >> 16) + (sum & 0x0000ffff);     /* fold in upper 16 bits */
    sum += (sum >> 16);                         /* add carry bits */
    return ~((uint16_t) sum);                     /* return 1-complement truncated to 16 bits */
}
