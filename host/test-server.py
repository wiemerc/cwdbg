#
# test-server.py - part of cwdbg, a debugger for the AmigaOS
#                  This file contains tests for the server based on protocol messages.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import pytest

from errors import ErrorCodes
from server import (
    SrvClearBreakpoint,
    SrvContinue,
    SrvGetBaseAddress,
    SrvKill,
    SrvPeekMem,
    SrvQuit,
    SrvRun,
    SrvSetBreakpoint,
    SrvSingleStep,
    ServerCommandError,
    ServerConnection,
)
from target import TargetStates


@pytest.fixture(scope='module')
def server_conn():
    conn = ServerConnection('localhost', 1234)
    yield conn
    conn.close()


def test_get_base_address(server_conn: ServerConnection):
    # Addresses are valid for AmigaOS 3.1.
    cmd = SrvGetBaseAddress(library_name="exec.library").execute(server_conn)
    assert cmd.result == 0x078007f8
    cmd = SrvGetBaseAddress(library_name="dos.library").execute(server_conn)
    assert cmd.result == 0x0780f8c4


def test_peek_mem(server_conn: ServerConnection):
    cmd = SrvPeekMem(address=4, nbytes=4).execute(server_conn)
    # Address 0x00000004 should contain base address of exec.library, see above
    assert cmd.result == b'\x07\x80\x07\xf8'


def test_set_bpoint(server_conn: ServerConnection):
    SrvSetBreakpoint(bpoint_offset=0x24).execute(server_conn)


def test_clear_bpoint(server_conn: ServerConnection):
    SrvClearBreakpoint(bpoint_num=1).execute(server_conn)


def test_clear_bpoint_wrong_number(server_conn: ServerConnection):
    with pytest.raises(ServerCommandError):
        cmd = SrvClearBreakpoint(bpoint_num=2).execute(server_conn)
        assert cmd.error_code == ErrorCodes.ERROR_UNKNOWN_BREAKPOINT.value


def test_run_to_bpoint(server_conn: ServerConnection):
    SrvSetBreakpoint(bpoint_offset=0x24).execute(server_conn)
    cmd = SrvRun().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_RUNNING | TargetStates.TS_STOPPED_BY_BPOINT


def test_single_step(server_conn: ServerConnection):
    cmd = SrvSingleStep().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_RUNNING | TargetStates.TS_SINGLE_STEPPING | TargetStates.TS_STOPPED_BY_SINGLE_STEP


def test_continue_from_bpoint(server_conn: ServerConnection):
    SrvClearBreakpoint(bpoint_num=2).execute(server_conn)
    cmd = SrvContinue().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_EXITED
    assert cmd.target_info.exit_code == 0


def test_kill(server_conn: ServerConnection):
    SrvSetBreakpoint(bpoint_offset=0x24).execute(server_conn)
    cmd = SrvRun().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_RUNNING | TargetStates.TS_STOPPED_BY_BPOINT
    cmd = SrvKill().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_KILLED
    SrvClearBreakpoint(bpoint_num=3).execute(server_conn)


def test_one_shot_bpoint(server_conn: ServerConnection):
    SrvSetBreakpoint(bpoint_offset=0x24, is_one_shot=True).execute(server_conn)
    cmd = SrvRun().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_RUNNING | TargetStates.TS_STOPPED_BY_ONE_SHOT_BPOINT
    cmd = SrvContinue().execute(server_conn)
    assert cmd.target_info.target_state == TargetStates.TS_EXITED
    assert cmd.target_info.exit_code == 0


def test_quit(server_conn: ServerConnection):
    SrvQuit().execute(server_conn)
