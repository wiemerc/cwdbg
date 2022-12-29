#
# errors.py - part of cwdbg, a debugger for the AmigaOS
#             This file contains the error codes.
#
# Copyright(C) 2018-2022 Constantin Wiemer


from enum import IntEnum


# TODO: Move to debugger.py
# keep in sync with values in target.h
class ErrorCodes(IntEnum):
    ERROR_OK                     = 0
    ERROR_NOT_ENOUGH_MEMORY      = 1
    ERROR_INVALID_ADDRESS        = 2
    ERROR_UNKNOWN_BREAKPOINT     = 3
    ERROR_LOAD_TARGET_FAILED     = 4
    ERROR_CREATE_PROC_FAILED     = 5
    ERROR_UNKNOWN_STOP_REASON    = 6
    ERROR_NO_TRAP                = 7
    ERROR_RUN_COMMAND_FAILED     = 8
    ERROR_BAD_DATA               = 9
    ERROR_OPEN_LIB_FAILED        = 10
