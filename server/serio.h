#ifndef CWDBG_SERIO_H
#define CWDBG_SERIO_H
//
// serio.h - part of cwdbg, a debugger for the AmigaOS
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include "stdint.h"


//
// constants
//
#define MAX_MSG_DATA_LEN 255
#define MAX_FRAME_SIZE 512      // should be large enough to hold a SLIP-encoded message + data


//
// type declarations
//
typedef struct SerialConnection {
    struct IOExtSer *p_io_request;
    uint32_t        errno;
} SerialConnection;



typedef struct Buffer {
    uint8_t  *p_addr;
    uint32_t size;
} Buffer;


//
// exported functions
//
SerialConnection *create_serial_conn();
void destroy_serial_conn(SerialConnection *p_conn);
int put_data_into_slip_frame(SerialConnection *p_conn, const Buffer *pb_data, Buffer *pb_frame);
int get_data_from_slip_frame(SerialConnection *p_conn, Buffer *pb_data, const Buffer *pb_frame);
int send_slip_frame(SerialConnection *p_conn, const Buffer *pb_frame);
int recv_slip_frame(SerialConnection *p_conn, Buffer *pb_frame);

#endif  // CWDBG_SERIO_H
