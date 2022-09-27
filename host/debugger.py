#
# debugger.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the debugger functionality on the host side.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import glob
import os
import pickle
from dataclasses import dataclass
from typing import Optional


# This is the global debugger object used to pass around all other objects that are needed by the other modules. It
# will be populated in cwdebug.py. We can't do this here because this would require us to import several things from
# other modules, and thus would cause circular imports. It's basically a form of dependency injection.
@dataclass
class Debugger:
    program: Optional['ProgramWithDebugInfo'] = None
    server_conn: Optional['ServerConnection'] = None
    cli: Optional['Cli'] = None
    syscall_db: dict[str, dict[int, 'SyscallInfo']] | None = None
    lib_base_addresses: dict[int, str] | None = None
    target_info: Optional['TargetInfo'] = None


dbg =  Debugger()
