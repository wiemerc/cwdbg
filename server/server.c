//
// server.c - part of CWDebug, a source-level debugger for the AmigaOS
//            This file contains the routines for the debugger server.
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include <dos/dos.h>
#include <proto/exec.h>
#include <string.h>

#include "debugger.h"
#include "serio.h"
#include "server.h"
#include "stdint.h"
#include "util.h"


static void init_host_conn(HostConnection *p_conn, uint16_t initial_seq_num);
static void send_ack_msg(HostConnection *p_conn, uint8_t *p_data, uint8_t data_len);
static void send_nack_msg(HostConnection *p_conn, uint8_t error_code);
static void send_target_stopped_msg(HostConnection *p_conn, TargetInfo *p_target_info);

static int is_correct_target_state_for_command(uint8_t msg_type);


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
// TODO: Create response objects and send them back to host
//
void process_remote_commands(TaskContext *p_task_ctx)
{
    static HostConnection host_conn;
    ProtoMessage msg;
    TargetInfo   target_info;
    uint8_t      dbg_errno;
    BreakPoint   *p_bpoint;

    // If we've been called by one of the handle_* routines, we get a task context and the target is still
    // running. In this case the host is waiting for us and we send a MSG_TARGET_STOPPED message to indicate
    // that the target has stopped and provide the target information to the host.
    if (p_task_ctx) {
        get_target_info(&g_dstate, &target_info, p_task_ctx);
        send_target_stopped_msg(&host_conn, &target_info);
    }

    // TODO: Catch Ctrl-C
    while(1) {
        LOG(INFO, "Waiting for command from host...");
        // TODO: add timeout
        if (recv_message(&msg) == DOSFALSE) {
            LOG(ERROR, "Failed to receive message from host");
            quit_debugger(&g_dstate, RETURN_ERROR);
        }
        // TODO: Log message type as string
        LOG(
            DEBUG,
            "Message from host received: seqnum=%d, type=%d, length=%d",
            msg.seqnum,
            msg.type,
            msg.length
        );
        if (msg.seqnum != host_conn.next_seq_num) {
            LOG(
                ERROR,
                "Received message with wrong sequence number, expected %d, got %d",
                host_conn.next_seq_num,
                msg.seqnum
            );
            quit_debugger(&g_dstate, RETURN_ERROR);
        }
        if (!is_correct_target_state_for_command(msg.type)) {
            quit_debugger(&g_dstate, RETURN_ERROR);
        }

        switch (msg.type) {
            case MSG_INIT:
                LOG(DEBUG, "Initializing connection");
                init_host_conn(&host_conn, msg.seqnum);
                send_ack_msg(&host_conn, NULL, 0);
                break;

            case MSG_SET_BP:
                if ((dbg_errno = set_breakpoint(&g_dstate, *(uint32_t *) msg.data)) == 0) {
                    // TODO: Return breakpoint number
                    send_ack_msg(&host_conn, NULL, 0);
                }
                else {
                    LOG(ERROR, "Failed to set breakpoint");
                    send_nack_msg(&host_conn, dbg_errno);
                }
                break;

            case MSG_CLEAR_BP:
                if ((p_bpoint = find_bpoint_by_num(&g_dstate, *(uint32_t *) msg.data)) != NULL) {
                    clear_breakpoint(&g_dstate, p_bpoint);
                    send_ack_msg(&host_conn, NULL, 0);
                }
                else {
                    LOG(ERROR, "Breakpoint #%d not found", *((uint32_t *) msg.data));
                    send_nack_msg(&host_conn, ERROR_UNKNOWN_BREAKPOINT);
                }
                break;


            case MSG_RUN:
                send_ack_msg(&host_conn, NULL, 0);
                run_target(&g_dstate);
                get_target_info(&g_dstate, &target_info, NULL);
                send_target_stopped_msg(&host_conn, &target_info);
                break;

            case MSG_CONT:
                send_ack_msg(&host_conn, NULL, 0);
                set_continue_mode(&g_dstate, p_task_ctx);
                return;

            case MSG_STEP:
                send_ack_msg(&host_conn, NULL, 0);
                set_single_step_mode(&g_dstate, p_task_ctx);
                return;

            case MSG_KILL:
                send_ack_msg(&host_conn, NULL, 0);
                kill_target(&g_dstate);
                get_target_info(&g_dstate, &target_info, NULL);
                send_target_stopped_msg(&host_conn, &target_info);
                break;

            case MSG_QUIT:
                send_ack_msg(&host_conn, NULL, 0);
                quit_debugger(&g_dstate, RETURN_OK);  // will not return

            default:
                LOG(CRIT, "Internal error: unknown command %d", msg.type);
                quit_debugger(&g_dstate, RETURN_FAIL);  // will not return
        }
    }
}


//
// local routines
//

static void init_host_conn(HostConnection *p_conn, uint16_t initial_seq_num)
{
    p_conn->state = CONN_STATE_INITIAL;
    p_conn->next_seq_num = initial_seq_num;
}


static void send_ack_msg(HostConnection *p_conn, uint8_t *p_data, uint8_t data_len)
{
    ProtoMessage msg;

    if (data_len > MAX_MSG_DATA_LEN) {
        LOG(CRIT, "Internal error: send_ack_msg() has been called with more than MAX_MSG_DATA_LEN data");
        quit_debugger(&g_dstate, RETURN_FAIL);
    }
    msg.seqnum = p_conn->next_seq_num;
    msg.type   = MSG_ACK;
    msg.length = data_len;
    memcpy(&msg.data, p_data, data_len);
    if (send_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(&g_dstate, RETURN_ERROR);
    }
    p_conn->next_seq_num++;
}


static void send_nack_msg(HostConnection *p_conn, uint8_t error_code)
{
    ProtoMessage msg;
    msg.seqnum  = p_conn->next_seq_num;
    msg.type    = MSG_NACK;
    msg.length  = 1;
    msg.data[0] = error_code;
    if (send_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(&g_dstate, RETURN_ERROR);
    }
    p_conn->next_seq_num++;
}


static void send_target_stopped_msg(HostConnection *p_conn, TargetInfo *p_target_info)
{
    ProtoMessage msg;

    if (sizeof(TargetInfo) > MAX_MSG_DATA_LEN) {
        LOG(CRIT, "Internal error: send_target_stopped_msg() has been called with TargetInfo larger than MAX_MSG_DATA_LEN");
        quit_debugger(&g_dstate, RETURN_FAIL);
    }
    LOG(DEBUG, "Sending MSG_TARGET_STOPPED message to host");
    msg.seqnum = p_conn->next_seq_num;
    msg.type   = MSG_TARGET_STOPPED;
    msg.length = sizeof(TargetInfo);
    memcpy(&msg.data, p_target_info, sizeof(TargetInfo));
    if (send_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(&g_dstate, RETURN_ERROR);
    }

    // TODO: add timeout
    if (recv_message(&msg) == DOSFALSE) {
        LOG(ERROR, "Failed to receive message from host");
        quit_debugger(&g_dstate, RETURN_ERROR);
    }
    if (msg.type == MSG_ACK) {
        if (msg.seqnum == p_conn->next_seq_num) {
            LOG(DEBUG, "Received ACK for MSG_TARGET_STOPPED message");
            p_conn->next_seq_num++;
        }
        else {
            LOG(
                ERROR,
                "Received ACK for MSG_TARGET_STOPPED message with wrong sequence number, expected %d, got %d",
                p_conn->next_seq_num,
                msg.seqnum
            );
            quit_debugger(&g_dstate, RETURN_ERROR);
        }
    }
    else {
        LOG(ERROR, "Received unexpected message of type %d from host instead of the expected ACK", msg.type);
        quit_debugger(&g_dstate, RETURN_ERROR);
    }
}


static int is_correct_target_state_for_command(uint8_t msg_type)
{
    if (!(get_target_state(&g_dstate) & TS_RUNNING) && (
        (msg_type == MSG_CONT) ||
        (msg_type == MSG_STEP) ||
        (msg_type == MSG_KILL)
    )) {
        LOG(ERROR, "Incorrect state for command %d: target is not yet running", msg_type);
        return 0;
    }
    if ((get_target_state(&g_dstate) & TS_RUNNING) && (
        (msg_type == MSG_INIT) ||
        (msg_type == MSG_RUN) ||
        (msg_type == MSG_QUIT)
    )) {
        LOG(ERROR, "Incorrect state for command %d: target is already / still running", msg_type);
        return 0;
    }
    return 1;
}

