#!/usr/bin/env python3


import pytest
import sys
from loguru import logger

from hunklib import get_debug_infos_from_exe
from stabslib import ProgramWithDebugInfo


@pytest.fixture(scope='module')
def program():
    logger.remove()
    logger.add(
        sys.stderr,
        level='DEBUG',
        format='<level>{level: <8}</level> | '
                '<cyan>{file}</cyan>:<cyan>{function}</cyan>:<cyan>{line}</cyan> | '
                '<level>{message}</level>'
    )
    return ProgramWithDebugInfo.from_stabs_data(get_debug_infos_from_exe('../examples/numbers'))


def test_get_addr_range_for_lineno(program):
    assert program.get_addr_range_for_lineno(22) == (0x0000017c, 0x0000018c)


def test_get_lineno_for_addr(program):
    assert program.get_lineno_for_addr(0x0000017c) == 22


def test_get_source_fname_for_addr(program):
    assert program.get_source_fname_for_addr(0x0000017c) == '/home/consti/Programmieren/Amiga/CWDebug/examples/numbers.c'
