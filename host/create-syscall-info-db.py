#!/usr/bin/env python3
#
# create-syscall-info-db.py - part of CWDebug, a source-level debugger for the AmigaOS
#                             This scripts creates a database with syscall information from the prototypes and pragmas.
#
# Copyright(C) 2018-2022 Constantin Wiemer


import pickle
import os
import re
import sys

from argparse import ArgumentParser

from debugger import SyscallArg, SyscallInfo, TargetRegisters

def main():
    parser = ArgumentParser(description='Create a database with syscall information from the prototypes and pragmas')
    parser.add_argument(
        '--include-dir',
        default='/opt/m68k-amigaos/m68k-amigaos/ndk/include',
        help='Directory containing the include files with the prototypes and the pragmas',
    )
    parser.add_argument('--lib-name', required=True, help='Name of the library to create the database for, e. g. exec')
    parser.add_argument('--db-file', required=True, help='Name of the file to write the database (in pickle format) to')
    cli_args = parser.parse_args()

    syscall_infos_by_name: dict[str, SyscallInfo] = {}
    syscall_infos_by_offset: dict[int, SyscallInfo] = {}

    with open(os.path.join(cli_args.include_dir, f'clib/{cli_args.lib_name}_protos.h'), errors='replace') as proto_file:
        for line in proto_file:
            line = line.strip()
            if line.endswith(';'):
                if (arg_list_start := line.find('(')) == -1 or (arg_list_end := line.rfind(')')) == -1:
                    print(f"Prototype not in the expected format: {line}", file=sys.stderr)
                    continue

                if match := re.search(r'^(?P<ret_type>.+)\b(?P<name>\w+)\b$', line[0:arg_list_start]):
                    arg_list = line[arg_list_start + 1:arg_list_end]
                    args = [arg.strip() for arg in arg_list.split(',')]
                    args = [SyscallArg(decl=arg) for arg in args if arg != 'VOID' and arg != '...']

                    sci = SyscallInfo(
                        name=match.group('name'),
                        args=args,
                        ret_type=match.group('ret_type')
                    )
                    syscall_infos_by_name[match.group('name')] = sci
                else:
                    print(f"Prototype not in the expected format: {line}", file=sys.stderr)
                    continue

    with open(os.path.join(cli_args.include_dir, f'pragmas/{cli_args.lib_name}_pragmas.h'), errors='replace') as pragma_file:
        for line in pragma_file:
            line = line.strip()
            if line.startswith('#pragma'):
                name, offset, reg_info = line.split()[-3:]
                offset = int(offset, 16)
                if name in syscall_infos_by_name:
                    sci = syscall_infos_by_name[name]

                    # The reg_info strings consists of
                    # - the registers for the arguments, in reverse order, as hexadecimal numbers with D0 = 0 and A0 = 8
                    # - the register for the return value (usually D0)
                    # - the number of arguments
                    reg_info = [int(x, 16) for x in list(reg_info)]
                    nargs = reg_info.pop()
                    ret_val_reg = reg_info.pop()

                    # TODO: Functions Printf, FPrintf and FWritef in dos.library seem to have a register assigned to the
                    #       first variadic arg, so we get the error below for them.
                    if nargs != len(sci.args):
                        print(
                            f"Number of args in pragma ({nargs}) isn't equal to number of args in prototype "
                            f"({len(sci.args)}) for syscall {name}",
                            file=sys.stderr
                        )
                        continue

                    for arg in sci.args:
                        arg.register = TargetRegisters(reg_info.pop())

                    syscall_infos_by_offset[offset] = sci
#                    print(sci)
                else:
                    print(f"Syscall '{name}' not found in list of prototypes", file=sys.stderr)
                    continue

    with open(cli_args.db_file, 'wb') as db_file:
        pickle.dump(syscall_infos_by_offset, db_file)


if __name__ == '__main__':
    main()
