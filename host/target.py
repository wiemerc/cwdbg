#
# target.py - part of cwdbg, a debugger for the AmigaOS
#             This file contains all target-related classes
#
# Copyright(C) 2018-2022 Constantin Wiemer


import struct
import sys
from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32
from dataclasses import dataclass
from enum import IntEnum

import capstone
from loguru import logger

# We can't use from server import ... because of the circular import target.py <-> server.py.
import server
from debugger import dbg
from errors import ErrorCodes


# keep in sync with values in target.h
NUM_NEXT_INSTRUCTIONS = 8
MAX_INSTR_BYTES       = 8
NUM_TOP_STACK_DWORDS  = 8

# format strings for pack / unpack
M68K_UINT16 = '>H'
M68K_INT16  = '>h'
M68K_UINT32 = '>I'


class Breakpoint(BigEndianStructure):
    _pack_ = 2
    _fields_ = (
        ('num', c_uint32),
        ('address', c_uint32),
        ('opcode', c_uint16),
        ('hit_count', c_uint32)
    )


@dataclass
class StackFrame:
    frame_ptr: int
    program_counter: int
    return_addr: int
    # TODO: Add function args and local vars once we process the corresponding stabs


@dataclass
class SyscallArg:
    decl: str
    register: int | None = None


@dataclass
class SyscallInfo:
    name: str
    args: list[SyscallArg]
    ret_type: str


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


    def next_instr_is_rts(self) -> bool:
        # check if next instruction is RTS
        if struct.unpack(M68K_UINT16, bytes(self.next_instr_bytes)[0:2])[0] == 0x4e75:
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


    def get_status_str(self) -> str:
        if self.target_state & TargetStates.TS_STOPPED_BY_BPOINT:
            return (
                f"Hit breakpoint #{self.bpoint.num} at entry + "
                f"{hex(self.bpoint.address - self.initial_pc)}, hit count = {self.bpoint.hit_count}"
            )
        if self.target_state & TargetStates.TS_STOPPED_BY_ONE_SHOT_BPOINT:
            return (
                f"Hit one-shot breakpoint #{self.bpoint.num} at entry + {hex(self.bpoint.address - self.initial_pc)}"
            )
        elif self.target_state & TargetStates.TS_STOPPED_BY_SINGLE_STEP:
            return "Stopped after single-stepping"
        elif self.target_state & TargetStates.TS_STOPPED_BY_EXCEPTION:
            return f"Stopped by exception #{self.task_context.exc_num}"
        elif self.target_state == TargetStates.TS_EXITED:
            return f"Exited with code {self.exit_code}"
        elif self.target_state == TargetStates.TS_KILLED:
            return "Killed"
        elif self.target_state == TargetStates.TS_ERROR:
            return f"Error {ErrorCodes(self.error_code).name} occured"
        else:
            raise AssertionError(f"Target has stopped with invalid state {self.target_state}")


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
        disasm = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32)
        instructions = []
        for idx, instr in enumerate(disasm.disasm(bytes(self.next_instr_bytes), self.task_context.reg_pc, NUM_NEXT_INSTRUCTIONS)):
            instr_addr = f'0x{instr.address:08x} (PC + {instr.address - self.task_context.reg_pc:04}):    '
            instr_repr = f'{instr.mnemonic:<10}{instr.op_str}\n'
            instructions.append(instr_addr + instr_repr)

            if (idx == 0) and (syscall_info := self._get_syscall_info()):
                instructions.append(f'{" " * len(instr_addr)}{syscall_info.name}(\n')
                for arg in syscall_info.args:
                    arg_int, arg_str = self._get_syscall_arg_values(syscall_info, arg)
                    arg_repr = f'{" " * (len(instr_addr) + 4)}{arg.decl} = {hex(arg_int)}'
                    if arg_str:
                        arg_repr += f' => "{arg_str}"'
                    instructions.append(arg_repr + ',\n')
                instructions.append(f'{" " * len(instr_addr)})\n')
        if instructions:
            return instructions
        else:
            return ['*** NOT AVAILABLE ***\n']



    def get_source_view(self) -> list[str]:
        if dbg.program is None:
            logger.debug("Program not loaded on host, source-level debugging not available")
            return ['*** NOT AVAILABLE ***\n']
        source_fname = dbg.program.get_comp_unit_for_addr(self.task_context.reg_pc - self.initial_pc)
        if source_fname is None:
            logger.warning("No source file available for current PC")
            return ['*** NOT AVAILABLE ***\n']
        # Ugly hack for the case that the program was built on Linux but the debugger runs on macOS...
        if sys.platform == 'darwin' and source_fname.startswith('/home'):
            source_fname = '/Users' + source_fname.removeprefix('/home')

        try:
            with open(source_fname) as f:
                source_lines = [f'{lineno + 1:<4}:    {line}' for lineno, line in enumerate(f.readlines())]
        except Exception as e:
            logger.warning(f"Could not read source file '{source_fname}': {e}")
            return ['*** NOT AVAILABLE ***\n']

        current_lineno = dbg.program.get_lineno_for_addr(self.task_context.reg_pc - self.initial_pc)
        if current_lineno is None:
            logger.warning("No line number available for current PC")
            return ['*** NOT AVAILABLE ***\n']

        if current_lineno > len(source_lines):
            raise AssertionError(
                f"Current line number in source file '{source_fname}' is {current_lineno} but file contains "
                f"only {len(source_lines)} lines"
            )

        # prepend current line with '=> '
        source_lines[current_lineno - 1] = source_lines[current_lineno - 1][0:6] + '=> ' + source_lines[current_lineno - 1][9:]

        start_lineno = current_lineno - 5
        if start_lineno < 1:
            start_lineno = 1
        end_lineno = current_lineno + 5
        if end_lineno > len(source_lines):
            end_lineno = len(source_lines)
        return [source_fname + ':\n'] + source_lines[start_lineno - 1: end_lineno -1]


    def get_call_stack(self) -> list[StackFrame]:
        stack_frames: list[StackFrame] = []
        frame_ptr = self.task_context.reg_a[5]
        program_counter = self.task_context.reg_pc
        while frame_ptr != 0xffffffff:
            # Previous frame pointer is stored at the address pointed to by the current frame pointer, return
            # address is at current frame pointer + 4. We get them both at once to save a roundtrip to the server.
            # A previous frame pointer 0xffffffff indicates that we've reached the initial frame.
            # TODO: How do we get out of this loop if a function doesn't use the frame pointer?
            try:
                cmd = server.SrvPeekMem(address=frame_ptr, nbytes=8).execute(dbg.server_conn)
            except server.ServerCommandError as e:
                raise RuntimeError(f"Getting return address / previous frame pointer failed") from e
            stack_frames.append(StackFrame(
                frame_ptr=frame_ptr,
                program_counter=program_counter,
                return_addr=struct.unpack(M68K_UINT32, cmd.result[4:])[0],
            ))
            frame_ptr = struct.unpack(M68K_UINT32, cmd.result[0:4])[0]
            program_counter = struct.unpack(M68K_UINT32, cmd.result[4:])[0]
        return stack_frames



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


    def _get_syscall_arg_values(self, syscall_info: SyscallInfo, arg: SyscallArg) -> tuple[int, str | None]:
        if arg.register >= 8:
            arg_int = self.task_context.reg_a[arg.register - 8]
        else:
            arg_int = self.task_context.reg_d[arg.register]

        arg_str = None
        if 'STRPTR' in arg.decl:
            # Argument is a pointer to a string. As we don't know the string length, we just get the memory block
            # with the maximum size at the address pointed to and search for a null byte in it.
            try:
                cmd = server.SrvPeekMem(address=arg_int, nbytes=server.MAX_MSG_DATA_LEN).execute(dbg.server_conn)
                if (str_len := cmd.result.find(b'\x00')) == -1:
                    str_len = server.MAX_MSG_DATA_LEN
                arg_str = cmd.result[0:str_len].decode(errors='replace').replace('\n', '\\n').replace('\r', '\\r')
            except server.ServerCommandError as e:
                raise RuntimeError(f"Getting string at address {hex(arg_int)} for arg {arg} of syscall {syscall_info} failed") from e
        return arg_int, arg_str
