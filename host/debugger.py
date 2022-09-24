#
# debugger.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the debugger functionality on the host side.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import capstone
import glob
import os
import pickle

from dataclasses import dataclass
from typing import Optional

from loguru import logger

from errors import ErrorCodes
from server import ServerConnection, SrvGetBaseAddress
from stabslib import ProgramWithDebugInfo
from target import SyscallInfo, TargetInfo


@dataclass
class Debugger:
    program: ProgramWithDebugInfo | None = None
    server_conn: ServerConnection | None  = None
    # TODO: We can reference Cli only by name because cli.py also imports us -> would cause circular import. But
    #       maybe we don't need to store the CLI object here?
    cli: Optional['Cli'] = None
    syscall_db: dict[str, dict[int, SyscallInfo]] | None = None
    lib_base_addresses: dict[int, str] | None = None
    target_info: TargetInfo | None = None

    def load_syscall_db(self, syscall_db_dir: str):
        logger.info("Loading system call database")
        self.syscall_db = {}
        for fname in glob.glob(os.path.join(syscall_db_dir, '*.data')):
            with open(fname, 'rb') as f:
                self.syscall_db[os.path.splitext(os.path.basename(fname))[0]] = pickle.load(f)

    def get_lib_base_addresses(self, syscall_db_dir: str):
        logger.info("Getting library base addresses from server")
        self.lib_base_addresses = {}
        file_names = glob.glob(os.path.join(syscall_db_dir, '*.data'))
        lib_names = [os.path.splitext(os.path.basename(fname))[0] for fname in file_names]
        for lname in lib_names:
            cmd = SrvGetBaseAddress(library_name=lname + '.library').execute(self.server_conn)
            if cmd.error_code == ErrorCodes.ERROR_OK:
                logger.debug(f"Library '{lname}.library' has address {hex(cmd.result)}")
                self.lib_base_addresses[cmd.result] = lname
            else:
                raise RuntimeError(f"Getting library base addresses failed with error code {ErrorCodes(cmd.error_code).name}")


dbg = Debugger()
