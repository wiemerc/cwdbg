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

#include "debugger.h"
#include "stdint.h"


/*
 * constants
 */
#define MAX_BUFFER_SIZE 1024

/*
 * SLIP protocol
 */
#define SLIP_END                0xc0
#define SLIP_ESCAPED_END        0xdc
#define SLIP_ESC                0xdb
#define SLIP_ESCAPED_ESC        0xdd

/*
 * debugger protocol commands
 */
#define CMD_SYN                 0
#define CMD_RUN                 1
#define CMD_CONT                2
#define CMD_STEP                3
#define CMD_QUIT                4
#define CMD_PEEK                5
#define CMD_POKE                6

#define IOExtTime timerequest   /* just to make the code look a bit nicer... */
#define SERIO_TIMEOUT 10        /* timeout for reads and writes in seconds */


/*
 * buffer for requests and responses
 */
typedef struct {
    uint8_t *b_addr;
    uint32_t b_size;
} Buffer;


/*
 * exported functions
 */
Buffer *create_buffer(uint32_t size);
void delete_buffer(const Buffer *buffer);
int32_t serio_init();
void serio_exit();
int8_t serio_get_status();
void serio_stop_timer();
void serio_abort();
int32_t recv_slip_frame(Buffer *frame);
void process_remote_commands(TaskContext *p_taks_ctx);


/*
 * external references
 */
extern uint32_t g_serio_errno;    /* serial IO error code */

#endif /* CWDEBUG_SERIO_H */
