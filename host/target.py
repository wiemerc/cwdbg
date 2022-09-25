#
# target.py - part of CWDebug, a source-level debugger for the AmigaOS
#             This file contains all target-related classes
#
# Copyright(C) 2018-2022 Constantin Wiemer


import struct

from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32
from dataclasses import dataclass
from enum import IntEnum

import capstone

from loguru import logger

from debugger import dbg


# keep in sync with values in target.h
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


class TargetRegisters(IntEnum):
    D0 = 0
    D1 = 1
    D2 = 2
    D3 = 3
    D4 = 4
    D5 = 5
    D6 = 6
    D7 = 7
    A0 = 8
    A1 = 9
    A2 = 10
    A3 = 11
    A4 = 12
    A5 = 13
    A6 = 14


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


@dataclass
class SyscallArg:
    decl: str
    register: int = None


@dataclass
class SyscallInfo:
    name: str
    args: list[SyscallArg]
    ret_type: str


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
        jsr_instr = next(capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32).disasm(
            bytes(self.next_instr_bytes),
            self.task_context.reg_pc,
            NUM_NEXT_INSTRUCTIONS,
        ))
        return jsr_instr.size

    def get_register_view(self) -> list[str]:
        regs = []
        for i in range(7):
            regs.append(f'A{i}=0x{self.task_context.reg_a[i]:08x}        D{i}=0x{self.task_context.reg_d[i]:08x}\n')
        regs.append(f'A7=0x{self.task_context.reg_sp:08x}        D7=0x{self.task_context.reg_d[7]:08x}\n')
        return regs

    def get_stack_view(self) -> list[str]:
        stack_dwords = []
        for i in range(NUM_TOP_STACK_DWORDS):
            stack_dwords.append(f'SP + {i * 4:02}:    0x{self.top_stack_dwords[i]:08x}\n')
        return stack_dwords

    def get_disasm_view(self) -> list[str]:
        instructions = []
        # TODO: Annotate first instruction if it's a syscall
        self._get_syscall_info()
        for instr in capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32).disasm(
            bytes(self.next_instr_bytes),
            self.task_context.reg_pc,
            NUM_NEXT_INSTRUCTIONS,
        ):
            instructions.append(f'0x{instr.address:08x} (PC + {instr.address - self.task_context.reg_pc:02}):    {instr.mnemonic:<10}{instr.op_str}\n')
        return instructions

    def _get_syscall_info(self) -> SyscallInfo | None:
        if self._next_instr_is_syscall():
            lib_base_addr = self.task_context.reg_a[6]
            if lib_base_addr in dbg.lib_base_addresses:
                lib_name = dbg.lib_base_addresses[lib_base_addr]
                syscall_offset = self._get_syscall_offset()
                if syscall_offset in dbg.syscall_db[lib_name]:
                    syscall_info = dbg.syscall_db[lib_name][syscall_offset]
                    logger.debug(f"Next instruction is syscall {syscall_info} in {lib_name}.library")
                    return syscall_info
                else:
                    logger.warning(
                        f"Register A6 contains base address of {lib_name}.library but syscall with offset {syscall_offset} "
                        f"was not found in syscall db"
                    )
                    return None
            else:
                logger.warning(f"Next instruction seems to be a syscall but base address {hex(lib_base_addr)} is unknown")
                return None
        else:
            return None

    def _next_instr_is_syscall(self) -> bool:
        # check if next instruction is JSR with an effective address of register A6 + 16-bit offset
        if (struct.unpack(M68K_UINT16, bytes(self.next_instr_bytes)[0:2])[0] & 0xffff) == 0x4eae:
            return True
        else:
            return False

    def _get_syscall_offset(self) -> int:
        # This only works if the next instruction is indeed a system call. We return the unsigned value because that's
        # how they appear in the pragmas and therefore in the syscall database.
        return abs(struct.unpack(M68K_INT16, bytes(self.next_instr_bytes)[2:4])[0])
