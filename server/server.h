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


//
// constants
//

// debugger protocol messages
#define MSG_INIT            0x00
#define MSG_ACK             0x01
#define MSG_NACK            0x02
#define MSG_RUN             0x03
#define MSG_QUIT            0x04
#define MSG_CONT            0x05
#define MSG_STEP            0x06
#define MSG_KILL            0x07
#define MSG_PEEK_MEM        0x08
#define MSG_POKE_MEM        0x09
#define MSG_SET_BP          0x0a
#define MSG_CLEAR_BP        0x0b
#define MSG_TARGET_STOPPED  0x0c

// connection states - for future use
#define CONN_STATE_INITIAL 0


//
// type definitions
//
typedef struct SHostConnection {
    int state;
    uint16_t next_seq_num;
} HostConnection;


//
// exported functions
//
void process_remote_commands(Debugger *p_dbg, TaskContext *p_taks_ctx);

#endif // CWDEBUG_SERVER_H
