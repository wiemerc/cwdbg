#
# stabslib.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the classes / routines to read and process the debug information
#               in STABS format and provide this information to the debugger.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from enum import IntEnum
from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32, sizeof
from loguru import logger
from typing import List


# stab types / names from binutils-gdb/include/aout/stab.def
class StabTypes(IntEnum):
    N_UNDF    = 0x00
    N_EXT     = 0x01
    N_ABS     = 0x02
    N_TEXT    = 0x04
    N_DATA    = 0x06
    N_BSS     = 0x08
    N_INDR    = 0x0a
    N_FN_SEQ  = 0x0c
    N_WEAKU   = 0x0d
    N_WEAKA   = 0x0e
    N_WEAKT   = 0x0f
    N_WEAKD   = 0x10
    N_WEAKB   = 0x11
    N_COMM    = 0x12
    N_SETA    = 0x14
    N_SETT    = 0x16
    N_SETD    = 0x18
    N_SETB    = 0x1a
    N_SETV    = 0x1c
    N_WARNING = 0x1e
    N_FN      = 0x1f
    N_GSYM    = 0x20
    N_FNAME   = 0x22
    N_FUN     = 0x24
    N_STSYM   = 0x26
    N_LCSYM   = 0x28
    N_MAIN    = 0x2a
    N_ROSYM   = 0x2c
    N_BNSYM   = 0x2e
    N_PC      = 0x30
    N_NSYMS   = 0x32
    N_NOMAP   = 0x34
    N_OBJ     = 0x38
    N_OPT     = 0x3c
    N_RSYM    = 0x40
    N_M2C     = 0x42
    N_SLINE   = 0x44
    N_DSLINE  = 0x46
    N_BSLINE  = 0x48
    N_DEFD    = 0x4a
    N_FLINE   = 0x4C
    N_ENSYM   = 0x4E
    N_EHDECL  = 0x50
    N_CATCH   = 0x54
    N_SSYM    = 0x60
    N_ENDM    = 0x62
    N_SO      = 0x64
    N_OSO     = 0x66
    N_ALIAS   = 0x6c
    N_LSYM    = 0x80
    N_BINCL   = 0x82
    N_SOL     = 0x84
    N_PSYM    = 0xa0
    N_EINCL   = 0xa2
    N_ENTRY   = 0xa4
    N_LBRAC   = 0xc0
    N_EXCL    = 0xc2
    N_SCOPE   = 0xc4
    N_PATCH   = 0xd0
    N_RBRAC   = 0xe0
    N_BCOMM   = 0xe2
    N_ECOMM   = 0xe4
    N_ECOML   = 0xe8
    N_WITH    = 0xea
    N_NBTEXT  = 0xF0
    N_NBDATA  = 0xF2
    N_NBBSS   = 0xF4
    N_NBSTS   = 0xF6
    N_NBLCS   = 0xF8
    N_LENG    = 0xfe


class Stab(BigEndianStructure):
    _fields_ = [
        ('offset', c_uint32),
        ('type', c_uint8),
        ('other', c_uint8),
        ('desc', c_uint16),
        ('value', c_uint32),
    ]


class ProgramNode:
    def __init__(self, type: int, name: str, typeid: str = '', start_addr: int = 0, end_addr: int = 0, lineno: int = 0):
        self.type       = type
        self.name       = name
        self.typeid     = typeid
        self.start_addr = start_addr
        self.end_addr   = end_addr
        self.lineno     = lineno
        self.children   = []

    def __str__(self) -> str:
        # TODO: look up type id in data dictionary => typeid_to_type()
        return f"ProgramNode(type={StabTypes(self.type).name}, " \
            f"name='{self.name}', " \
            f"typeid='{self.typeid}', " \
            f"start_addr=0x{self.start_addr:08x}, " \
            f"end_addr=0x{self.end_addr:08x}, " \
            f"lineno={self.lineno})" \


def read_stabs_info(data: bytes) -> ProgramNode:
    # With GCC, the stab table starts with a stab of type N_UNDF. The description field
    # of this stab contains the size of the stabs table in bytes for this compilation unit
    # (including this first stab), the value field is the size of the string table.
    # This format is somewhat described in the file binutils-gdb/bfd/stabs.c of the
    # GNU Binutils and GDB sources.
    offset = 0
    stab = Stab.from_buffer_copy(data[offset:])
    if stab.type == StabTypes.N_UNDF:
        nstabs  = int(stab.desc / sizeof(Stab))
        offset += sizeof(Stab)
        stab_table = data[offset:]                             # stab table without first stab
        str_table  = data[offset + sizeof(Stab) * nstabs:]     # string table
        logger.debug(f"Stab table contains {nstabs} entries")
    else:
        raise ValueError("Stab table does not start with stab N_UNDF")

    offset  = 0
    stabs   = []
    for i in range(0, nstabs - 1):
        stab = Stab.from_buffer_copy(stab_table[offset:])
        string = _get_string_from_buffer(str_table[stab.offset:])
        offset += sizeof(stab)
        try:
            logger.debug("Stab: type = {}, string = '{}' (at 0x{:x}), other = 0x{:x}, desc = 0x{:x}, value = 0x{:08x}".format(
                StabTypes(stab.type).name,
                string,
                stab.offset,
                stab.other,
                stab.desc,
                stab.value
            ))
        except ValueError:
            try:
                # stab probably contains external symbol => clear N_EXT bit to look up name
                logger.debug(
                    "Stab: type = {} (external), string = '{}' (at 0x{:x}), other = 0x{:x}, desc = 0x{:x}, value = 0x{:08x}".format(
                        StabTypes(stab.type & ~StabTypes.N_EXT).name,
                        string,
                        stab.offset,
                        stab.other,
                        stab.desc,
                        stab.value
                    )
                )
            except ValueError:
                logger.error(f"Stab with unknown type 0x{stab.type:02x} found")
                continue

        # process stab
        if stab.type == StabTypes.N_LSYM and stab.value == 0:
            # TODO: type definition => add it to data dictionary
            pass
        elif stab.type in (
            StabTypes.N_SO,
            StabTypes.N_GSYM,
            StabTypes.N_STSYM,
            StabTypes.N_LSYM,
            StabTypes.N_PSYM,
            StabTypes.N_FUN,
            StabTypes.N_LBRAC,
            StabTypes.N_RBRAC,
            StabTypes.N_SLINE
        ):
            # add stab to list for building tree structure
            stabs.append((stab, string))

    # build tree structure from the stabs describing the program (sort of a simplified AST), reverse list first 
    # so that _build_program_tree() can use pop()
    stabs.reverse()
    # root node of program
    program = ProgramNode(StabTypes.N_UNDF, '')
    while stabs:
        # loop over all compilation units
        node = _build_program_tree(stabs)
        program.children.append(node)
    return program


def print_program_node(node: ProgramNode, indent: int = 0):
    print(' ' * indent + str(node))
    indent += 4
    for node in node.children:
        print_program_node(node, indent)


def get_addr_by_line(program: ProgramNode, line: int) -> int:
    raise NotImplementedError


def get_addr_by_name(program: ProgramNode, name: str) -> int:
    raise NotImplementedError


def _get_string_from_buffer(buffer: bytes) -> str:
    idx = 0
    while idx < len(buffer) and buffer[idx] != 0:
        idx += 1
    if idx < len(buffer):
        return buffer[0:idx].decode('ascii')
    else:
        raise ValueError("no terminating NUL byte found in buffer")


def _build_program_tree(stabs: List[Stab], nodes: List[ProgramNode] = []) -> ProgramNode:
    # The stabs are emitted by the compiler (at least by GCC) in two different orders.
    # Local variables (and nested functions) appear *before* the enclosing scope.
    # Therefore we push their nodes onto a stack when we see them and pop them again
    # when we see the beginning of the enclosing scope.
    # Nested scopes on the other hand appear in the correct order, that is from outer to
    # inner. We handle them by recursively calling ourselves for each range of stabs
    # between N_LBRAC and N_RBRAC. Tricky stuff...
    node = None
    # set source directory to empty string because if there is just one compilation unit
    # there is no N_SO stab for the directory
    srcdir = ''
    while stabs:
        stab, string = stabs.pop()
        if stab.type == StabTypes.N_SO:
            if node is None:
                # new compilation unit => create new node
                if string.endswith('/'):
                    # stab for source directory
                    srcdir = string
                else:
                    # stab for file name
                    node = ProgramNode(StabTypes.N_SO, srcdir + string)
            else:
                # end of compilation unit => push stab back onto stack, add any functions
                # on the stack to current scope and return node for compilation unit
                stabs.append((stab, string))
                while nodes:
                    child = nodes.pop()
                    node.children.append(child)
                return node

        elif stab.type in (StabTypes.N_GSYM, StabTypes.N_STSYM, StabTypes.N_LCSYM):
            # global or file-scoped variable => store it in current node (compilation unit)
            symbol, typeid = string.split(':', 1)
            if node is None:
                raise AssertionError("Stab for global or file-scoped variable but no current node")
            node.children.append(ProgramNode(stab.type, symbol, typeid=typeid, start_addr=stab.value))

        elif stab.type in (StabTypes.N_LSYM, StabTypes.N_PSYM, StabTypes.N_RSYM):
            # local variable or function parameter => put it on the stack,the stab for the
            # scope (N_LBRAC) comes later. In case of register variables (N_RSYM), the value
            # is the register number with 0..7 = D0..D7 and 8..15 = A0..A7.
            symbol, typeid = string.split(':', 1)
            nodes.append(ProgramNode(stab.type, symbol, typeid=typeid, start_addr=stab.value))

        elif stab.type  == StabTypes.N_FUN:
            # function => put it on the stack, the stab for the scope (N_LBRAC) comes later
            # We change the type to N_FNAME so that we can differentiate between a node with
            # the scope of the function (N_FUN) and a node with just its name and start address (N_FNAME).
            # TODO: Maybe it would be better to use our own types for the program nodes.
            symbol, typeid = string.split(':', 1)
            nodes.append(ProgramNode(StabTypes.N_FNAME, symbol, typeid=typeid, start_addr=stab.value))

        elif stab.type  == StabTypes.N_SLINE:
            # line number / address tuple => put it on the stack, the stab for the scope (N_LBRAC) comes later
            # TODO: The tuples appear in the tree sorted by address in descending order. Is this
            # ok for the debugger? What about duplicate line numbers / addresses? Should we keep
            # only the first / the last?
            nodes.append(ProgramNode(StabTypes.N_SLINE, '', lineno=stab.desc, start_addr=stab.value))

        elif stab.type == StabTypes.N_LBRAC:
            # beginning of scope
            if node is not None:
                # current scope exists => we call ourselves to create new scope
                stabs.append((stab, string))                        # push current stab onto stack again
                child = _build_program_tree(stabs, nodes)
                if child.type == StabTypes.N_FUN:
                    # child is function => push it onto stack because nested functions appear
                    # *before* the enclosing scope
                    nodes.append(child)
                elif child.type == StabTypes.N_LBRAC:
                    # child is scope => add it to current scope because nested scopes appear
                    # *after* the enclosing scope
                    node.children.append(child)
                else:
                    raise AssertionError(f"Child is neither function nor scope, type = {StabTypes(child.type).name}")
            else:
                # current scope does not exist => we've just been called to create new scope
                node = ProgramNode(StabTypes.N_LBRAC, f'SCOPE@0x{stab.value:08x}', start_addr=stab.value)
                # add all nodes on the stack as children
                while nodes:
                    child = nodes.pop()
                    node.children.append(child)
                    if child.type == StabTypes.N_FNAME:
                        # change type to N_FUN so that our caller will put this scope onto the stack
                        # and change name to the function's name
                        node.type = StabTypes.N_FUN
                        node.name = child.name

        elif stab.type == StabTypes.N_RBRAC:
            # end of scope => add end address and return created scope
            node.end_addr = stab.value
            return node

    # add any functions on the stack to current scope and return node for compilation unit
    while nodes:
        child = nodes.pop()
        node.children.append(child)
    return node
