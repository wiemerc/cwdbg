#
# serio.py - part of CWDebug, a source-level debugger for the AmigaOS
#            This file contains the classes for the serial communication with the server.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import socket

from ctypes import BigEndianStructure, c_uint8, c_uint16, sizeof
from enum import IntEnum
from loguru import logger
from typing import Optional


#
# constants
#
MAX_FRAME_SIZE = 4096       # maximum number of bytes we try to read at once

# SLIP special characters
SLIP_END           = b'\xc0'
SLIP_ESCAPED_END   = b'\xdc'
SLIP_ESC           = b'\xdb'
SLIP_ESCAPED_ESC   = b'\xdd'


class Opcodes(IntEnum):
    OP_INIT      = 0
    OP_RUN       = 1
    OP_QUIT      = 2
    OP_KILL      = 3
    OP_CONT      = 4
    OP_STEP      = 5
    OP_READ_MEM  = 6
    OP_READ_REG  = 7
    OP_READ_REGS = 8
    OP_SET_BP    = 9
    OP_CLEAR_BP  = 10


class ProtoMessage(BigEndianStructure):
    _fields_ = (
        ("msg_seqnum", c_uint16),
        ("msg_checksum", c_uint16),
        ("msg_opcode", c_uint8),
        ("msg_length", c_uint8)
        # field msg_data omitted because we just append the data
    )


class ServerConnection:
    def __init__(self, host: str, port: int):
        try:
            self._conn = socket.create_connection((host, port))
            self._next_seqnum = 1
        except ConnectionRefusedError as e:
            raise RuntimeError(f"could not connect to server '{host}:{port}'") from e
        logger.debug("sending OP_INIT request to server")
        self.send_request(Opcodes.OP_INIT, 'hello'.encode() + b'\x00')
        opcode, data = self.recv_response()
        logger.debug(f"response received from server: opcode={hex(opcode)}, data={data}")


    def send_request(self, opcode: c_uint8, data: Optional[bytes] = None):
        try:
            msg = ProtoMessage(
                msg_seqnum=self._next_seqnum,
                msg_checksum=0xdead,
                msg_opcode=opcode,
                msg_length=len(data)
            )
            buffer = bytearray(msg)
            if data:
                buffer += data

            # SLIP-encode buffer and add end-of-frame marker
            buffer = buffer.replace(SLIP_ESC, SLIP_ESC + SLIP_ESCAPED_ESC)
            buffer = buffer.replace(SLIP_END, SLIP_ESC + SLIP_ESCAPED_END)
            buffer += SLIP_END

            self._conn.send(buffer)
            self._next_seqnum += 1
        except Exception as e:
            raise RuntimeError(f"could not send request to server: {e}") from e


    def recv_response(self):
        try:
            # check if there is already a complete SLIP frame in the buffer, if not 
            # read data from the connection until we have a complete frame
            buffer = bytearray()
            pos    = -1
            while pos == -1:
                buffer += self._conn.recv(MAX_FRAME_SIZE)
                pos = buffer.find(SLIP_END)

            # SLIP-decode buffer
            buffer = buffer.replace(SLIP_ESC + SLIP_ESCAPED_END, SLIP_END)
            buffer = buffer.replace(SLIP_ESC + SLIP_ESCAPED_ESC, SLIP_ESC)

            msg = ProtoMessage.from_buffer(buffer)
            data = buffer[sizeof(ProtoMessage) : sizeof(ProtoMessage) + msg.msg_length]
            return msg.msg_opcode, data
        except Exception as e:
            raise RuntimeError(f"could not read response from server: {exc}") from e


    def close(self):
        self._conn.close()