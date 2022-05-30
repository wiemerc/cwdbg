#!/usr/bin/env python3


import sys
from loguru import logger

from hunklib import get_debug_infos_from_exe
from stabslib import ProgramWithDebugInfo


logger.remove()
logger.add(
    sys.stderr,
    level='DEBUG',
    format='<level>{level: <8}</level> | '
            '<cyan>{file}</cyan>:<cyan>{function}</cyan>:<cyan>{line}</cyan> | '
            '<level>{message}</level>'
)
program = ProgramWithDebugInfo.from_stabs_data(get_debug_infos_from_exe(sys.argv[1]))
