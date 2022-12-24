#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the classes for the CLI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import functools
import re
import readline
import shlex
import struct
from abc import abstractmethod
from dataclasses import dataclass

import capstone
from loguru import logger

from debugger import dbg
from errors import ErrorCodes
from server import (
    ServerCommandError,
    SrvClearBreakpoint,
    SrvContinue,
    SrvKill,
    SrvPeekMem,
    SrvQuit,
    SrvRun,
    SrvSetBreakpoint,
    SrvSingleStep
)
from target import MAX_INSTR_BYTES, TargetInfo, TargetStates


class QuitDebuggerException(RuntimeError):
    pass


class ArgumentParserError(RuntimeError):
    pass


class NoDebugInfoError(RuntimeError):
    pass


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ArgumentParserError(message)


@dataclass
class CliCommandArg:
    name: str
    help: str
    type: type = str
    choices: list = None


#
# Classes for the debugger commands
# Each class derived from CliCommand encapsulates the information necessary for the argument parser (name, aliases,
# help and arguments) and implements the command using the classes derived from ServerCommand in server.py.
#
@dataclass
class CliCommand:
    command: str
    aliases: tuple[str]
    help: str
    arg_spec: tuple[CliCommandArg] = tuple()

    @abstractmethod
    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        pass

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        return True, None

    def _get_target_status_for_ui(self, target_info: TargetInfo) -> tuple[str | None, TargetInfo | None]:
        if target_info.target_state & TargetStates.TS_STOPPED_BY_BPOINT:
            return (
                f"Target has hit breakpoint #{target_info.bpoint.num} at entry + "
                f"{hex(target_info.bpoint.address - target_info.initial_pc)}, hit count = {target_info.bpoint.hit_count}",
                target_info
            )
        elif target_info.target_state & TargetStates.TS_STOPPED_BY_EXCEPTION:
            return f"Target has been stopped by exception #{target_info.task_context.exc_num}", target_info
        elif target_info.target_state == TargetStates.TS_EXITED:
            return f"Target exited with code {target_info.exit_code}", None
        elif target_info.target_state == TargetStates.TS_KILLED:
            return f"Target has been killed", None
        elif target_info.target_state == TargetStates.TS_ERROR:
            return f"Error {ErrorCodes(target_info.error_code).name} occured while running target", None
        else:
            return None, target_info


class CliBacktrace(CliCommand):
    def __init__(self):
        super().__init__('backtrace', ('bt', ), 'Print a backtrace or call stack')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        call_stack_repr = ""
        for idx, frame in enumerate(dbg.target_info.get_call_stack()):
            if frame.program_counter >= dbg.target_info.initial_pc:
                addr_offset = frame.program_counter - dbg.target_info.initial_pc
            else:
                addr_offset = -1
            if (comp_unit := dbg.program.get_comp_unit_for_addr(addr_offset)) is not None:
                if (lineno := dbg.program.get_lineno_for_addr(addr_offset, comp_unit=comp_unit)) is None:
                    lineno = '???'
            else:
                comp_unit = '???'
                lineno = '???'
            call_stack_repr += f"Frame #{idx}: 0x{frame.program_counter:08x} {comp_unit}:{lineno}\n"
        return call_stack_repr, None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'backtrace': target is not yet running"
        else:
            return True, None


class CliClearBreakpoint(CliCommand):
    def __init__(self):
        super().__init__(
            'delete',
            ('d', 'del'),
            'Delete breakpoint',
            (
                CliCommandArg(
                    name='number',
                    help='Breakpoint number',
                    type=int,
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            SrvClearBreakpoint(args.number).execute(dbg.server_conn)
            return "Breakpoint cleared", None
        except ServerCommandError as e:
            return f"Clearing breakpoint failed: {e}", None


class CliContinue(CliCommand):
    def __init__(self):
        super().__init__('continue', ('c', 'cont'), 'Continue target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvContinue().execute(dbg.server_conn)
            dbg.target_info = cmd.target_info
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Continuing target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'continue': target is not yet running"
        else:
            return True, None


class CliDisassemble(CliCommand):
    def __init__(self):
        super().__init__(
            'disassemble',
            ('di', 'dis'),
            'Create disassembly of a block of memory',
            (
                CliCommandArg(
                    name='address',
                    help='Address of memory block',
                    type=functools.partial(int, base=0),
                ),
                CliCommandArg(
                    name='ninstr',
                    help='Number of instructions (currently limited to <= 32)',
                    type=functools.partial(int, base=10),
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvPeekMem(address=args.address, nbytes=args.ninstr * MAX_INSTR_BYTES).execute(dbg.server_conn)
        except ServerCommandError as e:
            return f"Reading memory failed: {e}", None

        disasm = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32)
        listing = ''
        for instr in disasm.disasm(cmd.result, args.address, args.ninstr):
            listing += f"0x{instr.address:08x}:  {instr.mnemonic:<10}{instr.op_str}\n"
        return listing, None


class CliExamine(CliCommand):
    def __init__(self):
        super().__init__(
            'examine',
            ('x', ),
            'Examine memory',
            (
                CliCommandArg(
                    name='format',
                    help='Format string as understood by the unpack() function in the Python "struct" module',
                ),
                # TODO: Accept register name (prefixed with $) in addition to address, also in CliDisassemble and CliHexdump
                CliCommandArg(
                    name='address',
                    help='Memory address',
                    type=functools.partial(int, base=0),
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvPeekMem(address=args.address, nbytes=struct.calcsize(args.format)).execute(dbg.server_conn)
            return '\n'.join([
                hex(val) if isinstance(val, int) else repr(val)
                for val in struct.unpack(args.format, cmd.result)
            ]), None
        except struct.error as e:
            return f"Parsing format string failed: {e}", None
        except ServerCommandError as e:
            return f"Reading memory failed: {e}", None


class CliHexdump(CliCommand):
    def __init__(self):
        super().__init__(
            'hexdump',
            ('hx', ),
            'Create hexdump of a block of memory',
            (
                CliCommandArg(
                    name='address',
                    help='Address of memory block',
                    type=functools.partial(int, base=0),
                ),
                CliCommandArg(
                    name='size',
                    help='Size of memory block (currently limited to <= 255)',
                    type=functools.partial(int, base=0),
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvPeekMem(address=args.address, nbytes=args.size).execute(dbg.server_conn)
        except ServerCommandError as e:
            return f"Reading memory failed: {e}", None

        dump = f"Hex dump of {args.size} bytes at address {hex(args.address)}:\n"
        pos = 0
        while (pos < len(cmd.result)):
            dump += f"0x{args.address + pos:08x}:  {cmd.result[pos:pos + 16].hex(sep=' '):<47}  "
            dump += ''.join([chr(x) if x >= 0x20 and x <= 0x7e else '.' for x in cmd.result[pos:pos + 16]]) + '\n'
            pos += 16
        return dump, None


class CliInspect(CliCommand):
    def __init__(self):
        super().__init__(
            'inspect',
            ('i', ),
            'Inspect target',
            (
                CliCommandArg(
                    name='what',
                    help='Type of object to inspect: d(issambly), r(egisters), s(tack) or source (c)ode',
                    choices=('d', 'r', 's', 'c'),
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        if args.what == 'd':
            return ''.join(dbg.target_info.get_disasm_view()), None
        elif args.what == 'r':
            return ''.join(dbg.target_info.get_register_view()), None
        elif args.what == 's':
            return ''.join(dbg.target_info.get_stack_view()), None
        elif args.what == 'c':
            return ''.join(dbg.target_info.get_source_view()), None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'inspect': target is not yet running"
        else:
            return True, None


class CliKill(CliCommand):
    def __init__(self):
        super().__init__('kill', ('k',), 'Kill target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvKill().execute(dbg.server_conn)
            dbg.target_info = cmd.target_info
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Killing target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'kill': target is not yet running"
        else:
            return True, None


class CliNextInstr(CliCommand):
    def __init__(self):
        super().__init__('nexti', ('ni',), 'Execute target until next instruction (step over sub-routines)')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            # Check if next instruction is a JSR. If yes, set one-shot breakpoint at the instruction following the JSR
            # and continue. If no, just single-step.
            # TODO: Should we stop if we reach the end of the program (here and when single-stepping)?
            if dbg.target_info.next_instr_is_jsr():
                offset = (
                    dbg.target_info.task_context.reg_pc
                    - dbg.target_info.initial_pc
                    + dbg.target_info.get_bytes_used_by_jsr()
                )
                SrvSetBreakpoint(bpoint_offset=offset, is_one_shot=True).execute(dbg.server_conn)
                cmd = SrvContinue().execute(dbg.server_conn)
                dbg.target_info = cmd.target_info
                return self._get_target_status_for_ui(cmd.target_info)
            else:
                cmd = SrvSingleStep().execute(dbg.server_conn)
                dbg.target_info = cmd.target_info
                return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Executing target until next instruction failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'nexti': target is not yet running"
        else:
            return True, None


class CliNextLine(CliCommand):
    def __init__(self):
        super().__init__('next', ('n',), 'Execute target until next line (step over function calls)')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            self._execute_until_next_line()
            return self._get_target_status_for_ui(dbg.target_info)
        except ServerCommandError as e:
            return f"Executing target until next line failed: {e}", None
        except NoDebugInfoError as e:
            return f"Can't execute target until next line: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'next': target is not yet running"
        else:
            return True, None

    def _execute_until_next_line(self):
        current_comp_unit = dbg.program.get_comp_unit_for_addr(
            dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc
        )
        if current_comp_unit is None:
            raise NoDebugInfoError("No compilation unit available for current PC")
        current_lineno = dbg.program.get_lineno_for_addr(
            dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc,
            comp_unit=current_comp_unit
        )
        if current_lineno is None:
            raise NoDebugInfoError("No line number available for current PC")
        addr_range_of_current_line = dbg.program.get_addr_range_for_lineno(current_lineno, comp_unit=current_comp_unit)
        if addr_range_of_current_line is None:
            raise AssertionError(
                f"Found line #{current_lineno} for PC={dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc} "
                f"but looking up address range for that line failed"
            )

        disasm = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32)
        while (
            addr_range_of_current_line[0] <=
            (dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc) <
            addr_range_of_current_line[1]
        ):
            # execute all instructions that are part of the current line
            instr = next(disasm.disasm(bytes(dbg.target_info.next_instr_bytes), dbg.target_info.task_context.reg_pc, 1))
            logger.debug(f"Next instruction: 0x{instr.address:08x} :    {instr.mnemonic:<10}{instr.op_str}")
            if dbg.target_info.next_instr_is_jsr():
                # function call => set breakpoint on the return address (the instruction following the JSR) and continue
                # TODO: For some reason GCC sometimes generates a BSR instead of a JSR instruction. Can we catch these cases as well?
                SrvSetBreakpoint(
                    bpoint_offset=dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc + instr.size,
                    is_one_shot=True,
                ).execute(dbg.server_conn)
                cmd = SrvContinue().execute(dbg.server_conn)
                dbg.target_info = cmd.target_info
            elif dbg.target_info.next_instr_is_rts():
                # return from the current function => execute the RTS and then the rest of the line
                #                                     containing the function call in the parent
                # We can't rely on the stack pointer / stack frames to determine that we're in parent function
                # because not all functions use them.
                cmd = SrvSingleStep().execute(dbg.server_conn)
                dbg.target_info = cmd.target_info
                return self._execute_until_next_line()
            else:
                cmd = SrvSingleStep().execute(dbg.server_conn)
                dbg.target_info = cmd.target_info


class CliQuit(CliCommand):
    def __init__(self):
        super().__init__('quit', ('q', ), 'Quit debugger')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        SrvQuit().execute(dbg.server_conn)
        raise QuitDebuggerException()

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if dbg.target_info and dbg.target_info.target_state & TargetStates.TS_RUNNING:
            return False, "Incorrect state for command 'quit': target is still running"
        else:
            return True, None


class CliRun(CliCommand):
    def __init__(self):
        super().__init__('run', ('r', ), 'Run target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvRun().execute(dbg.server_conn)
            dbg.target_info = cmd.target_info
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Running target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if dbg.target_info and dbg.target_info.target_state & TargetStates.TS_RUNNING:
            return False, "Incorrect state for command 'run': target is already running"
        else:
            return True, None


class CliSetBreakpoint(CliCommand):
    def __init__(self):
        super().__init__(
            'break',
            ('b', ),
            'Set breakpoint',
            (
                CliCommandArg(
                    'location',
                    'Location of the breakpoint, meaning depends on format: '
                    'hex number = offset relative to entry point, '
                    'decimal number = line number, '
                    'string = function name',
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        if re.search(r'^0x[0-9a-fA-F]+$', args.location):
            offset = int(args.location, 16)
        else:
            try:
                if dbg.program is None:
                    return "Program not loaded on host, source-level debugging not available", None
                if re.search('^\d+$', args.location):
                    addr_range = dbg.program.get_addr_range_for_lineno(int(args.location, 10))
                elif re.search(r'^[a-zA-Z_]\w+$', args.location):
                    addr_range = dbg.program.get_addr_range_for_func_name(args.location)
                else:
                    # TODO: Implement <file name>:<line number> as location
                    return "Invalid format of breakpoint location", None
            except ValueError as e:
                return f"Failed to find address for breakpoint location {args.location}: {e}", None
            if addr_range is not None:
                offset, _ = addr_range
            else:
                return f"No address available for breakpoint location {args.location}", None

        try:
            SrvSetBreakpoint(offset).execute(dbg.server_conn)
            return "Breakpoint set", None
        except ServerCommandError as e:
            return f"Setting breakpoint failed: {e}", None


class CliStepInstr(CliCommand):
    def __init__(self):
        super().__init__('stepi', ('si',), 'Step one instruction')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvSingleStep().execute(dbg.server_conn)
            dbg.target_info = cmd.target_info
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Stepping one instruction failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'stepi': target is not yet running"
        else:
            return True, None


class CliStepLine(CliCommand):
    def __init__(self):
        super().__init__('step', ('s',), 'Step one line')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            self._execute_one_line()
            return self._get_target_status_for_ui(dbg.target_info)
        except ServerCommandError as e:
            return f"Stepping one line failed: {e}", None
        except NoDebugInfoError as e:
            return f"Can't step one line: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not dbg.target_info or not (dbg.target_info.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'step': target is not yet running"
        else:
            return True, None

    def _execute_one_line(self):
        # Single-step instructions until we're outside of the range of the current line
        current_comp_unit = dbg.program.get_comp_unit_for_addr(
            dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc
        )
        if current_comp_unit is None:
            raise NoDebugInfoError("No compilation unit available for current PC")
        current_lineno = dbg.program.get_lineno_for_addr(
            dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc,
            comp_unit=current_comp_unit
        )
        if current_lineno is None:
            raise NoDebugInfoError("No line number available for current PC")
        addr_range_of_current_line = dbg.program.get_addr_range_for_lineno(current_lineno, comp_unit=current_comp_unit)
        if addr_range_of_current_line is None:
            raise AssertionError(
                f"Found line #{current_lineno} for PC={dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc} "
                f"but looking up address range for that line failed"
            )

        call_stack = dbg.target_info.get_call_stack()
        curr_frame_ptr = call_stack[0].frame_ptr
        prev_frame_ptr = call_stack[1].frame_ptr if len(call_stack) > 1 else None
        while addr_range_of_current_line[0] <= (dbg.target_info.task_context.reg_pc - dbg.target_info.initial_pc) < addr_range_of_current_line[1]:
            cmd = SrvSingleStep().execute(dbg.server_conn)
            dbg.target_info = cmd.target_info
        if dbg.target_info.task_context.reg_a[5] == prev_frame_ptr:
            # returned from callee to caller => continue stepping until the end of the line containing the function call
            self._execute_one_line()


# TODO: Align commands with GDB
CLI_COMMANDS = [
    CliBacktrace(),
    CliClearBreakpoint(),
    CliContinue(),
    CliDisassemble(),
    CliExamine(),
    CliInspect(),
    CliHexdump(),
    CliKill(),
    CliNextInstr(),
    CliNextLine(),
    CliQuit(),
    CliRun(),
    CliSetBreakpoint(),
    CliStepInstr(),
    CliStepLine(),
]


class Cli:
    def __init__(self):
        self._commands_by_name: dict[str, CliCommand] = {}
        self._parser = ThrowingArgumentParser(prog='', description="CWDebug, a source-level debugger for the AmigaOS", add_help=False)
        # TODO: Catch -h / --help in sub-parsers, probably by adding a custom action, see https://stackoverflow.com/questions/58367375/can-i-prevent-argparse-from-exiting-if-the-user-specifies-h
        # TODO: If command exists but arguments are wrong / missing, show only help for that command
        subparsers = self._parser.add_subparsers(dest='command', help="Available commands")
        subparsers.add_parser('help', aliases=('h',), help="Show help message")
        for cmd in CLI_COMMANDS:
            if cmd.command not in self._commands_by_name:
                self._commands_by_name[cmd.command] = cmd
            else:
                raise ValueError(f"Command name '{cmd.command}' already used by command '{self._commands_by_name[cmd.command]}'")
            for alias in cmd.aliases:
                if alias not in self._commands_by_name:
                    self._commands_by_name[alias] = cmd
                else:
                    raise ValueError(f"Command alias '{alias}' already used by command '{self._commands_by_name[alias]}'")
            subparser = subparsers.add_parser(cmd.command, aliases=cmd.aliases, help=cmd.help)
            for arg in cmd.arg_spec:
                subparser.add_argument(arg.name, help=arg.help, type=arg.type, choices=arg.choices)


    # TODO: Pass server connection explicitly instead of accessing it via the global debugger object to break the circular import
    def process_command(self, cmd_line: str) -> tuple[str | None, TargetInfo | None]:
        try:
            try:
                if cmd_line:
                    args = self._stored_args = self._parser.parse_args(shlex.split(cmd_line))
                else:
                    args = self._stored_args
            except ArgumentParserError:
                return "Invalid command / argument\n" + self._parser.format_help(), None
            except AttributeError:
                return "Can't repeat command, no previous command available", None

            if args.command in ('help', 'h'):
                return self._parser.format_help(), None
            else:
                cmd = self._commands_by_name[args.command]
                command_ok, error = cmd.is_correct_target_state_for_command()
                if not command_ok:
                    return error, None
                return cmd.execute(args)

        except QuitDebuggerException:
            raise
        except Exception as e:
            raise RuntimeError(f"Error occurred while processing CLI commands") from e
