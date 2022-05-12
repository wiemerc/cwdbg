#
# test-server.py - part of CWDebug, a source-level debugger for the AmigaOS
#                  This file contains tests for the server based on protocol messages.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import pytest

from debugger import ErrorCodes, TargetStates
from serio import MsgTypes, ServerConnection


@pytest.fixture(scope='module')
def server_conn():
    conn = ServerConnection('localhost', 1234)
    yield conn
    conn.close()


def test_connect(server_conn: ServerConnection):
    pass


def test_set_bpoint(server_conn: ServerConnection):
    result = server_conn.execute_command(MsgTypes.MSG_SET_BP, b'\x00\x00\x00\x24')
    assert result.error_code == 0


def test_clear_bpoint(server_conn: ServerConnection):
    result = server_conn.execute_command(MsgTypes.MSG_CLEAR_BP, b'\x00\x00\x00\x01')
    assert result.error_code == 0


def test_clear_bpoint_wrong_number(server_conn: ServerConnection):
    result = server_conn.execute_command(MsgTypes.MSG_CLEAR_BP, b'\x00\x00\x00\x02')
    assert result.error_code == ErrorCodes.ERROR_UNKNOWN_BREAKPOINT.value


def test_run_to_bpoint(server_conn: ServerConnection):
    server_conn.execute_command(MsgTypes.MSG_SET_BP, b'\x00\x00\x00\x24')
    result = server_conn.execute_command(MsgTypes.MSG_RUN)
    assert result.target_info is not None
    assert result.target_info.target_state == TargetStates.TS_RUNNING | TargetStates.TS_STOPPED_BY_BREAKPOINT


def test_continue_from_bpoint(server_conn: ServerConnection):
    server_conn.execute_command(MsgTypes.MSG_CLEAR_BP, b'\x00\x00\x00\x02')
    result = server_conn.execute_command(MsgTypes.MSG_CONT)
    assert result.target_info is not None
    assert result.target_info.target_state == TargetStates.TS_EXITED
    assert result.target_info.exit_code == 0


def test_disconnect(server_conn: ServerConnection):
    pass


def test_quit(server_conn: ServerConnection):
    result = server_conn.execute_command(MsgTypes.MSG_QUIT)
    assert result.error_code == 0
