#
# cli.py - part of CWDebug, a source-level debugger for the AmigaOS
#          This file contains the routines for the CLI on the host machine.
#
# Copyright(C) 2018-2021 Constantin Wiemer


import argparse
import readline
import socket

from loguru import logger

from serio import ServerConnection


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
    while True:
        cmdline = input('> ')
        logger.debug(f"command line: {cmdline}")
        try:
            args = parser.parse_args((cmdline,))
        except ArgumentParserError:
            print("invalid command / argument")
            parser.print_usage()
            continue
        if args.command in ('help', 'h'):
            parser.print_help()
            continue
        if args.command in ('quit', 'q'):
            return