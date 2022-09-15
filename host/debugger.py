#
# debugger.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the debugger functionality on the host side.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import capstone
import struct

from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional


# keep in sync with values in debugger.h
NUM_NEXT_INSTRUCTIONS = 8
MAX_INSTR_BYTES       = 8
NUM_TOP_STACK_DWORDS  = 8

# format strings for pack / unpack
M68K_UINT16 = '>H'
M68K_INT16  = '>h'


class TaskContext(BigEndianStructure):
    _pack_ = 2
    _fields_ = (
        ('reg_sp', c_uint32),
        ('exc_num', c_uint32),
        ('reg_sr', c_uint16),
        ('reg_pc', c_uint32),
        ('reg_d', c_uint32 * 8),
        ('reg_a', c_uint32 * 7)
    )


class Breakpoint(BigEndianStructure):
    _pack_ = 2
    _fields_ = (
        ('num', c_uint32),
        ('address', c_uint32),
        ('opcode', c_uint16),
        ('hit_count', c_uint32)
    )


class TargetInfo(BigEndianStructure):
    _pack_ = 2
    _fields_ = (
        ('initial_pc', c_uint32),
        ('initial_sp', c_uint32),
        ('task_context', TaskContext),
        ('target_state', c_uint32),
        ('exit_code', c_uint32),
        ('error_code', c_uint32),
        ('next_instr_bytes', c_uint8 * NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES),
        ('top_stack_dwords', c_uint32 * NUM_TOP_STACK_DWORDS),
        ('bpoint', Breakpoint)
    )

    def next_instr_is_jsr(self) -> bool:
        # check if next instruction is JSR, see Musashi's opcode info table in m68kdasm.c and Motorola's
        # M68000 Family Programmer’s Reference Manual for details
        if (struct.unpack(M68K_UINT16, bytes(self.next_instr_bytes)[0:2])[0] & 0xffc0) == 0x4e80:
            return True
        else:
            return False

    def get_bytes_used_by_jsr(self) -> int:
        # This only works if the next instruction is indeed a JSR. We use the disassembler here to get the size of the
        # JSR instruction so we don't have to decode the different address modes ourselves.
        # TODO: Move disassembler to DebuggerState
        disasm = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32)
        jsr_instr = next(disasm.disasm(bytes(self.next_instr_bytes), self.task_context.reg_pc, NUM_NEXT_INSTRUCTIONS))
        return jsr_instr.size

    def next_instr_is_system_call(self) -> bool:
        # check if next instruction is JSR with an effective address of register A6 + 16-bit offset
        if (struct.unpack(M68K_UINT16, bytes(self.next_instr_bytes)[0:2])[0] & 0xffff) == 0x4eae:
            return True
        else:
            return False

    def get_system_call_offset(self) -> int:
        # This only works if the next instruction is indeed a system call.
        return struct.unpack(M68K_INT16, bytes(self.next_instr_bytes)[2:4])[0]


class TargetStates(IntEnum):
    TS_IDLE                        = 0
    TS_RUNNING                     = 1
    TS_SINGLE_STEPPING             = 2
    TS_EXITED                      = 4
    TS_KILLED                      = 8
    TS_STOPPED_BY_BPOINT           = 16
    TS_STOPPED_BY_ONE_SHOT_BPOINT  = 32
    TS_STOPPED_BY_SINGLE_STEP      = 64
    TS_STOPPED_BY_EXCEPTION        = 128
    TS_ERROR                       = 65536


# keep in sync with values in debugger.h
class ErrorCodes(IntEnum):
    ERROR_OK                     = 0
    ERROR_NOT_ENOUGH_MEMORY      = 1
    ERROR_INVALID_ADDRESS        = 2
    ERROR_UNKNOWN_BREAKPOINT     = 3
    ERROR_LOAD_TARGET_FAILED     = 4
    ERROR_CREATE_PROC_FAILED     = 5
    ERROR_UNKNOWN_STOP_REASON    = 6
    ERROR_NO_TRAP                = 7
    ERROR_RUN_COMMAND_FAILED     = 8
    ERROR_BAD_DATA               = 9


@dataclass
class DebuggerState:
    cli: Optional['Cli'] = None
    program: Optional['ProgramWithDebugInfo'] = None
    server_conn: Optional['ServerConnection'] = None
    target_info: TargetInfo | None = None


dbg_state = DebuggerState()
