#ifndef CWDEBUG_SERIO_H
#define CWDEBUG_SERIO_H
/*
 * serio.c - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <dos/dos.h>

#include "debugger.h"


#define MAX_BUFFER_SIZE 1024

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
extern ULONG g_serio_errno;    /* serial IO error code */

#endif /* CWDEBUG_SERIO_H */
