#ifndef CWDEBUG_SERVER_H
#define CWDEBUG_SERVER_H
//
// server.h - part of CWDebug, a source-level debugger for the AmigaOS
//
// Copyright(C) 2018-2021 Constantin Wiemer
//


//
// included files
//
#include "debugger.h"
#include "stdint.h"


//
// constants
//

// debugger protocol messages
#define MSG_INIT            0x00
#define MSG_RUN             0x01
#define MSG_QUIT            0x02
#define MSG_CONT            0x03
#define MSG_STEP            0x04
#define MSG_KILL            0x05
#define MSG_PEEK_MEM        0x06
#define MSG_POKE_MEM        0x07
#define MSG_SET_BP          0x08
#define MSG_CLEAR_BP        0x09
#define MSG_TARGET_STOPPED  0x0a

// flags for the responses, like in Diameter
#define MSG_RESPONSE_FLAG 0x80
#define MSG_ERROR_FLAG    0x40

#define MAX_MSG_DATA_LEN 255

//
// type definitions
//

// This is how a complete protocol message looks like:
//  ---------------------------------------------------
// | sequence number | checksum | message type | data |
//  ---------------------------------------------------
// The checksum is calculated in the same way as with IP / UDP headers.

// structure for protocol message
typedef struct {
    uint16_t msg_seqnum;
    uint16_t msg_checksum;
    uint8_t  msg_type;
    uint8_t  msg_length;
    uint8_t  msg_data[0];
} ProtoMessage;


//
// exported functions
//
void process_remote_commands(TaskContext *p_taks_ctx);

#endif // CWDEBUG_SERVER_H
