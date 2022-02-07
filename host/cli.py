#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the routines for the CLI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import readline

from loguru import logger
from typing import Optional, Tuple

from debugger import TargetInfo, TargetStates
from serio import ServerConnection, MsgTypes


class QuitDebuggerException(Exception):
    pass


class ArgumentParserError(Exception):
    pass


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ArgumentParserError(message)


def process_cli_command(conn: ServerConnection, command: str) -> Tuple[Optional[str], Optional[TargetInfo]]:
    parser = ThrowingArgumentParser(prog='', description="CWDebug, a source-level debugger for the AmigaOS", add_help=False)
    subparsers = parser.add_subparsers(dest='command', help="Available commands")
    subparsers.add_parser('help', aliases=('h',), help="Show help message")
    subparsers.add_parser('quit', aliases=('q',), help="Quit the debugger", add_help=True)
    subparsers.add_parser('run', aliases=('r',), help="Run the target")

    try:
        try:
            args = parser.parse_args((command,))
        except ArgumentParserError:
            return "Invalid command / argument\n" + parser.format_help(), None

        # TODO: Implement logic similiar to process_cli_commands() in cli.c
        # TODO: Implement connect / disconnect commands
        if args.command in ('help', 'h'):
            return parser.format_help(), None

        elif args.command in ('run', 'r'):
            target_info = conn.execute_command(MsgTypes.MSG_RUN)
            if target_info.target_state & TargetStates.TS_EXITED:
                return f"Target exited with code {target_info.exit_code}", None
            else:
                return None, target_info

        elif args.command in ('quit', 'q'):
            conn.execute_command(MsgTypes.MSG_QUIT)
            raise QuitDebuggerException()
    except QuitDebuggerException:
        raise
    except Exception as e:
        raise RuntimeError(f"Error occurred while processing CLI commands") from e