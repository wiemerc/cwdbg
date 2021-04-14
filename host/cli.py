#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the routines for the CLI on the host machine.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import argparse
import readline
import socket

from loguru import logger

from serio import ServerConnection, MsgTypes, TargetInfo


class ArgumentParserError(Exception):
    pass


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ArgumentParserError(message)


def process_cli_commands(conn: ServerConnection):
    parser = ThrowingArgumentParser(prog='', description="CWDebug, a source-level debugger for the AmigaOS", add_help=False)
    subparsers = parser.add_subparsers(dest='command', help="available commands")
    subparsers.add_parser('help', aliases=('h',), help="show help message")
    subparsers.add_parser('quit', aliases=('q',), help="quit the debugger", add_help=True)
    subparsers.add_parser('run', aliases=('r',), help="run the target")

    try:
        while True:
            cmdline = input('> ')
            logger.debug(f"command line: {cmdline}")
            try:
                args = parser.parse_args((cmdline,))
            except ArgumentParserError:
                print("invalid command / argument")
                parser.print_usage()
                continue

            # TODO: implement command loop similiar to process_cli_commands()
            if args.command in ('help', 'h'):
                parser.print_help()
                continue

            elif args.command in ('run', 'r'):
                conn.send_message(MsgTypes.MSG_RUN)
                msgtype, data = conn.recv_message()
                if msgtype == MsgTypes.MSG_TARGET_STOPPED:
                    tinfo = TargetInfo.from_buffer(data)
                    logger.info(f"target has stopped, state = {tinfo.ti_target_state}, exit code = {tinfo.ti_exit_code}")

            elif args.command in ('quit', 'q'):
                conn.send_message(MsgTypes.MSG_QUIT)
                msgtype, data = conn.recv_message()
                return
    except Exception as e:
        raise RuntimeError(f"error occurred while processing CLI commands: {e}") from e