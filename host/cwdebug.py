#!/usr/bin/env python3
#
# cwdebug.py - part of CWDebug, a source-level debugger for the AmigaOS
#              This is the debugger host that contains all the logic for source-level
#              debugging and talks to the remote server.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import glob
import os
import pickle
import sys
from loguru import logger

from cli import Cli, QuitDebuggerException
from debugger import dbg
from errors import ErrorCodes
from hunklib import get_debug_infos_from_exe
from server import ServerCommandError, ServerConnection, SrvGetBaseAddress
from stabslib import ProgramWithDebugInfo
from ui import MainScreen


RETURN_OK    = 0
RETURN_ERROR = 1


def main():
    args = _parse_command_line()
    _setup_logging(args.verbose)

    try:
        _init_debugger(args)
        if args.no_tui:
            _print_banner()
            while True:
                cmd_line = input('> ')
                try:
                    result_str, target_info = dbg.cli.process_command(cmd_line)
                    if result_str:
                        print(result_str)
                except QuitDebuggerException:
                    logger.debug("Exiting debugger...")
                    break
        else:
            MainScreen(args.verbose)
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
    parser.add_argument('--host', '-H', default='127.0.0.1', help="IP address / name of debugger server")
    parser.add_argument('--port', '-P', type=int, default=1234, help="Port of debugger server")
    parser.add_argument('--no-tui', action='store_true', default=False, help="Disable TUI (mainly for debugging the debugger itself)")
    parser.add_argument('--syscall-db-dir', default='../syscall-db', help="Directory containing the system call database files")
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
    if args.prog:
        dbg.program = ProgramWithDebugInfo.from_stabs_data(get_debug_infos_from_exe(args.prog))
    dbg.server_conn = ServerConnection(args.host, args.port) 
    dbg.cli = Cli()
    dbg.syscall_db = _load_syscall_db(args.syscall_db_dir)
    dbg.lib_base_addresses = _get_lib_base_addresses(args.syscall_db_dir)


def _load_syscall_db(syscall_db_dir: str):
    logger.info("Loading system call database")
    syscall_db = {}
    for fname in glob.glob(os.path.join(syscall_db_dir, '*.data')):
        with open(fname, 'rb') as f:
            syscall_db[os.path.splitext(os.path.basename(fname))[0]] = pickle.load(f)
    return syscall_db


def _get_lib_base_addresses(syscall_db_dir: str):
    logger.info("Getting library base addresses from server")
    lib_base_addresses = {}
    file_names = glob.glob(os.path.join(syscall_db_dir, '*.data'))
    lib_names = [os.path.splitext(os.path.basename(fname))[0] for fname in file_names]
    for lname in lib_names:
        try:
            cmd = SrvGetBaseAddress(library_name=lname + '.library').execute(dbg.server_conn)
            logger.debug(f"Library '{lname}.library' has address {hex(cmd.result)}")
            lib_base_addresses[cmd.result] = lname
        except ServerCommandError as e:
            raise RuntimeError(f"Getting library base addresses failed") from e
    return lib_base_addresses


if __name__ == '__main__':
    main()
