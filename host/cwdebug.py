#!/usr/bin/env python3
#
# cwdebug.py - part of CWDebug, a source-level debugger for the AmigaOS
#              This is the debugger host that contains all the logic for source-level
#              debugging and talks to the remote server.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import capstone
import sys

from loguru import logger

from cli import Cli, QuitDebuggerException
from debugger import Debugger, dbg
from hunklib import get_debug_infos_from_exe
from server import ServerConnection
from stabslib import ProgramWithDebugInfo
from ui import MainScreen


RETURN_OK    = 0
RETURN_ERROR = 1


def main():
    args = _parse_command_line()
    _setup_logging(args.verbose)
    _init_debugger(args)

    try:
        if args.no_tui:
            _print_banner()
            while True:
                cmd_line = input('> ')
                try:
                    result = dbg.cli.process_command(cmd_line)
                    if result[0]:
                        print(result[0])
                except QuitDebuggerException:
                    logger.debug("Exiting debugger...")
                    break
        else:
            main_screen = MainScreen(args.verbose)
    except Exception as e:
        logger.exception(f"Internal error occurred: {e}")
    finally:
        if dbg.server_conn:
            dbg.server_conn.close()


def _parse_command_line() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="CWDebug, a source-level debugger for the AmigaOS",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument('--prog', help="Program you want to debug (with debug information)")
    parser.add_argument('--verbose', '-v', action="store_true", default=False, help="Enable verbose logging")
    parser.add_argument('--host', '-H', help="IP address / name of debugger server")
    parser.add_argument('--port', '-P', type=int, help="Port of debugger server")
    parser.add_argument('--no-tui', action='store_true', default=False, help="Disable TUI (mainly for debugging the debugger itself)")
    args = parser.parse_args()
    return args


def _setup_logging(verbose: bool):
    logger.remove()
    logger.add(
        sys.stderr,
        level='DEBUG' if verbose else 'INFO',
        format='<level>{level: <8}</level> | '
               '<cyan>{file}</cyan>:<cyan>{function}</cyan>:<cyan>{line}</cyan> | '
               '<level>{message}</level>'
    )


def _print_banner():
    print("""
   _______       ______       __               
  / ____/ |     / / __ \___  / /_  __  ______ _
 / /    | | /| / / / / / _ \/ __ \/ / / / __ `/
/ /___  | |/ |/ / /_/ /  __/ /_/ / /_/ / /_/ / 
\____/  |__/|__/_____/\___/_.___/\__,_/\__, /  
                                      /____/   

    """)


def _init_debugger(args: argparse.Namespace):
        dbg.cli = Cli()
        if args.prog:
            dbg.program = ProgramWithDebugInfo.from_stabs_data(get_debug_infos_from_exe(args.prog))
        if args.host and args.port:
            dbg.server_conn = ServerConnection(args.host, args.port) 
        dbg.disasm = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_32)
        # TODO: Load db with syscall infos


if __name__ == '__main__':
    main()
