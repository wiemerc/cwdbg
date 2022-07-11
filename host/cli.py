#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the classes for the CLI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import re
import readline
import shlex
from abc import abstractmethod
from dataclasses import dataclass

from loguru import logger

from debugger import ErrorCodes, TargetInfo, TargetStates, dbg_state
from server import (
    ServerCommandError,
    SrvClearBreakpoint,
    SrvContinue,
    SrvKill,
    SrvQuit,
    SrvRun,
    SrvSetBreakpoint,
    SrvSingleStep
)


class QuitDebuggerException(RuntimeError):
    pass


class ArgumentParserError(RuntimeError):
    pass


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ArgumentParserError(message)


@dataclass
class CliCommandArg:
    name: str
    help: str
    type: type = str


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
        if target_info.target_state & TargetStates.TS_STOPPED_BY_BREAKPOINT:
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


class CliClearBreakpoint(CliCommand):
    def __init__(self):
        super().__init__(
            'delete',
            ('d', 'del'),
            'Delete breakpoint',
            (
                CliCommandArg(
                    'number',
                    'Breakpoint number',
                    int,
                ),
            ),
        )

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            SrvClearBreakpoint(args.number).execute(dbg_state.server_conn)
            return "Breakpoint cleared", None
        except ServerCommandError as e:
            return f"Clearing breakpoint failed: {e}", None


class CliContinue(CliCommand):
    def __init__(self):
        super().__init__('continue', ('c', 'cont'), 'Continue target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvContinue().execute(dbg_state.server_conn)
            dbg_state.target_state = cmd.target_info.target_state
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Continuing target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not (dbg_state.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'continue': target is not yet running"
        else:
            return True, None


class CliKill(CliCommand):
    def __init__(self):
        super().__init__('kill', ('k',), 'Kill target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvKill().execute(dbg_state.server_conn)
            dbg_state.target_state = cmd.target_info.target_state
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Killing target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not (dbg_state.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'kill': target is not yet running"
        else:
            return True, None


class CliQuit(CliCommand):
    def __init__(self):
        super().__init__('quit', ('q', ), 'Quit debugger')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        SrvQuit().execute(dbg_state.server_conn)
        raise QuitDebuggerException()

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if dbg_state.target_state & TargetStates.TS_RUNNING:
            return False, "Incorrect state for command 'quit': target is still running"
        else:
            return True, None


class CliRun(CliCommand):
    def __init__(self):
        super().__init__('run', ('r', ), 'Run target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvRun().execute(dbg_state.server_conn)
            dbg_state.target_state = cmd.target_info.target_state
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Running target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if dbg_state.target_state & TargetStates.TS_RUNNING:
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
        elif re.search('^\d+$', args.location):
            if dbg_state.program:
                offset = dbg_state.program.get_addr_for_lineno(int(args.location, 10))
            else:
                return "Program not loaded on host, source-level debugging not available", None
        elif re.search(r'^[a-zA-Z_]\w+$', args.location):
            if dbg_state.program:
                offset = dbg_state.program.get_addr_for_func_name(args.location)
            else:
                return "Program not loaded on host, source-level debugging not available", None
        else:
            # TODO: Implement <file name>:<line number> as location
            return "Invalid format of breakpoint location", None
        try:
            SrvSetBreakpoint(offset).execute(dbg_state.server_conn)
            return "Breakpoint set", None
        except ServerCommandError as e:
            return f"Setting breakpoint failed: {e}", None


class CliSingleStep(CliCommand):
    def __init__(self):
        super().__init__('step', ('s',), 'Single-step target')

    def execute(self, args: argparse.Namespace) -> tuple[str | None, TargetInfo | None]:
        try:
            cmd = SrvSingleStep().execute(dbg_state.server_conn)
            dbg_state.target_state = cmd.target_info.target_state
            return self._get_target_status_for_ui(cmd.target_info)
        except ServerCommandError as e:
            return f"Single-stepping target failed: {e}", None

    def is_correct_target_state_for_command(self) -> tuple[bool, str | None]:
        if not (dbg_state.target_state & TargetStates.TS_RUNNING):
            return False, "Incorrect state for command 'step': target is not yet running"
        else:
            return True, None


# TODO: Align commands with GDB
# TODO: Implement connect / disconnect commands
# TODO: Implement command to inspect / disassemble memory (like 'x' in GDB)
# TODO: Implement 'backtrace' command
# TODO: Implement 'next' command
CLI_COMMANDS = [
    CliClearBreakpoint(),
    CliContinue(),
    CliKill(),
    CliQuit(),
    CliRun(),
    CliSetBreakpoint(),
    CliSingleStep(),
]


class Cli:
    def __init__(self):
        self._commands_by_name: dict[str, CliCommand] = {}
        self._parser = ThrowingArgumentParser(prog='', description="CWDebug, a source-level debugger for the AmigaOS", add_help=False)
        # TODO: Catch -h / --help in sub-parsers, probably by adding a custom action, see https://stackoverflow.com/questions/58367375/can-i-prevent-argparse-from-exiting-if-the-user-specifies-h
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
                subparser.add_argument(arg.name, help=arg.help, type=arg.type)


    def process_command(self, cmd_line: str) -> tuple[str | None, TargetInfo | None]:
        try:
            try:
                if cmd_line:
                    args = self._stored_args = self._parser.parse_args(shlex.split(cmd_line))
                else:
                    args = self._stored_args
            except ArgumentParserError:
                return "Invalid command / argument\n" + self._parser.format_help(), None

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
