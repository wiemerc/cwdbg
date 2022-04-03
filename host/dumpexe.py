#!/usr/bin/env python3


import sys
from loguru import logger

from hunklib import read_exe, BlockTypes
from stabslib import ProgramWithDebugInfo


logger.remove()
logger.add(
    sys.stderr,
    level='DEBUG',
    format='<level>{level: <8}</level> | '
            '<cyan>{file}</cyan>:<cyan>{function}</cyan>:<cyan>{line}</cyan> | '
            '<level>{message}</level>'
)
content_by_type = read_exe(sys.argv[1])
program = ProgramWithDebugInfo.from_stabs_data(content_by_type[BlockTypes.HUNK_DEBUG])
