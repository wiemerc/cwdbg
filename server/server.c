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
#include "target.h"
#include "util.h"


//
// debugger protocol messages
//
#define MSG_INIT             0x00
#define MSG_ACK              0x01
#define MSG_NACK             0x02
#define MSG_RUN              0x03
#define MSG_QUIT             0x04
#define MSG_CONT             0x05
#define MSG_STEP             0x06
#define MSG_KILL             0x07
#define MSG_PEEK_MEM         0x08
#define MSG_POKE_MEM         0x09
#define MSG_SET_BPOINT       0x0a
#define MSG_CLEAR_BPOINT     0x0b
#define MSG_TARGET_STOPPED   0x0c
#define MSG_GET_BASE_ADDRESS 0x0d

//
// connection states - for future use
//
#define CONN_STATE_INITIAL   0
#define CONN_STATE_CONNECTED 1

#define MAX_LIB_NAME_LEN 64


struct HostConnection {
    SerialConnection *p_serial_conn;
    int              state;
    uint16_t         next_seq_num;
};

// This is how a complete protocol message looks like:
//  ----------------------------------------------------------------
// | sequence number | checksum | message type | data length | data |
//  ----------------------------------------------------------------
// The checksum is calculated in the same way as with IP / UDP headers.
// TODO: Make length field 16 bits wide to support bigger messages
struct ProtoMessage {
    uint16_t seqnum;
    uint16_t checksum;
    uint8_t  type;
    uint8_t  length;
    uint8_t  data[MAX_MSG_DATA_LEN];
};


static int send_message(HostConnection *p_conn, ProtoMessage *p_msg);
static int recv_message(HostConnection *p_conn, ProtoMessage *p_msg);
static void send_ack_msg(HostConnection *p_conn, uint8_t *p_data, uint8_t data_len);
static void send_nack_msg(HostConnection *p_conn, uint8_t error_code);
static void send_target_stopped_msg(HostConnection *p_conn, TargetInfo *p_target_info);

static int is_correct_target_state_for_command(uint32_t state, uint8_t msg_type);

static void handle_set_bpoint_msg(ProtoMessage *p_msg);
static void handle_clear_bpoint_msg(ProtoMessage *p_msg);
static void handle_get_base_address_msg(ProtoMessage *p_msg);
static void handle_peek_mem_msg(ProtoMessage *p_msg);


// keep aligned with definitions above
static char *msg_type_names[] = {
    "MSG_INIT",
    "MSG_ACK",
    "MSG_NACK",
    "MSG_RUN",
    "MSG_QUIT",
    "MSG_CONT",
    "MSG_STEP",
    "MSG_KILL",
    "MSG_PEEK_MEM",
    "MSG_POKE_MEM",
    "MSG_SET_BPOINT",
    "MSG_CLEAR_BPOINT",
    "MSG_TARGET_STOPPED",
    "MSG_GET_BASE_ADDRESS"
};


//
// exported routines
//

// This routine is called nested, once by main() as the central message loop of the debugger (the outer call),
// and by run_target() every time the target stops (the inner calls).
//
// TODO: Update flow with messages between target and debugger process
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
void process_remote_commands()
{
    ProtoMessage msg;
    TargetInfo   target_info;

    // If we've been called by run_target() the target is still running. In this case the host is waiting for us and we
    // send a MSG_TARGET_STOPPED message to indicate that the target has stopped and provide the target information to the host.
    LOG(DEBUG, "process_remote_commands() has been called");
    get_target_info(gp_dbg->p_target, &target_info);
    if (target_info.state & TS_RUNNING) {
        send_target_stopped_msg(gp_dbg->p_host_conn, &target_info);
    }

    // TODO: Catch Ctrl-C
    while(TRUE) {
        LOG(INFO, "Waiting for command from host...");
        // TODO: add timeout
        if (recv_message(gp_dbg->p_host_conn, &msg) == DOSFALSE) {
            LOG(ERROR, "Failed to receive message from host");
            quit_debugger(gp_dbg, RETURN_ERROR);
        }
        LOG(
            DEBUG,
            "Message from host received: seqnum=%d, type=%s (%d), length=%d",
            msg.seqnum,
            msg_type_names[msg.type],
            msg.type,
            msg.length
        );
        if (msg.seqnum != gp_dbg->p_host_conn->next_seq_num) {
            LOG(
                CRIT,
                "Internal error: Received message with wrong sequence number, expected %d, got %d",
                gp_dbg->p_host_conn->next_seq_num,
                msg.seqnum
            );
            quit_debugger(gp_dbg, RETURN_FAIL);
        }
        if (!is_correct_target_state_for_command(target_info.state, msg.type)) {
            LOG(CRIT, "Internal error: Target is in wrong state for command %d", msg.type);
            quit_debugger(gp_dbg, RETURN_FAIL);
        }

        switch (msg.type) {
            case MSG_INIT:
                LOG(DEBUG, "Initializing connection");
                gp_dbg->p_host_conn->state = CONN_STATE_CONNECTED;
                gp_dbg->p_host_conn->next_seq_num = msg.seqnum;
                send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
                break;

            case MSG_SET_BPOINT:
                handle_set_bpoint_msg(&msg);
                break;

            case MSG_CLEAR_BPOINT:
                handle_clear_bpoint_msg(&msg);
                break;

            case MSG_RUN:
                send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
                run_target(gp_dbg->p_target);
                get_target_info(gp_dbg->p_target, &target_info);
                send_target_stopped_msg(gp_dbg->p_host_conn, &target_info);
                break;

            case MSG_CONT:
                send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
                set_continue_mode(gp_dbg->p_target);
                return;

            case MSG_STEP:
                send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
                set_single_step_mode(gp_dbg->p_target);
                return;

            case MSG_KILL:
                send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
                kill_target(gp_dbg->p_target);
                // Return to run_target() so it can exit and the outer invocation can take over again (which will also
                // send the MSG_TARGET_STOPPED message)
                return;

            case MSG_GET_BASE_ADDRESS:
                handle_get_base_address_msg(&msg);
                break;

            case MSG_PEEK_MEM:
                handle_peek_mem_msg(&msg);
                break;

            case MSG_QUIT:
                send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
                quit_debugger(gp_dbg, RETURN_OK);

            default:
                LOG(CRIT, "Internal error: unknown command %d", msg.type);
                quit_debugger(gp_dbg, RETURN_FAIL);
        }
    }
}


HostConnection *create_host_conn()
{
    HostConnection *p_conn;

    if ((p_conn = AllocVec(sizeof(HostConnection), MEMF_CLEAR)) == NULL) {
        LOG(CRIT, "Failed to allocate memory for host connection object");
        return NULL;
    }
    if ((p_conn->p_serial_conn = create_serial_conn()) == NULL) {
        LOG(CRIT, "Failed to initialize serial connection");
        return NULL;
    }
    p_conn->state = CONN_STATE_INITIAL;
    return p_conn;
}


void destroy_host_conn(HostConnection *p_conn)
{
    destroy_serial_conn(p_conn->p_serial_conn);
    FreeVec(p_conn);
}


//
// local routines
//

static int send_message(HostConnection *p_conn, ProtoMessage *p_msg)
{
    uint8_t frame[MAX_FRAME_SIZE];
    Buffer b_msg, b_frame;

    // TODO: set checksum
    b_msg.p_addr = (uint8_t *) p_msg;
    b_msg.size   = sizeof(ProtoMessage);
    b_frame.p_addr = frame;
    b_frame.size   = MAX_FRAME_SIZE;
    if (put_data_into_slip_frame(p_conn->p_serial_conn, &b_msg, &b_frame) == DOSFALSE) {
        LOG(ERROR, "Could not put data into SLIP frame: %ld", p_conn->p_serial_conn->errno);
        return DOSFALSE;
    }
    if (send_slip_frame(p_conn->p_serial_conn, &b_frame) == DOSFALSE) {
        LOG(ERROR, "Failed to send SLIP frame: %ld", p_conn->p_serial_conn->errno);
        return DOSFALSE;
    }
    return DOSTRUE;
}


static int recv_message(HostConnection *p_conn, ProtoMessage *p_msg)
{
    uint8_t frame[MAX_FRAME_SIZE];
    Buffer b_msg, b_frame;

    b_msg.p_addr = (uint8_t *) p_msg;
    b_msg.size   = sizeof(ProtoMessage);
    b_frame.p_addr = frame;
    b_frame.size   = MAX_FRAME_SIZE;
    if (recv_slip_frame(p_conn->p_serial_conn, &b_frame) == DOSFALSE) {
        LOG(ERROR, "Failed to receive SLIP frame: %ld", p_conn->p_serial_conn->errno);
        return DOSFALSE;
    }
    if (get_data_from_slip_frame(p_conn->p_serial_conn, &b_msg, &b_frame) == DOSFALSE) {
        LOG(ERROR, "Could not get data from SLIP frame: %ld", p_conn->p_serial_conn->errno);
        return DOSFALSE;
    }
    // TODO: check checksum
    return DOSTRUE;
}


static void send_ack_msg(HostConnection *p_conn, uint8_t *p_data, uint8_t data_len)
{
    ProtoMessage msg;

    if (data_len > MAX_MSG_DATA_LEN) {
        LOG(CRIT, "Internal error: send_ack_msg() has been called with more than MAX_MSG_DATA_LEN data");
        quit_debugger(gp_dbg, RETURN_FAIL);
    }
    msg.seqnum = p_conn->next_seq_num;
    msg.type   = MSG_ACK;
    msg.length = data_len;
    memcpy(&msg.data, p_data, data_len);
    if (send_message(p_conn, &msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(gp_dbg, RETURN_ERROR);
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
    if (send_message(p_conn, &msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(gp_dbg, RETURN_ERROR);
    }
    p_conn->next_seq_num++;
}


static void send_target_stopped_msg(HostConnection *p_conn, TargetInfo *p_target_info)
{
    ProtoMessage msg;

    if (sizeof(TargetInfo) > MAX_MSG_DATA_LEN) {
        LOG(CRIT, "Internal error: send_target_stopped_msg() has been called with TargetInfo larger than MAX_MSG_DATA_LEN");
        quit_debugger(gp_dbg, RETURN_FAIL);
    }
    LOG(DEBUG, "Sending MSG_TARGET_STOPPED message to host");
    msg.seqnum = p_conn->next_seq_num;
    msg.type   = MSG_TARGET_STOPPED;
    msg.length = sizeof(TargetInfo);
    memcpy(&msg.data, p_target_info, sizeof(TargetInfo));
    if (send_message(p_conn, &msg) == DOSFALSE) {
        LOG(ERROR, "Failed to send message to host");
        quit_debugger(gp_dbg, RETURN_ERROR);
    }

    // TODO: add timeout
    if (recv_message(p_conn, &msg) == DOSFALSE) {
        LOG(ERROR, "Failed to receive message from host");
        quit_debugger(gp_dbg, RETURN_ERROR);
    }
    if (msg.type == MSG_ACK) {
        if (msg.seqnum == p_conn->next_seq_num) {
            LOG(DEBUG, "Received ACK for MSG_TARGET_STOPPED message");
            p_conn->next_seq_num++;
        }
        else {
            LOG(
                CRIT,
                "Internal error: Received ACK for MSG_TARGET_STOPPED message with wrong sequence number, expected %d, got %d",
                p_conn->next_seq_num,
                msg.seqnum
            );
            quit_debugger(gp_dbg, RETURN_FAIL);
        }
    }
    else {
        LOG(CRIT, "Internal error: Received unexpected message of type %d from host instead of the expected ACK", msg.type);
        quit_debugger(gp_dbg, RETURN_FAIL);
    }
}


static int is_correct_target_state_for_command(uint32_t state, uint8_t msg_type)
{
    if (!(state & TS_RUNNING) && (
        (msg_type == MSG_CONT) ||
        (msg_type == MSG_STEP) ||
        (msg_type == MSG_KILL)
    )) {
        LOG(ERROR, "Incorrect state for command %d: target is not yet running", msg_type);
        return 0;
    }
    if ((state & TS_RUNNING) && (
        (msg_type == MSG_INIT) ||
        (msg_type == MSG_RUN) ||
        (msg_type == MSG_QUIT)
    )) {
        LOG(ERROR, "Incorrect state for command %d: target is already / still running", msg_type);
        return 0;
    }
    return 1;
}


static void handle_set_bpoint_msg(ProtoMessage *p_msg)
{
    uint32_t bpoint_offset;
    uint16_t bpoint_type;
    uint8_t  dbg_errno;

    if (unpack_data(p_msg->data, p_msg->length, "!I!H", &bpoint_offset, &bpoint_type) == DOSTRUE) {
        if ((dbg_errno = set_breakpoint(gp_dbg->p_target, bpoint_offset, bpoint_type)) == NULL) {
            // TODO: Return breakpoint number
            send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
        }
        else {
            LOG(ERROR, "Failed to set breakpoint");
            send_nack_msg(gp_dbg->p_host_conn, dbg_errno);
        }
    }
    else {
        LOG(ERROR, "Failed to unpack data of MSG_SET_BPOINT message");
        send_nack_msg(gp_dbg->p_host_conn, ERROR_BAD_DATA);
    }
}


static void handle_clear_bpoint_msg(ProtoMessage *p_msg)
{
    uint32_t   bpoint_num;
    Breakpoint *p_bpoint;

    if (unpack_data(p_msg->data, p_msg->length, "!I", &bpoint_num) == DOSTRUE) {
        if ((p_bpoint = find_bpoint_by_num(gp_dbg->p_target, bpoint_num)) != NULL) {
            clear_breakpoint(gp_dbg->p_target, p_bpoint);
            send_ack_msg(gp_dbg->p_host_conn, NULL, 0);
        }
        else {
            LOG(ERROR, "Breakpoint #%d not found", bpoint_num);
            send_nack_msg(gp_dbg->p_host_conn, ERROR_UNKNOWN_BREAKPOINT);
        }
    }
    else {
        LOG(ERROR, "Failed to unpack data of MSG_CLEAR_BPOINT message");
        send_nack_msg(gp_dbg->p_host_conn, ERROR_BAD_DATA);
    }
}


static void handle_get_base_address_msg(ProtoMessage *p_msg)
{
    char lib_name[MAX_LIB_NAME_LEN];
    uint8_t msg_data[4];
    struct Library **pp_sys_base = (struct Library **) 4l, *p_lib_base;

    if (unpack_data(p_msg->data, p_msg->length, "64s", lib_name) == DOSTRUE) {
        if (strncmp(lib_name, "exec.library", MAX_LIB_NAME_LEN) == 0) {
            LOG(DEBUG, "Base address of exec.library = 0x%08x\n", *pp_sys_base);
            pack_data(msg_data, 4, "!I", *pp_sys_base);
            send_ack_msg(gp_dbg->p_host_conn, msg_data, 4);
        }
        else {
            if ((p_lib_base = OpenLibrary(lib_name, 0l)) != NULL) {
                LOG(DEBUG, "Base address of %s = 0x%08x\n", lib_name, p_lib_base);
                CloseLibrary(p_lib_base);
                pack_data(msg_data, 4, "!I", p_lib_base);
                send_ack_msg(gp_dbg->p_host_conn, msg_data, 4);
            }
            else {
                LOG(ERROR, "Could not open library %s\n", lib_name);
                send_nack_msg(gp_dbg->p_host_conn, ERROR_OPEN_LIB_FAILED);
            }
        }
    }
    else {
        LOG(ERROR, "Failed to unpack data of MSG_GET_BASE_ADDRESS message");
        send_nack_msg(gp_dbg->p_host_conn, ERROR_BAD_DATA);
    }
}


static void handle_peek_mem_msg(ProtoMessage *p_msg)
{
    uint32_t address;
    uint16_t nbytes;

    if (unpack_data(p_msg->data, p_msg->length, "!I!H", &address, &nbytes) == DOSTRUE) {
        if (nbytes > MAX_MSG_DATA_LEN) {
            LOG(ERROR, "Number of bytes %d exceeds maximum message data size %d", nbytes, MAX_MSG_DATA_LEN);
            send_nack_msg(gp_dbg->p_host_conn, ERROR_BAD_DATA);
        }
        if (address > (0xffffffff - nbytes)) {
            LOG(ERROR, "Invalid address 0x%08x, is greater than maximum address - %d", address, nbytes);
            send_nack_msg(gp_dbg->p_host_conn, ERROR_BAD_DATA);
        }
        // Due to the fact that all processes share the same address space in AmigaOS we can just pass address and
        // number of bytes to send_ack_msg(), which will copy the data and send it to the host.
        send_ack_msg(gp_dbg->p_host_conn, (uint8_t *) address, nbytes);
    }
    else {
        LOG(ERROR, "Failed to unpack data of MSG_PEEK_MEM message");
        send_nack_msg(gp_dbg->p_host_conn, ERROR_BAD_DATA);
    }
}
