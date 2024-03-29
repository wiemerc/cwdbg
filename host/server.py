#
# server.py - part of cwdbg, a debugger for the AmigaOS
#             This file contains the classes for the serial communication with the server.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import socket
import struct
from dataclasses import dataclass
from ctypes import BigEndianStructure, c_uint8, c_uint16, sizeof
from enum import IntEnum
from loguru import logger

# We can't use from target import ... because of the circular import target.py <-> server.py.
import target
from errors import ErrorCodes


#
# constants
#
MAX_FRAME_SIZE = 4096       # maximum number of bytes we try to read at once
MAX_MSG_DATA_LEN = 255      # maximum number of bytes a message can carry

M68K_UINT16 = '>H'
M68K_UINT32 = '>I'

# SLIP special characters
SLIP_END           = b'\xc0'
SLIP_ESCAPED_END   = b'\xdc'
SLIP_ESC           = b'\xdb'
SLIP_ESCAPED_ESC   = b'\xdd'


class MsgTypes(IntEnum):
    MSG_INIT             = 0
    MSG_ACK              = 1
    MSG_NACK             = 2
    MSG_RUN              = 3
    MSG_QUIT             = 4
    MSG_CONT             = 5
    MSG_STEP             = 6
    MSG_KILL             = 7
    MSG_PEEK_MEM         = 8
    MSG_POKE_MEM         = 9
    MSG_SET_BPOINT       = 10
    MSG_CLEAR_BPOINT     = 11
    MSG_TARGET_STOPPED   = 12
    MSG_GET_BASE_ADDRESS = 13


class ProtoMessage(BigEndianStructure):
    _fields_ = (
        ("seqnum", c_uint16),
        ("checksum", c_uint16),
        ("type", c_uint8),
        ("length", c_uint8)
        # field 'data' omitted because we just append the data
    )


class ConnectionError(RuntimeError):
    pass


class ServerCommandError(RuntimeError):
    pass


class ServerConnection:
    def __init__(self, host: str, port: int):
        logger.info("Connecting to server...")
        try:
            self._conn = socket.create_connection((host, port))
            self._next_seqnum = 0
        except ConnectionRefusedError as e:
            raise RuntimeError(f"Could not connect to server '{host}:{port}'") from e

        SrvInit().execute(self)


    def close(self):
        self._conn.close()


    def send_message(self, msg_type: c_uint8, data: bytes | None = None):
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

            logger.debug("Sending message to server: seqnum={}, checksum={}, type={}, length={}".format(
                msg.seqnum,
                hex(msg.checksum),
                MsgTypes(msg.type).name,
                msg.length
            ))
            self._conn.send(buffer)
            if msg_type in (MsgTypes.MSG_ACK, MsgTypes.MSG_NACK):
                self._next_seqnum += 1
        except Exception as e:
            raise ConnectionError(f"Could not send message to server") from e


    def recv_message(self) -> tuple[c_uint8, bytes | None]:
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
            logger.debug("Received message from server: seqnum={}, checksum={}, type={}, length={}".format(
                msg.seqnum,
                hex(msg.checksum),
                MsgTypes(msg.type).name,
                msg.length
            ))

            # TODO: Check that checksum is correct first

            if msg.type in (MsgTypes.MSG_ACK, MsgTypes.MSG_NACK):
                if msg.seqnum == self._next_seqnum:
                    logger.debug("Received ACK / NACK with correct sequence number")
                    self._next_seqnum += 1
                else:
                    raise ConnectionError(
                        f"Received ACK / NACK with wrong sequence number, expected {self._next_seqnum}, got {msg.seqnum}"
                    )

            return msg.type, data
        except Exception as e:
            raise ConnectionError(f"Could not read message from server") from e


#
# Classes for the debugger commands
# ServerCommand itself implements the commands by sending and receiving the appropriate protocol messages. The derived classes
# set the message type, serialize the command's arguments and deserialize the result (provided as property), if any.
#
@dataclass
class ServerCommand:
    msg_type: c_uint8
    data: bytes | None = None
    error_code: int = -1
    target_info: target.TargetInfo | None = None

    def execute(self, server_conn: ServerConnection) -> 'ServerCommand':
        logger.debug(f"Sending message {MsgTypes(self.msg_type).name}")
        server_conn.send_message(self.msg_type, self.data)
        msg_type, data = server_conn.recv_message()
        if msg_type not in (MsgTypes.MSG_ACK, MsgTypes.MSG_NACK):
            raise ConnectionError(f"Received unexpected message of type {MsgTypes(msg_type).name} from server instead of the expected ACK / NACK")
        if msg_type == MsgTypes.MSG_ACK:
            self.error_code = 0
            self.data = data
        else:
            self.error_code = data[0]
            raise ServerCommandError(f"Server command failed with error {ErrorCodes(self.error_code).name} ({self.error_code})")

        # If we just sent a message that caused the target to stop / terminate, we need to wait for the MSG_TARGET_STOPPED message.
        if self.msg_type in (MsgTypes.MSG_RUN, MsgTypes.MSG_STEP, MsgTypes.MSG_CONT, MsgTypes.MSG_KILL):
            logger.info("Waiting for MSG_TARGET_STOPPED message from server...")
            msg_type, data = server_conn.recv_message()
            if msg_type == MsgTypes.MSG_TARGET_STOPPED:
                logger.debug("Received MSG_TARGET_STOPPED message from server, sending ACK")
                server_conn.send_message(MsgTypes.MSG_ACK)
                # TODO: Should we let the caller create a TargetInfo object from the data? This way we wouldn't have
                #       to import target.py here and thus prevent circular imports.
                self.target_info = target.TargetInfo.from_buffer(data)
                logger.info(f"Target has stopped, state = {self.target_info.target_state}")
            else:
                raise ConnectionError(f"Received unexpected message {MsgTypes(msg_type).name} from server, expected MSG_TARGET_STOPPED")
        return self


class SrvClearBreakpoint(ServerCommand):
    def __init__(self, bpoint_num: int):
        super().__init__(MsgTypes.MSG_CLEAR_BPOINT, data=struct.pack(M68K_UINT32, bpoint_num))


class SrvContinue(ServerCommand):
    def __init__(self):
        super().__init__(MsgTypes.MSG_CONT)


class SrvGetBaseAddress(ServerCommand):
    def __init__(self, library_name: str):
        super().__init__(MsgTypes.MSG_GET_BASE_ADDRESS, data=library_name.encode() + b'\x00')

    @property
    def result(self):
        return struct.unpack(M68K_UINT32, self.data[0:4])[0]


class SrvInit(ServerCommand):
    def __init__(self):
        super().__init__(MsgTypes.MSG_INIT)


class SrvKill(ServerCommand):
    def __init__(self):
        super().__init__(MsgTypes.MSG_KILL)


class SrvPeekMem(ServerCommand):
    def __init__(self, address: int, nbytes: int):
        super().__init__(MsgTypes.MSG_PEEK_MEM, data=struct.pack(M68K_UINT32, address) + struct.pack(M68K_UINT16, nbytes))

    @property
    def result(self):
        return self.data


class SrvQuit(ServerCommand):
    def __init__(self):
        super().__init__(MsgTypes.MSG_QUIT)


class SrvRun(ServerCommand):
    def __init__(self):
        super().__init__(MsgTypes.MSG_RUN)


class SrvSetBreakpoint(ServerCommand):
    def __init__(self, bpoint_offset: int, is_one_shot: bool = False):
        super().__init__(MsgTypes.MSG_SET_BPOINT, data=struct.pack(M68K_UINT32, bpoint_offset) + struct.pack(M68K_UINT16, is_one_shot))


class SrvSingleStep(ServerCommand):
    def __init__(self):
        super().__init__(MsgTypes.MSG_STEP)
