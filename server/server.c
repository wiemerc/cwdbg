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


static ProtoMessage *create_message();
static void delete_message(ProtoMessage *p_msg);
static int32_t send_message(ProtoMessage *p_msg);
static int32_t recv_message(ProtoMessage *p_msg);


//
// exported routines
//

void process_remote_commands(TaskContext *p_task_ctx)
{
    ProtoMessage *p_msg;
    TargetInfo    tinfo;

    if ((p_msg = create_message()) == NULL) {
        LOG(ERROR, "could not allocate memory for message");
        abort_debugger();
    }

    // If target is running we've been called by one of the handle_* routines. In this case
    // the host is waiting for us and we send a MSG_TARGET_STOPPED message to indicate that
    // the target has stopped and provide the target information to the host.
    if (gp_dstate->ds_target_state & TS_RUNNING) {
        tinfo.ti_target_state = gp_dstate->ds_target_state;
        tinfo.ti_exit_code    = gp_dstate->ds_exit_code;
        memcpy(&tinfo.ti_task_context, p_task_ctx, sizeof(TaskContext));
        p_msg->msg_seqnum   = g_msg_seqnum;
        p_msg->msg_checksum = 0xbeef;
        p_msg->msg_type     = MSG_TARGET_STOPPED;
        p_msg->msg_length   = sizeof(TargetInfo);
        memcpy(&p_msg->msg_data, &tinfo, sizeof(TargetInfo));
        if (send_message(p_msg) == DOSFALSE) {
            LOG(ERROR, "failed to send message to host");
            delete_message(p_msg);
            abort_debugger();
        }
    }

    // TODO: check sequence number and checksum
    // TODO: catch Ctrl-C
    while(1) {
        LOG(INFO, "waiting for message from host...");
        if (recv_message(p_msg) == DOSFALSE) {
            LOG(ERROR, "failed to receive message from host");
            delete_message(p_msg);
            abort_debugger();
        }
        LOG(
            DEBUG,
            "message from host received: seqnum=%d, checksum=%x, type=%d, length=%d",
            p_msg->msg_seqnum,
            p_msg->msg_checksum,
            p_msg->msg_type,
            p_msg->msg_length
        );
        g_msg_seqnum = p_msg->msg_seqnum;


        //
        // target is not running (we've been called by main())
        //
        if (!(gp_dstate->ds_target_state & TS_RUNNING)) {
            switch (p_msg->msg_type) {
                case MSG_INIT:
                    LOG(DEBUG, "initializing connection, ISN=%d", p_msg->msg_seqnum);
                    goto send_response_and_continue;
                    break;

                case MSG_RUN:
                    run_target();
                    tinfo.ti_target_state = gp_dstate->ds_target_state;
                    tinfo.ti_exit_code    = gp_dstate->ds_exit_code;
                    p_msg->msg_seqnum   = g_msg_seqnum;
                    p_msg->msg_checksum = 0xbeef;
                    p_msg->msg_type     = MSG_TARGET_STOPPED;
                    p_msg->msg_length   = sizeof(TargetInfo);
                    memcpy(&p_msg->msg_data, &tinfo, sizeof(TargetInfo));
                    if (send_message(p_msg) == DOSFALSE) {
                        LOG(ERROR, "failed to send message to host");
                        delete_message(p_msg);
                        abort_debugger();
                    }
                    break;

                case MSG_QUIT:
                    LOG(DEBUG, "terminating connection");
                    goto send_response_and_quit;
                    break;

                default:
                    LOG(ERROR, "command %d not allowed when target is not running", p_msg->msg_type);
                    p_msg->msg_type |= MSG_ERROR_FLAG;
                    goto send_response_and_continue;
            }
        }
    

        //
        // target is running (we've been called by one of the handle_* routines)
        //
        if (gp_dstate->ds_target_state & TS_RUNNING) {
            // TODO
        }


        send_response_and_continue:
            if (send_message(p_msg) == DOSFALSE) {
                LOG(ERROR, "failed to send message to host");
                delete_message(p_msg);
                abort_debugger();
            }
            continue;
        send_response_and_quit:
            if (send_message(p_msg) == DOSFALSE) {
                LOG(ERROR, "failed to send message to host");
                delete_message(p_msg);
                abort_debugger();
            }
            break;
    }
    delete_message(p_msg);
}


//
// local routines
//

static ProtoMessage *create_message()
{
    ProtoMessage *p_msg;

    // allocate a memory block large enough for the protocol message header and any data
    if ((p_msg = AllocVec(sizeof(ProtoMessage) + MAX_MSG_DATA_LEN, 0)) != NULL) {
        p_msg->msg_length = MAX_MSG_DATA_LEN;
        return p_msg;
    }
    else
        return NULL;
}


static void delete_message(ProtoMessage *p_msg)
{
    FreeVec((void *) p_msg);
}


static int32_t send_message(ProtoMessage *p_msg)
{
    uint8_t *p_frame;
    Buffer b_msg, b_frame;

    if ((p_frame = AllocVec(MAX_FRAME_SIZE, 0)) == NULL) {
        LOG(ERROR, "could not allocate memory for SLIP frame");
        return DOSFALSE;
    }
    b_msg.p_addr = (uint8_t *) p_msg;
    b_msg.size   = sizeof(ProtoMessage) + MAX_MSG_DATA_LEN;
    b_frame.p_addr = p_frame;
    b_frame.size   = MAX_FRAME_SIZE;
    if (put_data_into_slip_frame(&b_msg, &b_frame) == DOSFALSE) {
        LOG(ERROR, "could not put data into SLIP frame: %ld", g_serio_errno);
        FreeVec((void *) p_frame);
        return DOSFALSE;
    }
    if (send_slip_frame(&b_frame) == DOSFALSE) {
        LOG(ERROR, "failed to send SLIP frame: %ld", g_serio_errno);
        return DOSFALSE;
    }
    return DOSTRUE;
}


static int32_t recv_message(ProtoMessage *p_msg)
{
    uint8_t *p_frame;
    Buffer b_msg, b_frame;

    if ((p_frame = AllocVec(MAX_FRAME_SIZE, 0)) == NULL) {
        LOG(ERROR, "could not allocate memory for SLIP frame");
        return DOSFALSE;
    }
    b_msg.p_addr = (uint8_t *) p_msg;
    b_msg.size   = sizeof(ProtoMessage) + MAX_MSG_DATA_LEN;
    b_frame.p_addr = p_frame;
    b_frame.size   = MAX_FRAME_SIZE;
    if (recv_slip_frame(&b_frame) == DOSFALSE) {
        LOG(ERROR, "failed to receive SLIP frame: %ld", g_serio_errno);
        FreeVec((void *) p_frame);
        return DOSFALSE;
    }
    if (get_data_from_slip_frame(&b_msg, &b_frame) == DOSFALSE) {
        LOG(ERROR, "could not get data from SLIP frame: %ld", g_serio_errno);
        FreeVec((void *) p_frame);
        return DOSFALSE;
    }
    FreeVec((void *) p_frame);
    return DOSTRUE;
}


/*
 * calculate IP / ICMP checksum (taken from the code for in_cksum() floating on the net)
 */
static USHORT calc_checksum(const UBYTE * bytes, ULONG len)
{
    ULONG sum, i;
    USHORT * p;

    sum = 0;
    p = (USHORT *) bytes;

    for (i = len; i > 1; i -= 2)                /* sum all 16-bit words */
        sum += *p++;

    if (i == 1)                                 /* add an odd byte if necessary */
        sum += (USHORT) *((UBYTE *) p);

    sum = (sum >> 16) + (sum & 0x0000ffff);     /* fold in upper 16 bits */
    sum += (sum >> 16);                         /* add carry bits */
    return ~((USHORT) sum);                     /* return 1-complement truncated to 16 bits */
}