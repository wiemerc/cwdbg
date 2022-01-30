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
#define MAX_FRAME_SIZE 1024
#define MAX_MSG_DATA_LEN 255

/*
 * SLIP protocol
 */
#define SLIP_END                0xc0
#define SLIP_ESCAPED_END        0xdc
#define SLIP_ESC                0xdb
#define SLIP_ESCAPED_ESC        0xdd

#define IOExtTime timerequest   /* just to make the code look a bit nicer... */
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
    uint16_t msg_seqnum;
    uint16_t msg_checksum;
    uint8_t  msg_type;
    uint8_t  msg_length;
    uint8_t  msg_data[0];
} ProtoMessage;


/*
 * exported functions
 */
int32_t serio_init();
void serio_exit();
ProtoMessage *create_message();
void delete_message(ProtoMessage *p_msg);
int32_t send_message(ProtoMessage *p_msg);
int32_t recv_message(ProtoMessage *p_msg);


/*
 * external references
 */
extern uint32_t g_serio_errno;    /* serial IO error code */

#endif /* CWDEBUG_SERIO_H */
