#
# debugger.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the debugger functionality on the host side.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32
from enum import IntEnum


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
        ('task_context', TaskContext),
        ('target_state', c_uint32),
        ('exit_code', c_uint32)
    )


class TargetStates(IntEnum):
    TS_IDLE                   = 0
    TS_RUNNING                = 1
    TS_SINGLE_STEPPING        = 2
    TS_EXITED                 = 4
    TS_STOPPED_BY_BREAKPOINT  = 8
    TS_STOPPED_BY_SINGLE_STEP = 16
    TS_STOPPED_BY_EXCEPTION   = 32
