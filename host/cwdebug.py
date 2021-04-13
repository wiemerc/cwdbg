#!/usr/bin/env python3
#
# cwdebug.py - part of CWDebug, a source-level debugger for the AmigaOS
#              This is the debugger host that contains all the logic for source-level
#              debugging and talks to the remote server.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import argparse
import socket
import sys

from loguru import logger

from cli import process_cli_commands
from serio import ServerConnection
#from hunklib import ...
#from stabslib import read_stabs_info, build_program_tree, ...


RETURN_OK    = 0
RETURN_ERROR = 1


def main() -> None:
    args = _parse_command_line()
    level = 'DEBUG' if args.verbose else 'INFO'
    _setup_logging(level)
    _print_banner()

    conn = None
    try:
        conn = ServerConnection(args.host, args.port)
        process_cli_commands(conn)
    except Exception as e:
        logger.exception(f"internal error occurred: {e}")
    finally:
        if conn:
            conn.close()


def _parse_command_line() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CWDebug, a source-level debugger for the AmigaOS")
    parser.add_argument("--verbose", "-v", help="enable verbose logging", action="store_true")
    parser.add_argument("--host", "-H", default="127.0.0.1", help="IP address / name of debugger server")
    parser.add_argument("--port", "-P", type=int, default=1234, help="port of debugger server")
    args = parser.parse_args()
    return args


def _setup_logging(level: str) -> None:
    logger.remove()
    logger.add(
        sys.stderr,
        level=level,
        format='<level>{level: <8}</level> | '
               '<cyan>{file}</cyan>:<cyan>{function}</cyan>:<cyan>{line}</cyan> | '
               '<level>{message}</level>'
    )


def _print_banner() -> None:
    print("""
   _______       ______       __               
  / ____/ |     / / __ \___  / /_  __  ______ _
 / /    | | /| / / / / / _ \/ __ \/ / / / __ `/
/ /___  | |/ |/ / /_/ /  __/ /_/ / /_/ / /_/ / 
\____/  |__/|__/_____/\___/_.___/\__,_/\__, /  
                                      /____/   

    """)


if __name__ == "__main__":
    main()
