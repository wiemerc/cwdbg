#!/usr/bin/env python3
#
# cwdebug.py - part of CWDebug, a source-level debugger for the AmigaOS
#              This is the debugger host that contains all the logic for source-level
#              debugging and talks to the remote server.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import argparse
import sys

from loguru import logger

from cli import process_cli_command, QuitDebuggerException
from serio import ServerConnection
from ui import MainScreen
#from hunklib import ...
#from stabslib import read_stabs_info, build_program_tree, ...


RETURN_OK    = 0
RETURN_ERROR = 1


def main():
    args = _parse_command_line()
    _setup_logging(args.verbose)

    conn = None
    try:
        conn = ServerConnection(args.host, args.port)
        if args.no_tui:
            _print_banner()
            while True:
                command = input('> ')
                try:
                    result, target_info = process_cli_command(conn, command)
                    if result:
                        print(result)
                except QuitDebuggerException:
                    logger.debug("Exiting debugger...")
                    break
        else:
            main_screen = MainScreen(args.verbose, conn)
    except Exception as e:
        logger.exception(f"Internal error occurred: {e}")
    finally:
        if conn:
            conn.close()


def _parse_command_line() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CWDebug, a source-level debugger for the AmigaOS")
    parser.add_argument('--verbose', '-v', help="Enable verbose logging", action="store_true")
    parser.add_argument('--host', '-H', default='127.0.0.1', help="IP address / name of debugger server (default=127.0.0.1)")
    parser.add_argument('--port', '-P', type=int, default=1234, help="Port of debugger server(default=1234)")
    parser.add_argument('--no-tui', action='store_true', default=False, help="Disable TUI (mainly for debugging)")
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


if __name__ == '__main__':
    main()
