#
# debugger.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the debugger functionality on the host side.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional


# keep in sync with values in debugger.h
NUM_NEXT_INSTRUCTIONS = 8
MAX_INSTR_BYTES       = 8
NUM_TOP_STACK_DWORDS  = 8


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
        ('top_stack_dwords', c_uint32 * NUM_TOP_STACK_DWORDS)
    )


class TargetStates(IntEnum):
    TS_IDLE                   = 0
    TS_RUNNING                = 1
    TS_SINGLE_STEPPING        = 2
    TS_EXITED                 = 4
    TS_KILLED                 = 8
    TS_STOPPED_BY_BREAKPOINT  = 16
    TS_STOPPED_BY_SINGLE_STEP = 32
    TS_STOPPED_BY_EXCEPTION   = 64
    TS_ERROR                  = 65536


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


@dataclass
class DebuggerState:
    cli: Optional['Cli'] = None
    program: Optional['ProgramWithDebugInfo'] = None
    server_conn: Optional['ServerConnection'] = None
    target_state: int = TargetStates.TS_IDLE

dbg_state = DebuggerState()
