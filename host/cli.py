#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the routines for the CLI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import re
import readline
import shlex
import struct

from loguru import logger
from typing import Optional, Tuple

from debugger import ErrorCodes, TargetInfo, TargetStates
from serio import ServerConnection, MsgTypes
from stabslib import ProgramWithDebugInfo


FMT_UINT32 = '>I'


class QuitDebuggerException(Exception):
    pass


class ArgumentParserError(Exception):
    pass


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ArgumentParserError(message)


def process_cli_command(
    server_conn: ServerConnection,
    program: ProgramWithDebugInfo,
    cmd_line: str
) -> Tuple[Optional[str], Optional[TargetInfo]]:
    parser = ThrowingArgumentParser(prog='', description="CWDebug, a source-level debugger for the AmigaOS", add_help=False)
    # TODO: Rewrite with a table of commands, their argument(s) and handler functions
    # TODO: Catch -h / --help in sub-parsers, probably by adding a custom action, see https://stackoverflow.com/questions/58367375/can-i-prevent-argparse-from-exiting-if-the-user-specifies-h
    # TODO: Align commands with GDB
    # TODO: Implement connect / disconnect commands
    # TODO: Implement command to inspect / disassemble memory (like 'x' in GDB)
    # TODO: Implement 'backtrace' command
    # TODO: Implement 'next' command
    # TODO: Repeat last command with 'enter'
    subparsers = parser.add_subparsers(dest='command', help="Available commands")
    subparsers.add_parser('break', aliases=('b',), help="Set breakpoint").add_argument(
        'location',
        help="Location of the breakpoint, meaning depends on format: "
             "hex number = offset relative to entry point, "
             "decimal number = line number, "
             "string = function name",
    )
    subparsers.add_parser('continue', aliases=('c', 'cont'), help="Continue target")
    subparsers.add_parser('delete', aliases=('d', 'del'), help="Delete breakpoint").add_argument(
        'number',
        type=int,
        help="Breakpoint number",
    )
    subparsers.add_parser('help', aliases=('h',), help="Show help message")
    subparsers.add_parser('kill', aliases=('k',), help="Kill target")
    subparsers.add_parser('quit', aliases=('q',), help="Quit debugger", add_help=True)
    subparsers.add_parser('run', aliases=('r',), help="Run target")
    subparsers.add_parser('step', aliases=('s',), help="Single-step target")

    try:
        try:
            args = parser.parse_args(shlex.split(cmd_line))
        except ArgumentParserError:
            return "Invalid command / argument\n" + parser.format_help(), None

        command_ok, error = _is_correct_target_state_for_command(server_conn.target_state, args.command)
        if not command_ok:
            return error, None

        if args.command in ('break', 'b'):
            if re.search(r'^0x[0-9a-fA-F]+$', args.location):
                offset = int(args.location, 16)
            elif re.search('^\d+$', args.location):
                offset = program.get_addr_for_lineno(int(args.location, 10))
            elif re.search(r'^[a-zA-Z_]\w+$', args.location):
                offset = program.get_addr_for_func_name(args.location)
            else:
                # TODO: Implement <file name>:<line number> as location
                return "Invalid format of breakpoint location", None
            result = server_conn.execute_command(MsgTypes.MSG_SET_BP, struct.pack(FMT_UINT32, offset))
            if result.error_code == 0:
                return "Breakpoint set", None
            else:
                return f"Setting breakpoint failed: {ErrorCodes(result.result.error_code).name}", None

        elif args.command in ('delete', 'del', 'd'):
            result = server_conn.execute_command(MsgTypes.MSG_CLEAR_BP, struct.pack(FMT_UINT32, args.number))
            if result.error_code == 0:
                return "Breakpoint cleared", None
            else:
                return f"Clearing breakpoint failed: {ErrorCodes(result.error_code).name}", None

        elif args.command in ('continue', 'cont', 'c'):
            result = server_conn.execute_command(MsgTypes.MSG_CONT)
            return _get_target_status_for_ui(result.target_info)

        elif args.command in ('help', 'h'):
            return parser.format_help(), None

        elif args.command in ('kill', 'k'):
            result =server_conn.execute_command(MsgTypes.MSG_KILL)
            return _get_target_status_for_ui(result.target_info)

        elif args.command in ('quit', 'q'):
            result = server_conn.execute_command(MsgTypes.MSG_QUIT)
            raise QuitDebuggerException()

        elif args.command in ('run', 'r'):
            result = server_conn.execute_command(MsgTypes.MSG_RUN)
            return _get_target_status_for_ui(result.target_info)

        elif args.command in ('step', 's'):
            # TODO: Implement single-stepping on C level (next line instead of next instruction)
            result = server_conn.execute_command(MsgTypes.MSG_STEP)
            return _get_target_status_for_ui(result.target_info)

    except QuitDebuggerException:
        raise
    except Exception as e:
        raise RuntimeError(f"Error occurred while processing CLI commands") from e


def _is_correct_target_state_for_command(target_state: int, command: str) -> Tuple[bool, Optional[str]]:
    # keep lists of commands in sync with process_cli_command()
    if not (target_state & TargetStates.TS_RUNNING) and command[0] in ('c', 's', 'k'):
        return False, f"Incorrect state for command '{command}': target is not yet running"
    if (target_state & TargetStates.TS_RUNNING) and command[0] in ('r', 'q'):
        return False, f"Incorrect state for command '{command}': target is already / still running"
    return True, None


def _get_target_status_for_ui(target_info: TargetInfo) -> Tuple[Optional[str], Optional[TargetInfo]]:
    if target_info.target_state & TargetStates.TS_STOPPED_BY_BREAKPOINT:
        return f"Target has hit breakpoint at address {hex(target_info.task_context.reg_pc)}", target_info
    elif target_info.target_state & TargetStates.TS_STOPPED_BY_EXCEPTION:
        return f"Target has been stopped by exception #{target_info.task_context.exc_num}", target_info
    elif target_info.target_state == TargetStates.TS_EXITED:
        return f"Target exited with code {target_info.exit_code}", None
    elif target_info.target_state == TargetStates.TS_KILLED:
        return f"Target has been killed", None
    else:
        return None, target_info
