#
# hunklib.py - part of CWDebug, a source-level debugger for the AmigaOS
#              This file contains the routines to read executables in the Amiga Hunk format,
#              including the debug information.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from enum import IntEnum
from loguru import logger
from struct import unpack


# block types from from dos/doshunks.h
class BlockTypes(IntEnum):
    HUNK_UNIT	  = 999
    HUNK_NAME	  = 1000
    HUNK_CODE	  = 1001
    HUNK_DATA	  = 1002
    HUNK_BSS	  = 1003
    HUNK_RELOC32  = 1004
    HUNK_RELOC16  = 1005
    HUNK_RELOC8	  = 1006
    HUNK_EXT	  = 1007
    HUNK_SYMBOL	  = 1008
    HUNK_DEBUG	  = 1009
    HUNK_END	  = 1010
    HUNK_HEADER	  = 1011
    HUNK_OVERLAY  = 1013
    HUNK_BREAK	  = 1014
    HUNK_DREL32	  = 1015
    HUNK_DREL16	  = 1016
    HUNK_DREL8	  = 1017
    HUNK_LIB	  = 1018
    HUNK_INDEX	  = 1019


# symbol types from from dos/doshunks.h
class SymbolTypes(IntEnum):
    EXT_SYMB   = 0
    EXT_DEF	   = 1
    EXT_ABS	   = 2
    EXT_RES	   = 3
    EXT_REF32  = 129
    EXT_COMMON = 130
    EXT_REF16  = 131
    EXT_REF8   = 132
    EXT_DEXT32 = 133
    EXT_DEXT16 = 134
    EXT_DEXT8  = 135


def _read_word(exe_file) -> int:
    buffer = exe_file.read(4)
    if buffer:
        return unpack('>L', buffer)[0]
    else:
        raise EOFError


def _read_string(exe_file, nchars) -> str:
    buffer = exe_file.read(nchars)
    if buffer:
        return unpack(f'{nchars}s', buffer)[0].decode('ascii').replace('\x00', '')
    else:
        return EOFError


def _read_header_block(exe_file):
    logger.debug("HUNK_HEADER block... file is an AmigaDOS executable")
    logger.debug(f"Long words reserved for resident libraries: {_read_word(exe_file)}")
    logger.debug(f"Number of hunks: {_read_word(exe_file)}")
    first_hunk = _read_word(exe_file)
    last_hunk = _read_word(exe_file)
    logger.debug(f"Number of first hunk: {first_hunk}")
    logger.debug(f"Number of last hunk: {last_hunk}")
    for hunk_num in range(first_hunk, last_hunk + 1):
        logger.debug(f"Size (in bytes) of hunk #{hunk_num}: {_read_word(exe_file) * 4}")


def _read_unit_block(exe_file):
    logger.info("HUNK_UNIT block... file is an AmigaDOS object file")
    logger.info(f"Unit name: {_read_string(exe_file)}")


def _read_name_block(exe_file):
    logger.info(f"Hunk name: {_read_string(exe_file)}")


def _read_code_block(exe_file) -> bytes:
    nwords = _read_word(exe_file)
    logger.debug(f"Size (in bytes) of code block: {nwords * 4}")
    return exe_file.read(nwords * 4)


def _read_data_block(exe_file):
    nwords = _read_word(exe_file)
    logger.debug(f"Size (in bytes) of data block: {nwords * 4}")
    return exe_file.read(nwords * 4)


def _read_bss_block(exe_file):
    nwords = _read_word(exe_file)
    logger.debug(f"Size (in bytes) of BSS block: {nwords * 4}")


def _read_ext_block(exe_file):
    # For now, we just log the content of the HUNK_EXT, HUNK_SYMBOL and HUNK_RELOC32 blocks. We could
    # also return the content to the users of the library for further processing. But either way, we
    # need to iterate over it because there is no size field in these blocks but they consist of lists
    # terminated by elements of size 0.
    while True:
        type_len = _read_word(exe_file)
        if type_len == 0:
            break

        sym_type = (type_len & 0xff000000) >> 24
        sym_name = _read_string(exe_file, (type_len & 0x00ffffff) * 4)

        if sym_type in (SymbolTypes.EXT_DEF, SymbolTypes.EXT_ABS, SymbolTypes.EXT_RES):
            # definition
            logger.debug(f"Definition of symbol (type = {sym_type}): {sym_name} = 0x{_read_word(exe_file)}")
        elif sym_type in (SymbolTypes.EXT_REF8, SymbolTypes.EXT_REF16, SymbolTypes.EXT_REF32):
            # reference(s)
            nrefs = _read_word(exe_file)
            for i in range(0, nrefs):
                logger.debug(f"Reference to symbol {sym_name} (type = {sym_type}): 0x{_read_word(exe_file)}")
        else:
            raise ValueError(f"Symbol type {sym_type} not supported")


def _read_symbol_block(exe_file):
    while True:
        nwords = _read_word(exe_file)
        if nwords == 0:
            break

        sym_name = _read_string(exe_file, nwords * 4)
        sym_val  = _read_word(exe_file)
        logger.debug(f"{sym_name} = 0x{sym_val:08x}")


def _read_reloc32_block(exe_file):
    while True:
        noffsets = _read_word(exe_file)
        if noffsets == 0:
            break

        ref_hnum = _read_word(exe_file)
        logger.debug(f"Relocations referencing hunk #{ref_hnum}:")
        for i in range(0, noffsets):
            logger.debug(f"Position = 0x{_read_word(exe_file)}")


def _read_debug_block(exe_file):
    nwords = _read_word(exe_file)
    data   = exe_file.read(nwords * 4)
    offset = 0

    # The content of a HUNK_DEBUG block was not specified by Commodore. Different compilers
    # used different formats for the debug information. We support two different formats:
    # - the LINE format used by SAS/C that only contains a line / offset table. This format
    #   is also generated by VBCC / VLINK and the code below is based on the function
    #   linedebug_hunks() in the file t_amigahunk.c from VLINK.
    # - the STABS format that was also popular on UNIX and was used by GCC that contains
    #   type definitions, a list of all functions and variables and a line / offset table
    if data[offset + 4:offset + 8] == b'LINE':
        logger.debug("Format is assumed to be LINE (SAS/C or VBCC) - dumping it")
        logger.debug(f"Section offset: 0x{unpack('>L')}")
        offset += 8  # skip section offset and 'LINE'
        nwords_fname = unpack('>L', data[offset:offset + 4])[0]
        offset += 4
        logger.debug(f"File name: {data[offset:offset + nwords_fname * 4].decode()}")
        nwords = nwords - nwords_fname - 3
        offset += nwords_fname * 4
        logger.debug("Outputting line table:")
        while nwords > 0:
            line = unpack('>L', data[offset:offset + 4])[0]
            offset += 4
            addr = unpack('>L', data[offset:offset + 4])[0]
            offset += 4
            logger.debug(f"Line #{line} at address 0x{addr}")
            nwords -= 2
    else:
        logger.debug("Format is assumed to be STABS (GCC)")
        return data


READ_FUNC_BY_BLOCK_TYPE = {
    BlockTypes.HUNK_HEADER:  _read_header_block,
    BlockTypes.HUNK_UNIT:    _read_unit_block,
    BlockTypes.HUNK_NAME:    _read_name_block,
    BlockTypes.HUNK_CODE:    _read_code_block,
    BlockTypes.HUNK_DATA:    _read_data_block,
    BlockTypes.HUNK_BSS:     _read_bss_block,
    BlockTypes.HUNK_EXT:     _read_ext_block,
    BlockTypes.HUNK_SYMBOL:  _read_symbol_block,
    BlockTypes.HUNK_RELOC32: _read_reloc32_block,
    BlockTypes.HUNK_DEBUG:   _read_debug_block
}


def read_exe(fname: str) -> dict[int, bytes]:
    content_by_type: dict[int, bytes] = {}
    hunk_num = 0
    logger.info("Reading executable...")
    with open(fname, 'rb') as exe_file:
        while True:
            try:
                block_type = _read_word(exe_file)
                logger.debug(f"Reading hunk #{hunk_num}, {BlockTypes(block_type).name} ({block_type}) block")
                if block_type == BlockTypes.HUNK_END:
                    # possibly another hunk follows, nothing else to do
                    logger.debug(f"End of hunk #{hunk_num} reached")
                    hunk_num += 1
                    continue
                else:
                    content_by_type[block_type] = READ_FUNC_BY_BLOCK_TYPE[block_type](exe_file)

            except EOFError:
                if block_type == BlockTypes.HUNK_END:
                    break
                else:
                    logger.error(f"Encountered EOF while reading file '{fname}'")
                    break

            except KeyError as ex:
                logger.error(f"Block type {ex} not known or implemented")
                break

            except Exception as ex:
                logger.error(f"Error occured while reading file: {ex}")
                raise

    return content_by_type


def get_debug_infos_from_exe(fname: str) -> bytes:
    return read_exe(fname)[BlockTypes.HUNK_DEBUG]
