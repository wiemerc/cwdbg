#
# serio.py - part of CWDebug, a source-level debugger for the AmigaOS
#            This file contains the classes for the serial communication with the server.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import socket

from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32, sizeof
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


class MsgTypes(IntEnum):
    MSG_INIT           = 0
    MSG_RUN            = 1
    MSG_QUIT           = 2
    MSG_CONT           = 3
    MSG_STEP           = 4
    MSG_KILL           = 5
    MSG_PEEK_MEM       = 6
    MSG_POKE_MEM       = 7
    MSG_SET_BP         = 8
    MSG_CLEAR_BP       = 9
    MSG_TARGET_STOPPED = 10


class ProtoMessage(BigEndianStructure):
    _fields_ = (
        ("msg_seqnum", c_uint16),
        ("msg_checksum", c_uint16),
        ("msg_type", c_uint8),
        ("msg_length", c_uint8)
        # field msg_data omitted because we just append the data
    )


# TODO: move classes to debugger.py or similiar
class TaskContext(BigEndianStructure):
    _pack_ = 2
    _fields_ = (
        ('tc_reg_sp', c_uint32),
        ('tc_exc_num', c_uint32),
        ('tc_reg_sr', c_uint16),
        ('tc_reg_pc', c_uint32),
        ('tc_reg_d', c_uint32 * 8),
        ('tc_reg_a', c_uint32 * 7)
    )


class TargetInfo(BigEndianStructure):
    _pack_ = 2
    _fields_ = (
        ('ti_task_context', TaskContext),
        ('ti_target_state', c_uint32),
        ('ti_exit_code', c_uint32)
    )


class ConnectionError(Exception):
    pass


class ServerConnection:
    def __init__(self, host: str, port: int):
        try:
            self._conn = socket.create_connection((host, port))
            self._next_seqnum = 1
        except ConnectionRefusedError as e:
            raise RuntimeError(f"could not connect to server '{host}:{port}'") from e
        logger.debug("sending MSG_INIT message to server")
        self.send_message(MsgTypes.MSG_INIT, 'hello'.encode() + b'\x00')
        msgtype, data = self.recv_message()


    def send_message(self, msgtype: c_uint8, data: Optional[bytes] = None):
        try:
            msg = ProtoMessage(
                msg_seqnum=self._next_seqnum,
                msg_checksum=0xdead,
                msg_type=msgtype,
                msg_length=len(data) if data else 0
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
            raise ConnectionError(f"could not send message to server: {e}") from e


    def recv_message(self):
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
            logger.debug("message from server received: seqnum={}, checksum={}, type={}, length={}".format(
                msg.msg_seqnum,
                msg.msg_checksum,
                msg.msg_type,
                msg.msg_length
            ))
            return msg.msg_type, data
        except Exception as e:
            raise ConnectionError(f"could not read message from server: {exc}") from e


    def close(self):
        self._conn.close()