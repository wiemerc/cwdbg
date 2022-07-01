#ifndef CWDEBUG_SERIO_H
#define CWDEBUG_SERIO_H
/*
 * serio.h - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


/*
 * included files
 */
#include <dos/dos.h>

#include "stdint.h"


/*
 * constants
 */
#define MAX_FRAME_SIZE 512      // should be large enough to hold a SLIP-encoded message + data
#define MAX_MSG_DATA_LEN 256

/*
 * SLIP protocol
 */
#define SLIP_END                0xc0
#define SLIP_ESCAPED_END        0xdc
#define SLIP_ESC                0xdb
#define SLIP_ESCAPED_ESC        0xdd

#define SERIO_TIMEOUT 10        /* timeout for reads and writes in seconds */


//
// type definitions
//

/*
 * buffer for requests and responses
 */
typedef struct {
    uint8_t *p_addr;
    uint32_t size;
} Buffer;


// structure for protocol message
typedef struct {
    uint16_t seqnum;
    uint16_t checksum;
    uint8_t  type;
    uint8_t  length;
    uint8_t  data[MAX_MSG_DATA_LEN];
} ProtoMessage;


/*
 * exported functions
 */
int32_t serio_init();
void serio_exit();
int32_t send_message(ProtoMessage *p_msg);
int32_t recv_message(ProtoMessage *p_msg);


/*
 * external references
 */
extern uint32_t g_serio_errno;    /* serial IO error code */

#endif /* CWDEBUG_SERIO_H */
