#
# stabslib.py - part of CWDebug, a source-level debugger for the AmigaOS
#               This file contains the classes to read and process the debug information in STABS format and
#               provide this information to the debugger.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from copy import copy
from dataclasses import dataclass, field
from enum import IntEnum
from ctypes import BigEndianStructure, c_uint8, c_uint16, c_uint32, sizeof
from loguru import logger


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


@dataclass
class ProgramNode:
    type: int
    name: str
    typeid: str = ''
    start_addr: int = 0
    end_addr: int = 0
    lineno: int = 0
    children: list['ProgramNode'] = field(default_factory=list)

    def __str__(self) -> str:
        # TODO: look up type id in data dictionary => typeid_to_type()
        return f"ProgramNode(type={StabTypes(self.type).name}, " \
            f"name='{self.name}', " \
            f"typeid='{self.typeid}', " \
            f"start_addr=0x{self.start_addr:08x}, " \
            f"end_addr=0x{self.end_addr:08x}, " \
            f"lineno={self.lineno})" \


    @staticmethod
    def print_node(node: 'ProgramNode', indent: int = 0):
        logger.debug(' ' * indent + str(node))
        indent += 4
        for child in node.children:
            ProgramNode.print_node(child, indent)


@dataclass
class Type:
    name: str
    referenced_type: int = None
    min_val: int = None
    max_val: int = None
    num_bits: int = None


class ProgramWithDebugInfo:
    def __init__(self, stabs: list[Stab]):
        builder = DataDictBuilder(stabs)
        builder.build()

        builder = ProgramTreeBuilder(stabs)
        builder.build()
        self._program_tree = builder.get_tree()
        self._addresses_by_lineno = builder.get_addresses_by_lineno()



    @staticmethod
    def from_stabs_data(data: bytes) -> 'ProgramWithDebugInfo':
        # With GCC, the stab table starts with a stab of type N_UNDF. The description field of this stab contains
        # the size of the stabs table in bytes for this compilation unit (including this first stab), the value field
        # is the size of the string table. This format is somewhat described in the file binutils-gdb/bfd/stabs.c
        # of the GNU Binutils and GDB sources.
        offset = 0
        stab = Stab.from_buffer_copy(data[offset:])
        if stab.type == StabTypes.N_UNDF:
            num_stabs  = int(stab.desc / sizeof(Stab))
            offset += sizeof(Stab)
            stab_table = data[offset:]  # stab table without first stab
            string_table  = data[offset + sizeof(Stab) * num_stabs:]
            logger.debug(f"Stab table contains {num_stabs} entries")
        else:
            raise ValueError("Stab table does not start with stab N_UNDF")

        offset  = 0
        stabs: list[Stab] = []
        for i in range(0, num_stabs - 1):
            stab = Stab.from_buffer_copy(stab_table[offset:])
            stab.string = ProgramWithDebugInfo._get_string_from_buffer(string_table[stab.offset:])
            offset += sizeof(stab)
            try:
                logger.debug("Stab(type={}, string='{}' (at 0x{:x}), other=0x{:x}, desc=0x{:x}, value=0x{:08x})".format(
                    StabTypes(stab.type).name,
                    stab.string,
                    stab.offset,
                    stab.other,
                    stab.desc,
                    stab.value
                ))
            except ValueError:
                try:
                    # stab probably contains external symbol => clear N_EXT bit to look up name
                    logger.debug("Stab(type={}, string='{}' (at 0x{:x}), other=0x{:x}, desc=0x{:x}, value=0x{:08x})".format(
                            StabTypes(stab.type & ~StabTypes.N_EXT).name,
                            stab.string,
                            stab.offset,
                            stab.other,
                            stab.desc,
                            stab.value
                        )
                    )
                except ValueError:
                    logger.error(f"Stab with unknown type 0x{stab.type:02x} found")
                    continue

            if stab.type in (
                StabTypes.N_SO,
                StabTypes.N_GSYM,
                StabTypes.N_STSYM,
                StabTypes.N_LSYM,
                StabTypes.N_PSYM,
                StabTypes.N_FUN,
                StabTypes.N_LBRAC,
                StabTypes.N_RBRAC,
                StabTypes.N_SLINE
                # The other types are not relevant for us.
            ):
                stabs.append((stab))
        return ProgramWithDebugInfo(stabs)


    def get_addr_range_for_lineno(self, lineno: int, comp_unit: str | None = None) -> tuple[int, int] | None:
        if comp_unit is None:
            if len(self._addresses_by_lineno.keys()) == 1:
                comp_unit = next(iter(self._addresses_by_lineno.keys()))
            else:
                raise ValueError("Compilation unit can't be omitted because the program consists of more than one")
        if comp_unit in self._addresses_by_lineno and lineno in self._addresses_by_lineno[comp_unit]:
            return self._addresses_by_lineno[comp_unit][lineno]
        else:
            return None


    def get_addr_range_for_func_name(self, name: str) -> int | None:
        raise NotImplementedError


    def get_comp_unit_for_addr(self, addr: int) -> str | None:
        for child in self._program_tree.children:
            if child.type == StabTypes.N_SO:
                if addr >= child.start_addr:
                    # TODO: How to get end address for the last compilation unit so that we can correctly tell if an address is contained in it?
                    # TODO: Compile startup code (from libnix) with debug information so that it shows up as compilation unit
                    if child.end_addr == 0 or addr < child.end_addr:
                        return child.name
            else:
                raise AssertionError(f"Found top-level node that is not a compilation unit, type = {StabTypes(child.type).name}")
        else:
            return None


    def get_lineno_for_addr(self, addr: int, comp_unit: str | None = None) -> int | None:
        # TODO: Store address ranges in a segment tree so we can look up the line number for an address more quickly.
        #       See https://leetcode.com/problems/range-sum-query-mutable/discuss/75784/python-well-commented-solution-using-segment-trees
        if comp_unit is None:
            if len(self._addresses_by_lineno.keys()) == 1:
                comp_unit = next(iter(self._addresses_by_lineno.keys()))
            else:
                raise ValueError("Compilation unit can't be omitted because the program consists of more than one")
        if comp_unit in self._addresses_by_lineno:
            for lineno, addr_range in self._addresses_by_lineno[comp_unit].items():
                if addr_range[0] <= addr < addr_range[1]:
                    return lineno
            else:
                return None
        else:
            return None


    def get_func_name_for_addr(self, addr: int) -> str | None:
        raise NotImplementedError


    @staticmethod
    def _get_string_from_buffer(buffer: bytes) -> str:
        idx = 0
        while idx < len(buffer) and buffer[idx] != 0:
            idx += 1
        if idx < len(buffer):
            return buffer[0:idx].decode('ascii')
        else:
            raise ValueError("No terminating NUL byte found in buffer")


class DataDictBuilder:
    def __init__(self, stabs: list[Stab]):
        self._stabs = [stab for stab in stabs if stab.type == StabTypes.N_LSYM and stab.value == 0]
        

    def build(self):
        for stab in self._stabs:
            if stab.type == StabTypes.N_LSYM and stab.value == 0:
                # type definition => add it to data dictionary
                type_name, type_info = stab.string.split(':', maxsplit=1)
                if type_info[0] == 't':
                    type_num, type_def_or_ref = type_info.split('=', maxsplit=1)
                    type_num = type_num[1:]  # skip 't'
                    logger.debug(f"Type '{type_name}' has number {type_num}")
                else:
                    logger.warning(f"Stab with type N_LSYM and value = 0 doesn't contain type definition")


# We build a tree structure from the stabs describing the program (sort of a simplified AST) because we need
# to know which local variables a scope contains. In addition, we store the line number - address tuples for fast lookup.
class ProgramTreeBuilder:
    def __init__(self, stabs: list[Stab]):
        self._stabs = [stab for stab in stabs if not (stab.type == StabTypes.N_LSYM and stab.value == 0)]
        self._nodes_stack: list[ProgramNode] = []
        self._func_nodes_stack: list[ProgramNode] = []
        self._addresses_by_lineno: dict[str, dict[int, tuple[int, int]]] = {}

    def build(self):
        self._root_node = ProgramNode(StabTypes.N_UNDF, '')
        # reverse list so build() can use pop()
        self._stabs.reverse()
        while self._stabs:
            # loop over all compilation units
            comp_unit_node = self._build_subtree()
            self._root_node.children.append(comp_unit_node)
        logger.debug("Program tree:")
        ProgramNode.print_node(self._root_node)
    

    def get_tree(self) -> ProgramNode:
        return self._root_node


    def get_addresses_by_lineno(self) -> dict[str, dict[int, tuple[int, int]]]:
        return self._addresses_by_lineno


    def _build_subtree(self,
        current_comp_unit: str = None,
        current_func_lineno: int = None,
        prev_lineno: int = None,
    ) -> ProgramNode:
        # The stabs are emitted by the compiler (at least by GCC) in two different orders. Local variables (and nested
        # functions) appear *before* the enclosing scope. The same is true for line number - address pairs, they appear
        # before the function definition. Therefore we push their nodes onto a stack when we see them and pop them again
        # when we see the beginning of the enclosing scope / the function definition. Function parameters and nested
        # scopes on the other hand appear in the correct order, the parameters after the function definition, the scopes
        # from outer to inner. The nodes for functions and scopes (with all their children) are created by recursively
        # calling this function.

        node = None
        # set source directory to empty string because if there is just one compilation unit
        # there is no N_SO stab for the directory
        srcdir = ''
        while self._stabs:
            stab = self._stabs.pop()
            if stab.type == StabTypes.N_SO:
                if node is None:
                    # new compilation unit => create new node
                    if stab.string.endswith('/'):
                        # stab for source directory
                        srcdir = stab.string
                    else:
                        # stab for file name
                        node = ProgramNode(StabTypes.N_SO, srcdir + stab.string, start_addr=stab.value)
                        current_comp_unit = srcdir + stab.string
                        self._addresses_by_lineno[current_comp_unit] = {}
                else:
                    # end of compilation unit => use start address of next compilation unit as end address of this one,
                    #                            add any functions on the stack to current node and return it
                    # TODO: Can we get an end address if there is only one compilation unit?
                    self._stabs.append((stab, stab.string))
                    node.end_addr = stab.value
                    node.children.extend(self._func_nodes_stack)
                    self._func_nodes_stack.clear()
                    return node

            elif stab.type in (StabTypes.N_GSYM, StabTypes.N_STSYM, StabTypes.N_LCSYM):
                # global or file-scoped variable => add it to the current node (a compilation unit)
                symbol, typeid = stab.string.split(':', 1)
                assert node is not None, "Encountered stab for global or file-scoped variable but no current node"
                node.children.append(ProgramNode(stab.type, symbol, typeid=typeid, start_addr=stab.value))

            elif stab.type in (StabTypes.N_LSYM, StabTypes.N_RSYM):
                # local variable => put it on the stack, the stab for the scope (N_LBRAC) comes later.
                # In case of register variables (N_RSYM), the value is the register number
                # with 0..7 = D0..D7 and 8..15 = A0..A7.
                symbol, typeid = stab.string.split(':', 1)
                self._nodes_stack.append(ProgramNode(stab.type, symbol, typeid=typeid, start_addr=stab.value))

            elif stab.type == StabTypes.N_PSYM:
                # function parameter => add it to the current node (a function)
                symbol, typeid = stab.string.split(':', 1)
                assert node is not None, "Encountered stab for function parameter but no current node"
                node.children.append(ProgramNode(stab.type, symbol, typeid=typeid, start_addr=stab.value))

            elif stab.type == StabTypes.N_FUN:
                # beginning of function
                if node is not None:
                    self._stabs.append(stab)
                    if node.type == StabTypes.N_FUN:
                        # use start address of the next function as end address of the one just created and return it
                        node.end_addr = stab.value
                        return node
                    elif node.type == StabTypes.N_SO:
                        # call ourselves to create new function and push it onto the stack
                        child = self._build_subtree(current_comp_unit=node.name, prev_lineno=prev_lineno)
                        if child.type == StabTypes.N_FUN:
                            self._func_nodes_stack.append(child)
                        else:
                            raise AssertionError(
                                f"Encountered N_FUN stab but created child is not a function, "
                                f"type = {StabTypes(child.type).name}"
                            )
                    else:
                        raise AssertionError(f"Encountered N_FUN stab but current node is not any of N_FUN / N_SO")
                else:
                    # no current node => we've just been called to create new function
                    symbol, typeid = stab.string.split(':', 1)
                    node = ProgramNode(StabTypes.N_FUN, symbol, lineno=stab.desc, start_addr=stab.value)
                    node.children.extend(self._nodes_stack)
                    self._nodes_stack.clear()

            elif stab.type == StabTypes.N_SLINE:
                # line number / address tuple => put it on the stack, the stab for the function (N_FUN) comes later,
                #                                and also store them for fast lookup
                self._nodes_stack.append(ProgramNode(StabTypes.N_SLINE, '', lineno=stab.desc, start_addr=stab.value))
                # For some reason unknown to me, there are multiple addresses for one line sometimes. However,
                # it seems the first is always the start of code block for the line, so we store only the first.
                assert current_comp_unit is not None, "Encountered N_SLINE stab but current compilation unit is not set"
                if prev_lineno is not None and prev_lineno < stab.desc:
                    # If we've seen an N_SLINE stab before we use the start address of the current one as end address
                    # of the previous one.
                    start_addr, _ = self._addresses_by_lineno[current_comp_unit][prev_lineno]
                    self._addresses_by_lineno[current_comp_unit][prev_lineno] = (start_addr, stab.value)
                    logger.debug(f"Line #{prev_lineno} is at address range 0x{start_addr:08x}-0x{stab.value:08x}")
                    # TODO: Can we get an end address for the last line in the compilation unit?
                if not stab.desc in self._addresses_by_lineno[current_comp_unit]:
                    self._addresses_by_lineno[current_comp_unit][stab.desc] = (stab.value, 0)
                    prev_lineno = stab.desc

            elif stab.type == StabTypes.N_LBRAC:
                # beginning of scope
                if node is not None:
                    # function / scope exists => call ourselves to create new scope
                    self._stabs.append(stab)
                    child = self._build_subtree(current_func_lineno=node.lineno)
                    if child.type == StabTypes.N_LBRAC:
                        node.children.append(child)
                    else:
                        raise AssertionError(
                            f"Encountered N_LBRAC stab but created child is not a scope, "
                            f"type = {StabTypes(child.type).name}"
                        )
                else:
                    # no current node => we've just been called to create new scope
                    node = ProgramNode(StabTypes.N_LBRAC, f'SCOPE@0x{stab.value:08x}', start_addr=stab.value)
                    node.children.extend(self._nodes_stack)
                    self._nodes_stack.clear()
                    assert current_func_lineno is not None, "Encountered N_LBRAC stab but line number of current function is not set"
                    if self._func_nodes_stack and self._func_nodes_stack[0].lineno > current_func_lineno:
                        # function on stack is a nested function => add it to current scope
                        node.children.extend(self._func_nodes_stack)
                        self._func_nodes_stack.clear()

            elif stab.type == StabTypes.N_RBRAC:
                # end of scope => add end address and return created scope
                node.end_addr = stab.value
                return node

            else:
                raise AssertionError(f"Unknown stab type {StabTypes(stab.type).name}")

        # add any functions on the stack to compilation unit and return it
        if node.type == StabTypes.N_SO:
            node.children.extend(self._func_nodes_stack)
            self._func_nodes_stack.clear()
        return node
