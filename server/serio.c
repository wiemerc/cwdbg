/*
 * serio.c - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


#include <devices/serial.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <stdio.h>

#include "debugger.h"
#include "serio.h"
#include "util.h"
#include "stdint.h"


uint32_t g_serio_errno = 0;
static struct IOExtSer *sreq;
static struct IOExtTime *treq;


static int32_t slip_encode_buffer(Buffer *dbuf, const Buffer *sbuf);
static int32_t slip_decode_buffer(Buffer *dbuf, const Buffer *sbuf);


/*
 * initialize this module
 */
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


/*
 * free all ressources
 */
void serio_exit()
{
    CloseDevice((struct IORequest *) treq);
    CloseDevice((struct IORequest *) sreq);
    DeleteExtIO((struct IORequest *) treq);
    DeleteExtIO((struct IORequest *) sreq);
}


/*
 * create / delete a buffer
 */
Buffer *create_buffer(uint32_t size)
{
    Buffer *buffer;

    /* allocate a memory block large enough for the Buffer structure and buffer itself */
    if ((buffer = AllocVec(size + sizeof(Buffer), 0)) != NULL) {
        buffer->b_addr = ((uint8_t *) buffer) + sizeof(Buffer);
        buffer->b_size = 0;
        return buffer;
    }
    else
        return NULL;
}


void delete_buffer(const Buffer *buffer)
{
    FreeVec((void *) buffer);
}


/*
 * create a hexdump of a buffer
 */
// TODO: move hexdump routine to util.c and use it here and in cli.c
void dump_buffer(const Buffer *buffer)
{
    uint32_t pos = 0, i, nchars;
    char line[256], *p;

    while (pos < buffer->b_size) {
        printf("%04x: ", pos);
        for (i = pos, p = line, nchars = 0; (i < pos + 16) && (i < buffer->b_size); ++i, ++p, ++nchars) {
            printf("%02x ", buffer->b_addr[i]);
            if (buffer->b_addr[i] >= 0x20 && buffer->b_addr[i] <= 0x7e) {
                sprintf(p, "%c", buffer->b_addr[i]);
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

/*
 * SLIP routines
 */
static Buffer *create_slip_frame(const Buffer *data)
{
    Buffer *frame;

    /* create buffer large enough to hold the IP header and the data */
    if ((frame = create_buffer(MAX_BUFFER_SIZE)) == NULL) {
        LOG(ERROR, "could not create buffer for SLIP frame");
        g_serio_errno = ERROR_NO_FREE_STORE;
        return NULL;
    }

    if (slip_encode_buffer(frame, data) == DOSFALSE) {
        LOG(ERROR, "could not copy all data to the SLIP frame");
        /* g_serio_errno has already been set by slip_encode_buffer() */
        return NULL;
    }
    
    /* add SLIP end-of-frame marker */
    if (frame->b_size < MAX_BUFFER_SIZE) {
        *(frame->b_addr + frame->b_size) = SLIP_END;
        ++frame->b_size;
    }
    else {
        LOG(ERROR, "could not add SLIP end-of-frame marker");
        g_serio_errno = ERROR_BUFFER_OVERFLOW;
        return NULL;
    }
    g_serio_errno = 0;
    return frame;
}


static int32_t send_slip_frame(const Buffer *frame)
{
    int8_t error;

    sreq->io_SerFlags     &= ~SERF_EOFMODE;      /* clear EOF mode */
    sreq->IOSer.io_Command = CMD_WRITE;
    sreq->IOSer.io_Length  = frame->b_size;
    sreq->IOSer.io_Data    = (void *) frame->b_addr;
    g_serio_errno = error = DoIO((struct IORequest *) sreq);
    if (error == 0)
        return DOSTRUE;
    else
        return DOSFALSE;
}


int32_t recv_slip_frame(Buffer *frame)
{
    int8_t error;

    sreq->io_SerFlags     |= SERF_EOFMODE;       /* set EOF mode */
    sreq->IOSer.io_Command = CMD_READ;
    sreq->IOSer.io_Length  = MAX_BUFFER_SIZE;
    sreq->IOSer.io_Data    = (void *) frame->b_addr;
    g_serio_errno = error = DoIO((struct IORequest *) sreq);
    if (error == 0) {
        frame->b_size = sreq->IOSer.io_Actual;
        LOG(DEBUG, "dump of received SLIP frame (%ld bytes):", frame->b_size);
        dump_buffer(frame);
        return DOSTRUE;
    }
    else
        return DOSFALSE;
}


/*
 * copy data between two buffers and SLIP-encode them on the way
 */
static int32_t slip_encode_buffer(Buffer *dbuf, const Buffer *sbuf)
{
    const uint8_t *src = sbuf->b_addr;
    uint8_t *dst       = dbuf->b_addr;
    int nbytes, nbytes_tot = 0;
    /*
     * The limit for nbytes_tot has to be length of the destination buffer - 1
     * because due to the escaping mechanism in SLIP, we can get two bytes in
     * one pass of the loop.
     */
    for (nbytes = 0;
        nbytes < sbuf->b_size && nbytes_tot < MAX_BUFFER_SIZE - 1; 
		nbytes++, nbytes_tot++, src++, dst++) {
        if (*src == SLIP_END) {
            *dst = SLIP_ESC;
            ++dst;
            ++nbytes_tot;
            *dst = SLIP_ESCAPED_END;
        }
        else if (*src == SLIP_ESC) {
            *dst = SLIP_ESC;
            ++dst;
            ++nbytes_tot;
            *dst = SLIP_ESCAPED_ESC;
        }
        else {
            *dst = *src;
        }
    }
    dbuf->b_size = nbytes_tot;
    if (nbytes < sbuf->b_size) {
        LOG(ERROR, "could not copy all bytes to the destination");
        g_serio_errno = ERROR_BUFFER_OVERFLOW;
        return DOSFALSE;
    }
    g_serio_errno = 0;
    return DOSTRUE;
}


/*
 * copy data between two buffers and SLIP-decode them on the way
 */
static int32_t slip_decode_buffer(Buffer *dbuf, const Buffer *sbuf)
{
    const uint8_t *src = sbuf->b_addr;
    uint8_t *dst       = dbuf->b_addr;
    int nbytes;
    g_serio_errno = ERROR_BUFFER_OVERFLOW;
    for (nbytes = 0;
        nbytes < sbuf->b_size && nbytes < MAX_BUFFER_SIZE; 
		nbytes++, src++, dst++) {
        if (*src == SLIP_ESC) {
            ++src;
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
        else
            *dst = *src;
    }
    dbuf->b_size = nbytes;
    if (nbytes < sbuf->b_size) {
        LOG(ERROR, "could not copy all bytes to the destination");
        return DOSFALSE;
    }
    g_serio_errno = 0;
    return DOSTRUE;
}


// TODO: move routines below to separate file (server.c / remote.c)

void process_remote_commands(TaskContext *p_taks_ctx)
{
    Buffer              *p_frame;

    p_frame = create_buffer(MAX_BUFFER_SIZE);
    LOG(INFO, "waiting for host to connect...");
    recv_slip_frame(p_frame);
}
