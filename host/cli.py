#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the routines for the CLI on the host machine.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import argparse
import readline

from loguru import logger

from serio import ServerConnection, MsgTypes, TargetInfo


class ArgumentParserError(Exception):
    pass


class ThrowingArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ArgumentParserError(message)


def process_cli_commands(conn: ServerConnection):
    parser = ThrowingArgumentParser(prog='', description="CWDebug, a source-level debugger for the AmigaOS", add_help=False)
    subparsers = parser.add_subparsers(dest='command', help="Available commands")
    subparsers.add_parser('help', aliases=('h',), help="Show help message")
    subparsers.add_parser('quit', aliases=('q',), help="Quit the debugger", add_help=True)
    subparsers.add_parser('run', aliases=('r',), help="Run the target")

    try:
        target_running = False
        while True:
            if target_running:
                logger.info("Target is running, waiting for it to stop...")
                msg, data = conn.recv_message()
                if msg.type == MsgTypes.MSG_TARGET_STOPPED:
                    conn.send_message(MsgTypes.MSG_ACK)
                    target_running = False
                    target_info = TargetInfo.from_buffer(data)
                    logger.info(f"Target has stopped, state = {target_info.target_state}, exit code = {target_info.exit_code}")
                else:
                    raise ConnectionError(f"Received unexpected message {MsgTypes(msg.type).name} from server, expected MSG_TARGET_STOPPED")

            cmdline = input('> ')
            logger.debug(f"Command line: {cmdline}")
            try:
                args = parser.parse_args((cmdline,))
            except ArgumentParserError:
                print("Invalid command / argument")
                parser.print_usage()
                continue

            # TODO: implement command loop similiar to process_cli_commands()
            if args.command in ('help', 'h'):
                parser.print_help()
                continue

            elif args.command in ('run', 'r'):
                conn.send_command(MsgTypes.MSG_RUN)
                target_running = True

            elif args.command in ('quit', 'q'):
                conn.send_command(MsgTypes.MSG_QUIT)
                return
    except Exception as e:
        raise RuntimeError(f"Error occurred while processing CLI commands") from e