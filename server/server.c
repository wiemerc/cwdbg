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
    ProtoMessage *p_in_msg;
    TargetInfo    target_info;

    if ((p_in_msg = create_message()) == NULL) {
        LOG(ERROR, "could not allocate memory for message");
        quit_debugger(RETURN_ERROR);
    }

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
        if (recv_message(p_in_msg) == DOSFALSE) {
            LOG(ERROR, "failed to receive message from host");
            delete_message(p_in_msg);
            quit_debugger(RETURN_ERROR);
        }
        LOG(
            DEBUG,
            "message from host received: seqnum=%d, type=%d, length=%d",
            p_in_msg->msg_seqnum,
            p_in_msg->msg_type,
            p_in_msg->msg_length
        );
        // TODO: ignore messages with a seq number already seen
        g_msg_seqnum = p_in_msg->msg_seqnum;


        //
        // target is not running (we've been called by main())
        //
        if (!(gp_dstate->ds_target_state & TS_RUNNING)) {
            switch (p_in_msg->msg_type) {
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
                    LOG(ERROR, "command %d not allowed when target is not running", p_in_msg->msg_type);
                    send_nack_msg(E_INVALID_TARGET_STATE);
            }
        }
    

        //
        // target is running (we've been called by one of the handle_* routines)
        //
        if (gp_dstate->ds_target_state & TS_RUNNING) {
            // TODO
        }
    }
    delete_message(p_in_msg);
}


//
// local routines
//

void send_ack_msg(uint8_t *p_data, uint8_t data_len)
{
    ProtoMessage *p_msg;
    if ((p_msg = create_message()) == NULL) {
        LOG(ERROR, "could not allocate memory for message");
        quit_debugger(RETURN_ERROR);
    }
    p_msg->msg_seqnum   = g_msg_seqnum;
    p_msg->msg_type     = MSG_ACK;
    p_msg->msg_length   = data_len;
    memcpy(&p_msg->msg_data, p_data, data_len);
    if (send_message(p_msg) == DOSFALSE) {
        LOG(ERROR, "failed to send message to host");
        delete_message(p_msg);
        quit_debugger(RETURN_ERROR);
    }
    delete_message(p_msg);
    ++g_msg_seqnum;
}


void send_nack_msg(uint8_t error_code)
{
    ProtoMessage *p_msg;
    if ((p_msg = create_message()) == NULL) {
        LOG(ERROR, "could not allocate memory for message");
        quit_debugger(RETURN_ERROR);
    }
    p_msg->msg_seqnum   = g_msg_seqnum;
    p_msg->msg_type     = MSG_NACK;
    p_msg->msg_length   = 1;
    p_msg->msg_data[0]  = error_code;
    if (send_message(p_msg) == DOSFALSE) {
        LOG(ERROR, "failed to send message to host");
        delete_message(p_msg);
        quit_debugger(RETURN_ERROR);
    }
    delete_message(p_msg);
    ++g_msg_seqnum;
}


void send_target_stopped_msg(TargetInfo *p_target_info)
{
    ProtoMessage *p_msg;

    if ((p_msg = create_message()) == NULL) {
        LOG(ERROR, "could not allocate memory for message");
        quit_debugger(RETURN_ERROR);
    }

    p_msg->msg_seqnum   = g_msg_seqnum;
    p_msg->msg_type     = MSG_TARGET_STOPPED;
    p_msg->msg_length   = sizeof(TargetInfo);
    memcpy(&p_msg->msg_data, p_target_info,sizeof(TargetInfo));
    if (send_message(p_msg) == DOSFALSE) {
        LOG(ERROR, "failed to send message to host");
        delete_message(p_msg);
        quit_debugger(RETURN_ERROR);
    }

    // TODO: add timeout and re-transmit
    if (recv_message(p_msg) == DOSFALSE) {
        LOG(ERROR, "failed to receive message from host");
        delete_message(p_msg);
        quit_debugger(RETURN_ERROR);
    }
    if (p_msg->msg_type == MSG_ACK) {
        if (p_msg->msg_seqnum == g_msg_seqnum) {
            LOG(DEBUG, "received ACK for MSG_TARGET_STOPPED message");
        }
        else {
            LOG(
                ERROR,
                "received ACK for MSG_TARGET_STOPPED message with wrong sequence number, expected %d, got %d",
                g_msg_seqnum,
                p_msg->msg_seqnum
            );
            quit_debugger(RETURN_ERROR);
        }
    }
    else {
        LOG(ERROR, "received unexpected message of type %d from host instead of the expected ACK", p_msg->msg_type);
        quit_debugger(RETURN_ERROR);
    }

    delete_message(p_msg);
}
