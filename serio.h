#ifndef CWNET_SERIO_H
#define CWNET_SERIO_H
/*
 * serio.c - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <devices/serial.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <exec/io.h>
#include <exec/types.h>
#include <proto/alib.h>
#include <proto/exec.h>

#include "util.h"
#include "debugger.h"


#define MAX_BUFFER_SIZE 1024

/*
 * SLIP protocol
 */
#define SLIP_END                0xc0
#define SLIP_ESCAPED_END        0xdc
#define SLIP_ESC                0xdb
#define SLIP_ESCAPED_ESC        0xdd


/*
 * states
 */
#define S_QUEUED       0
#define S_READY        1
#define S_WRQ_SENT     2
#define S_RRQ_SENT     3
#define S_DATA_SENT    4
#define S_ERROR        5
#define S_FINISHED     6


#define IOExtTime timerequest   /* just to make the code look a bit nicer... */
#define SERIO_TIMEOUT 10        /* timeout for reads and writes in seconds */


/*
 * buffer for requests and responses
 */
typedef struct {
    UBYTE *b_addr;
    ULONG  b_size;
} Buffer;


/*
 * exported functions
 */
Buffer *create_buffer(ULONG size);
void delete_buffer(const Buffer *buffer);
LONG serio_init();
void serio_exit();
BYTE serio_get_status();
void serio_stop_timer();
void serio_abort();
LONG recv_slip_frame(Buffer *frame);
void process_remote_commands(TaskContext *p_taks_ctx);


/*
 * external references
 */
extern ULONG             g_serio_errno;    /* serial IO error code */

#endif /* CWNET_SERIO_H */
