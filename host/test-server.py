#
# test-server.py - part of CWDebug, a source-level debugger for the AmigaOS
#                  This file contains tests for the server based on protocol messages.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import pytest

from debugger import ErrorCodes, TargetStates
from server import (
    SrvClearBreakpoint,
    SrvContinue,
    SrvKill,
    SrvQuit,
    SrvRun,
    SrvSetBreakpoint,
    SrvSingleStep,
    ServerCommandError,
    ServerConnection,
)


@pytest.fixture(scope='module')
def server_conn():
    conn = ServerConnection('localhost', 1234)
    yield conn
    conn.close()


def test_connect(server_conn: ServerConnection):
    pass


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
    assert cmd.target_info.target_state == TargetStates.TS_RUNNING | TargetStates.TS_STOPPED_BY_BREAKPOINT


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
    SrvRun().execute(server_conn)
    cmd = SrvKill().execute(server_conn)
    assert cmd.target_info is not None
    assert cmd.target_info.target_state == TargetStates.TS_KILLED


def test_disconnect(server_conn: ServerConnection):
    pass


def test_quit(server_conn: ServerConnection):
    SrvQuit().execute(server_conn)
