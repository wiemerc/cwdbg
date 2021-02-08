#ifndef CWDEBUG_SERIO_H
#define CWDEBUG_SERIO_H
/*
 * serio.h - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <dos/dos.h>

#include "stdint.h"


/*
 * constants
 */
#define MAX_FRAME_SIZE 4096

/*
 * SLIP protocol
 */
#define SLIP_END                0xc0
#define SLIP_ESCAPED_END        0xdc
#define SLIP_ESC                0xdb
#define SLIP_ESCAPED_ESC        0xdd

#define IOExtTime timerequest   /* just to make the code look a bit nicer... */
#define SERIO_TIMEOUT 10        /* timeout for reads and writes in seconds */


/*
 * buffer for requests and responses
 */
typedef struct {
    uint8_t *p_addr;
    uint32_t size;
} Buffer;


/*
 * exported functions
 */
int32_t serio_init();
void serio_exit();
int32_t put_data_into_slip_frame(const Buffer *pb_data, Buffer *pb_frame);
int32_t get_data_from_slip_frame(Buffer *pb_data, const Buffer *pb_frame);
int32_t send_slip_frame(const Buffer *pb_frame);
int32_t recv_slip_frame(Buffer *pb_frame);


/*
 * external references
 */
extern uint32_t g_serio_errno;    /* serial IO error code */

#endif /* CWDEBUG_SERIO_H */
