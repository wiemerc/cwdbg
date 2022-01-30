//
// server.c - part of CWDebug, a source-level debugger for the AmigaOS
//            This file contains the routines for the debugger server.
//
// Copyright(C) 2018-2021 Constantin Wiemer
//


#include <dos/dos.h>
#include <proto/exec.h>
#include <string.h>

#include "debugger.h"
#include "serio.h"
#include "server.h"
#include "stdint.h"
#include "util.h"


static uint16_t g_msg_seqnum;


static void send_ack_msg(uint8_t *p_data, uint8_t data_len);
static void send_nack_msg(uint8_t error_code);
static void send_target_stopped_msg(TargetInfo *p_target_info);


//
// exported routines
//

// This is how a complete protocol message looks like:
//  ---------------------------------------------------
// | sequence number | checksum | message type | data |
//  ---------------------------------------------------
// The checksum is calculated in the same way as with IP / UDP headers.

// Programm flow when target is started by host:
// @startuml
// User -> Host: command 'run'
// Host -> Server: MSG_RUN
// Server -> Host: MSG_ACK
// Server -> Target: run_target()
// Target -> Target: target runs until a breakpoint / next instruction is hit
// Target -> Server: handle_breakpoint() / handle_single_step()
// Server -> Server: process_remote_commands()
// Server -> Host: MSG_TARGET_STOPPED
// Host -> Server: MSG_ACK
// Host -> User: display target infos and prompt
// User -> Host: command 'continue' / 'step'
// Host -> Server: MSG_CONT / MSG_STEP
// Server -> Host: MSG_ACK
// Server -> Target: returns to target
// Target -> Target: target runs until completion
// Target -> Server: returns to run_target()
// Server -> Server: process_remote_commands()
// Server -> Host: MSG_TARGET_STOPPED
// Host -> Server: MSG_ACK
// Host -> User: display target infos and prompt
// @enduml
//
void process_remote_commands(TaskContext *p_task_ctx)
{
    ProtoMessage msg;
    TargetInfo   target_info;

    // If target is running we've been called by one of the handle_* routines. In this case
    // the host is waiting for us and we send a MSG_TARGET_STOPPED message to indicate that
    // the target has stopped and provide the target information to the host.
    // TODO: Remove prefixes of struct members
    if (gp_dstate->ds_target_state & TS_RUNNING) {
        target_info.ti_target_state = gp_dstate->ds_target_state;
        target_info.ti_exit_code    = gp_dstate->ds_exit_code;
        memcpy(&target_info.ti_task_context, p_task_ctx, sizeof(TaskContext));
        send_target_stopped_msg(&target_info);
    }

    // TODO: catch Ctrl-C
    while(1) {
        LOG(INFO, "waiting for command from host...");
        if (recv_message(&msg) == DOSFALSE) {
            LOG(ERROR, "failed to receive message from host");
            quit_debugger(RETURN_ERROR);
        }
        LOG(
            DEBUG,
            "message from host received: seqnum=%d, type=%d, length=%d",
            msg.msg_seqnum,
            msg.msg_type,
            msg.msg_length
        );
        g_msg_seqnum = msg.msg_seqnum;


        //
        // target is not running (we've been called by main())
        //
        if (!(gp_dstate->ds_target_state & TS_RUNNING)) {
            switch (msg.msg_type) {
                case MSG_INIT:
                    LOG(DEBUG, "initializing connection");
                    send_ack_msg(NULL, 0);
                    break;

                case MSG_RUN:
                    send_ack_msg(NULL, 0);
                    run_target();
                    target_info.ti_target_state = gp_dstate->ds_target_state;
                    target_info.ti_exit_code    = gp_dstate->ds_exit_code;
                    send_target_stopped_msg(&target_info);
                    break;

                case MSG_QUIT:
                    LOG(DEBUG, "terminating connection");
                    send_ack_msg(NULL, 0);
                    quit_debugger(RETURN_OK);
                    break;

                default:
                    LOG(ERROR, "command %d not allowed when target is not running", msg.msg_type);
                    send_nack_msg(E_INVALID_TARGET_STATE);
            }
        }
    

        //
        // target is running (we've been called by one of the handle_* routines)
        //
        else {
            // TODO
        }
    }
}


//
// local routines
//

void send_ack_msg(uint8_t *p_data, uint8_t data_len)
{
    ProtoMessage msg;

    if (data_len > MAX_MSG_DATA_LEN) {
        LOG(CRIT, "Internal error: send_ack_msg() has been called with more than MAX_MSG_DATA_LEN data");
        quit_debugger(RETURN_FAIL);
    }
    msg.msg_seqnum = g_msg_seqnum;
    msg.msg_type   = MSG_ACK;
    msg.msg_length = data_len;
    memcpy(&msg.msg_data, p_data, data_len);
    if (send_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(RETURN_ERROR);
    }
    ++g_msg_seqnum;
}


void send_nack_msg(uint8_t error_code)
{
    ProtoMessage msg;
    msg.msg_seqnum  = g_msg_seqnum;
    msg.msg_type    = MSG_NACK;
    msg.msg_length  = 1;
    msg.msg_data[0] = error_code;
    if (send_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(RETURN_ERROR);
    }
    ++g_msg_seqnum;
}


void send_target_stopped_msg(TargetInfo *p_target_info)
{
    ProtoMessage msg;

    if (sizeof(TargetInfo) > MAX_MSG_DATA_LEN) {
        LOG(CRIT, "Internal error: send_target_stopped_msg() has been called with TargetInfo larger than MAX_MSG_DATA_LEN");
        quit_debugger(RETURN_FAIL);
    }
    msg.msg_seqnum = g_msg_seqnum;
    msg.msg_type   = MSG_TARGET_STOPPED;
    msg.msg_length = sizeof(TargetInfo);
    memcpy(&msg.msg_data, p_target_info, sizeof(TargetInfo));
    if (send_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(RETURN_ERROR);
    }

    // TODO: add timeout
    if (recv_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to receive message from host");
        quit_debugger(RETURN_ERROR);
    }
    if (msg.msg_type == MSG_ACK) {
        if (msg.msg_seqnum == g_msg_seqnum) {
            LOG(DEBUG, "Received ACK for MSG_TARGET_STOPPED message");
        }
        else {
            LOG(
                ERROR,
                "Received ACK for MSG_TARGET_STOPPED message with wrong sequence number, expected %d, got %d",
                g_msg_seqnum,
                msg.msg_seqnum
            );
            quit_debugger(RETURN_ERROR);
        }
    }
    else {
        LOG(ERROR, "Received unexpected message of type %d from host instead of the expected ACK", msg.msg_type);
        quit_debugger(RETURN_ERROR);
    }
}
