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
#include "util.h"
#include "stdint.h"


static ProtoMessage *create_proto_msg();
static void delete_proto_msg(ProtoMessage *p_msg);
static int32_t recv_request(ProtoMessage *p_msg);
static int32_t send_response(ProtoMessage *p_msg);


//
// exported routines
//

void process_remote_commands(TaskContext *p_taks_ctx)
{
    ProtoMessage *p_msg;

    LOG(INFO, "waiting for host to connect...");
    if ((p_msg = create_proto_msg()) == NULL) {
        LOG(ERROR, "could not allocate memory for request");
        return;
    }
    if (recv_request(p_msg) == DOSFALSE) {
        LOG(ERROR, "failed to receive request");
        return;
    }
    LOG(
        DEBUG,
        "request received: seqnum=%d, checksum=%x, opcode=%d, length=%d, data=%s",
        p_msg->msg_seqnum,
        p_msg->msg_checksum,
        p_msg->msg_opcode,
        p_msg->msg_length,
        (char *) p_msg->msg_data
    );
    p_msg->msg_opcode |= OP_RESPONSE_FLAG;
    if (send_response(p_msg) == DOSFALSE) {
        LOG(ERROR, "failed to send response");
        return;
    }
    delete_proto_msg(p_msg);
}


//
// local routines
//

static ProtoMessage *create_proto_msg()
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


static void delete_proto_msg(ProtoMessage *p_msg)
{
    FreeVec((void *) p_msg);
}


static int32_t recv_request(ProtoMessage *p_msg)
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


static int32_t send_response(ProtoMessage *p_msg)
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
