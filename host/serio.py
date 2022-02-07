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

from debugger import TargetInfo


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
    MSG_ACK            = 1
    MSG_NACK           = 2
    MSG_RUN            = 3
    MSG_QUIT           = 4
    MSG_CONT           = 5
    MSG_STEP           = 6
    MSG_KILL           = 7
    MSG_PEEK_MEM       = 8
    MSG_POKE_MEM       = 9
    MSG_SET_BP         = 10
    MSG_CLEAR_BP       = 11
    MSG_TARGET_STOPPED = 12


class ProtoMessage(BigEndianStructure):
    _fields_ = (
        ("seqnum", c_uint16),
        ("checksum", c_uint16),
        ("type", c_uint8),
        ("length", c_uint8)
        # Field data omitted because we just append the data
    )


class ConnectionError(Exception):
    pass


class ServerConnection:
    def __init__(self, host: str, port: int):
        try:
            self._conn = socket.create_connection((host, port))
            self._next_seqnum = 0
        except ConnectionRefusedError as e:
            raise RuntimeError(f"Could not connect to server '{host}:{port}'") from e
        logger.debug("Sending MSG_INIT message to server")
        self.execute_command(MsgTypes.MSG_INIT)


    # TODO: Move class / this method to debugger.py just keep send_message() / receive_message() here
    def execute_command(self, command: c_uint8, data: Optional[bytes] = None) -> Optional[TargetInfo]:
        self.send_message(command, data)
        msg, data = self.recv_message()
        if msg.type == MsgTypes.MSG_ACK:
            if msg.seqnum == self._next_seqnum:
                logger.debug(f"Received ACK for message {MsgTypes(command).name}")
                self._next_seqnum += 1
            else:
                raise ConnectionError(
                    "Received ACK for message {} with wrong sequence number, expected {}, got {}".format(
                        MsgTypes(command).name,
                        self._next_seqnum,
                        msg.seqnum
                    )
                )
        else:
            raise ConnectionError(f"Received unexpected message of type {MsgTypes(msg.type).name} from server instead of the expected ACK")

        # If we just sent a command that causes the target to run, we need to wait for the MSG_TARGET_STOPPED message.
        if command in (MsgTypes.MSG_RUN, MsgTypes.MSG_STEP, MsgTypes.MSG_CONT):
            logger.info("Target is running, waiting for it to stop...")
            msg, data = self.recv_message()
            if msg.type == MsgTypes.MSG_TARGET_STOPPED:
                self.send_message(MsgTypes.MSG_ACK)
                target_info = TargetInfo.from_buffer(data)
                logger.info(f"Target has stopped, state = {target_info.target_state}")
                return target_info
            else:
                raise ConnectionError(f"Received unexpected message {MsgTypes(msg.type).name} from server, expected MSG_TARGET_STOPPED")



    def send_message(self, msg_type: c_uint8, data: Optional[bytes] = None):
        try:
            msg = ProtoMessage(
                seqnum=self._next_seqnum,
                checksum=0xdead,
                type=msg_type,
                length=len(data) if data else 0
            )
            buffer = bytearray(msg)
            if data:
                buffer += data

            # SLIP-encode buffer and add end-of-frame marker
            buffer = buffer.replace(SLIP_ESC, SLIP_ESC + SLIP_ESCAPED_ESC)
            buffer = buffer.replace(SLIP_END, SLIP_ESC + SLIP_ESCAPED_END)
            buffer += SLIP_END

            self._conn.send(buffer)
            if msg_type in (MsgTypes.MSG_ACK, MsgTypes.MSG_NACK):
                self._next_seqnum += 1
        except Exception as e:
            raise ConnectionError(f"Could not send message to server") from e


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
            data = buffer[sizeof(ProtoMessage) : sizeof(ProtoMessage) + msg.length]
            logger.debug("Message from server received: seqnum={}, checksum={}, type={}, length={}".format(
                msg.seqnum,
                hex(msg.checksum),
                MsgTypes(msg.type).name,
                msg.length
            ))
            return msg, data
        except Exception as e:
            raise ConnectionError(f"Could not read message from server") from e


    def close(self):
        self._conn.close()