//
// serio.c - part of CWDebug, a source-level debugger for the AmigaOS
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


uint32_t g_serio_errno = 0;
static struct IOExtSer *io_request;


static int32_t put_data_into_slip_frame(const Buffer *pb_data, Buffer *pb_frame);
static int32_t get_data_from_slip_frame(Buffer *pb_data, const Buffer *pb_frame);
static int32_t send_slip_frame(const Buffer *pb_frame);
static int32_t recv_slip_frame(Buffer *pb_frame);


//
// exported routines
//

int32_t serio_init()
{
    struct MsgPort *port = &(((struct Process *) FindTask(NULL))->pr_MsgPort);
    if ((io_request = (struct IOExtSer *) CreateExtIO(port, sizeof(struct IOExtSer))) == NULL) {
        LOG(CRIT, "Could not create IO request for serial device");
        return DOSFALSE;
    }
    if (OpenDevice("serial.device", 0l, (struct IORequest *) io_request, 0l) != 0) {
        LOG(CRIT, "Could not open serial device");
        DeleteExtIO((struct IORequest *) io_request);
        return DOSFALSE;
    }
    /* configure device to terminate read requests on SLIP end-of-frame-markers and disable flow control */
    /* 
        * TODO: configure device for maximum speed:
    io_request->io_SerFlags     |= SERF_XDISABLED | SERF_RAD_BOOGIE;
    io_request->io_Baud          = 292000l;
        */
    io_request->io_SerFlags     |= SERF_XDISABLED;
    io_request->IOSer.io_Command = SDCMD_SETPARAMS;
    memset(&io_request->io_TermArray, SLIP_END, 8);
    if (DoIO((struct IORequest *) io_request) != 0) {
        LOG(CRIT, "Could not configure serial device");
        CloseDevice((struct IORequest *) io_request);
        DeleteExtIO((struct IORequest *) io_request);
        return DOSFALSE;
    }
    return DOSTRUE;
}


void serio_exit()
{
    // We need to check if serial IO has actually been initialized because quit_debugger() calls us unconditionally.
    if (io_request) {
        CloseDevice((struct IORequest *) io_request);
        DeleteExtIO((struct IORequest *) io_request);
    }
}


int32_t send_message(ProtoMessage *p_msg)
{
    uint8_t frame[MAX_FRAME_SIZE];
    Buffer b_msg, b_frame;

    // TODO: set checksum
    b_msg.p_addr = (uint8_t *) p_msg;
    b_msg.size   = sizeof(ProtoMessage);
    b_frame.p_addr = frame;
    b_frame.size   = MAX_FRAME_SIZE;
    if (put_data_into_slip_frame(&b_msg, &b_frame) == DOSFALSE) {
        LOG(ERROR, "could not put data into SLIP frame: %ld", g_serio_errno);
        return DOSFALSE;
    }
    if (send_slip_frame(&b_frame) == DOSFALSE) {
        LOG(ERROR, "failed to send SLIP frame: %ld", g_serio_errno);
        return DOSFALSE;
    }
    return DOSTRUE;
}


int32_t recv_message(ProtoMessage *p_msg)
{
    uint8_t frame[MAX_FRAME_SIZE];
    Buffer b_msg, b_frame;

    b_msg.p_addr = (uint8_t *) p_msg;
    b_msg.size   = sizeof(ProtoMessage);
    b_frame.p_addr = frame;
    b_frame.size   = MAX_FRAME_SIZE;
    if (recv_slip_frame(&b_frame) == DOSFALSE) {
        LOG(ERROR, "failed to receive SLIP frame: %ld", g_serio_errno);
        return DOSFALSE;
    }
    if (get_data_from_slip_frame(&b_msg, &b_frame) == DOSFALSE) {
        LOG(ERROR, "could not get data from SLIP frame: %ld", g_serio_errno);
        return DOSFALSE;
    }
    // TODO: check checksum
    return DOSTRUE;
}


//
// local routines
//

/*
 * calculate IP / ICMP checksum (taken from the code for in_cksum() floating on the net)
 */
static USHORT calc_checksum(const UBYTE * bytes, ULONG len)
{
    ULONG sum, i;
    USHORT * p;

    sum = 0;
    p = (USHORT *) bytes;

    for (i = len; i > 1; i -= 2)                /* sum all 16-bit words */
        sum += *p++;

    if (i == 1)                                 /* add an odd byte if necessary */
        sum += (USHORT) *((UBYTE *) p);

    sum = (sum >> 16) + (sum & 0x0000ffff);     /* fold in upper 16 bits */
    sum += (sum >> 16);                         /* add carry bits */
    return ~((USHORT) sum);                     /* return 1-complement truncated to 16 bits */
}


static int32_t put_data_into_slip_frame(const Buffer *pb_data, Buffer *pb_frame)
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
        LOG(ERROR, "could not copy all bytes to the destination");
        g_serio_errno = ERROR_BUFFER_OVERFLOW;
        return DOSFALSE;
    }

    /* add SLIP end-of-frame marker */
    if (nbytes_written < pb_frame->size) {
        *dst = SLIP_END;
        pb_frame->size = ++nbytes_written;
    }
    else {
        LOG(ERROR, "could not add SLIP end-of-frame marker");
        g_serio_errno = ERROR_BUFFER_OVERFLOW;
        return NULL;
    }
    g_serio_errno = 0;
    return DOSTRUE;
}


static int32_t get_data_from_slip_frame(Buffer *pb_data, const Buffer *pb_frame)
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
                LOG(ERROR, "invalid escape sequence found in SLIP frame: 0x%02lx", (uint32_t) *src);
                g_serio_errno = ERROR_BAD_NUMBER;
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
        LOG(ERROR, "could not copy all bytes to the destination");
        g_serio_errno = ERROR_BUFFER_OVERFLOW;
        return DOSFALSE;
    }
    g_serio_errno = 0;
    return DOSTRUE;
}


static int32_t send_slip_frame(const Buffer *pb_frame)
{
    int8_t error;

    io_request->io_SerFlags     &= ~SERF_EOFMODE;      /* clear EOF mode */
    io_request->IOSer.io_Command = CMD_WRITE;
    io_request->IOSer.io_Length  = pb_frame->size;
    io_request->IOSer.io_Data    = (void *) pb_frame->p_addr;
    // We need to set the reply port in the IORequest to the message port of the current process because serial IO
    // can be done by either the target process or the debugger process although the serial device is opened by
    // the debugger process.
    io_request->IOSer.io_Message.mn_ReplyPort = &((struct Process *) FindTask(NULL))->pr_MsgPort;
    g_serio_errno = error = DoIO((struct IORequest *) io_request);
    if (error == 0)
        return DOSTRUE;
    else
        return DOSFALSE;
}


static int32_t recv_slip_frame(Buffer *pb_frame)
{
    int8_t error;

    io_request->io_SerFlags     |= SERF_EOFMODE;       /* set EOF mode */
    io_request->IOSer.io_Command = CMD_READ;
    io_request->IOSer.io_Data    = (void *) pb_frame->p_addr;
    io_request->IOSer.io_Length  = pb_frame->size;
    io_request->IOSer.io_Message.mn_ReplyPort = &((struct Process *) FindTask(NULL))->pr_MsgPort;
    g_serio_errno = error = DoIO((struct IORequest *) io_request);
    if (error == 0) {
        pb_frame->size = io_request->IOSer.io_Actual;
        LOG(DEBUG, "dump of received SLIP frame (%ld bytes):", pb_frame->size);
        dump_memory(pb_frame->p_addr, pb_frame->size);
        return DOSTRUE;
    }
    else
        return DOSFALSE;
}
