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
static struct IOExtSer *sreq;
static struct IOExtTime *treq;


static int32_t put_data_into_slip_frame(const Buffer *pb_data, Buffer *pb_frame);
static int32_t get_data_from_slip_frame(Buffer *pb_data, const Buffer *pb_frame);
static int32_t send_slip_frame(const Buffer *pb_frame);
static int32_t recv_slip_frame(Buffer *pb_frame);
static void dump_buffer(const Buffer *pb_buffer);


//
// exported routines
//

// TODO: get rid of timer device
int32_t serio_init()
{
    struct MsgPort *port = &(((struct Process *) FindTask(NULL))->pr_MsgPort);
    if ((sreq = (struct IOExtSer *) CreateExtIO(port, sizeof(struct IOExtSer))) != NULL) {
        if ((treq = (struct IOExtTime *) CreateExtIO(port, sizeof(struct IOExtTime)))) {
            if (OpenDevice("serial.device", 0l, (struct IORequest *) sreq, 0l) == 0) {
                /* configure device to terminate read requests on SLIP end-of-frame-markers and disable flow control */
                /* 
                 * TODO: configure device for maximum speed:
                sreq->io_SerFlags     |= SERF_XDISABLED | SERF_RAD_BOOGIE;
                sreq->io_Baud          = 292000l;
                 */
                sreq->io_SerFlags     |= SERF_XDISABLED;
                sreq->IOSer.io_Command = SDCMD_SETPARAMS;
                memset(&sreq->io_TermArray, SLIP_END, 8);
                if (DoIO((struct IORequest *) sreq) == 0) {
                    if (OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *) treq, 0l) == 0) {
                        return DOSTRUE;
                    }
                    else {
                        LOG(CRIT, "could not open timer device");
                        CloseDevice((struct IORequest *) sreq);
                        DeleteExtIO((struct IORequest *) treq);
                        DeleteExtIO((struct IORequest *) sreq);
                        return DOSFALSE;
                    }
                }
                else {
                    LOG(CRIT, "could not configure serial device");
                    CloseDevice((struct IORequest *) sreq);
                    DeleteExtIO((struct IORequest *) treq);
                    DeleteExtIO((struct IORequest *) sreq);
                    return DOSFALSE;
                }
            }
            else {
                LOG(CRIT, "could not open serial device");
                DeleteExtIO((struct IORequest *) treq);
                DeleteExtIO((struct IORequest *) sreq);
                return DOSFALSE;
            }
        }
        else {
            LOG(CRIT, "could not create request for timer device");
            DeleteExtIO((struct IORequest *) sreq);
            return DOSFALSE;
        }
    }
    else {
        LOG(CRIT, "could not create request for serial device");
        return DOSFALSE;
    }
}


void serio_exit()
{
    if (treq && sreq) {
        CloseDevice((struct IORequest *) treq);
        CloseDevice((struct IORequest *) sreq);
        DeleteExtIO((struct IORequest *) treq);
        DeleteExtIO((struct IORequest *) sreq);
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

    sreq->io_SerFlags     &= ~SERF_EOFMODE;      /* clear EOF mode */
    sreq->IOSer.io_Command = CMD_WRITE;
    sreq->IOSer.io_Length  = pb_frame->size;
    sreq->IOSer.io_Data    = (void *) pb_frame->p_addr;
    // We need to set the reply port in the IORequest to the message port of the current process because serial IO
    // can be done by either the target process or the debugger process although the serial device is opened by
    // the debugger process.
    sreq->IOSer.io_Message.mn_ReplyPort = &((struct Process *) FindTask(NULL))->pr_MsgPort;
    g_serio_errno = error = DoIO((struct IORequest *) sreq);
    if (error == 0)
        return DOSTRUE;
    else
        return DOSFALSE;
}


static int32_t recv_slip_frame(Buffer *pb_frame)
{
    int8_t error;

    sreq->io_SerFlags     |= SERF_EOFMODE;       /* set EOF mode */
    sreq->IOSer.io_Command = CMD_READ;
    sreq->IOSer.io_Data    = (void *) pb_frame->p_addr;
    sreq->IOSer.io_Length  = pb_frame->size;
    sreq->IOSer.io_Message.mn_ReplyPort = &((struct Process *) FindTask(NULL))->pr_MsgPort;
    g_serio_errno = error = DoIO((struct IORequest *) sreq);
    if (error == 0) {
        pb_frame->size = sreq->IOSer.io_Actual;
        LOG(DEBUG, "dump of received SLIP frame (%ld bytes):", pb_frame->size);
        dump_buffer(pb_frame);
        return DOSTRUE;
    }
    else
        return DOSFALSE;
}


//
// create a hexdump of a buffer
//
// TODO: move hexdump routine to util.c and use it here and in cli.c
static void dump_buffer(const Buffer *pb_buffer)
{
    uint32_t pos = 0, i, nchars;
    char line[256], *p;

    while (pos < pb_buffer->size) {
        printf("%04x: ", pos);
        for (i = pos, p = line, nchars = 0; (i < pos + 16) && (i < pb_buffer->size); ++i, ++p, ++nchars) {
            printf("%02x ", pb_buffer->p_addr[i]);
            if (pb_buffer->p_addr[i] >= 0x20 && pb_buffer->p_addr[i] <= 0x7e) {
                sprintf(p, "%c", pb_buffer->p_addr[i]);
            }
            else {
                sprintf(p, ".");
            }
        }
        if (nchars < 16) {
            for (i = 1; i <= (3 * (16 - nchars)); ++i, ++p, ++nchars) {
                sprintf(p, " ");
            }
        }
        *p = '\0';

        printf("\t%s\n", line);
        pos += 16;
    }
}
