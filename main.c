/*
 * main.c - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <exec/types.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "debugger.h"
#include "m68k.h"
#include "serio.h"
#include "util.h"


/*
 * global variables
 */
BPTR                 g_logfh;            // for the LOG() macro
UBYTE                g_loglevel;
char                 g_logmsg[256];


extern void exc_handler();


int main(int argc, char **argv)
{
    int                 status = RETURN_OK;         // exit status
    struct Task         *self = FindTask(NULL);     // pointer to this task
    APTR                old_exc_handler;            // previous execption handler

    // setup logging
    // TODO: get rid of g_logfh
    // TODO: specify log level via command-line switch
    g_logfh = Output();
    g_loglevel = DEBUG;

    if (argc == 1) {
        LOG(ERROR, "no target specified - usage: cwdebug <target> [args]");
        status = RETURN_ERROR;
        goto ERROR_WRONG_USAGE;
    }

    LOG(INFO, "initializing...");

    // allocate trap and install exception handler
    old_exc_handler   = self->tc_TrapCode;
    self->tc_TrapCode = exc_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        status = RETURN_ERROR;
        goto ERROR_NO_TRAP;
    }

    // initialize disassembler routines
    m68k_build_opcode_table();

    // initialize serial IO
    if (serio_init() == DOSFALSE) {
        LOG(ERROR, "could not initialize serial IO");
        status = RETURN_ERROR;
        goto ERROR_NO_SERIAL_IO;
    }
    else {
        LOG(INFO, "serial IO initialized");
    }

    // hand over control to debug_main() which does all the work
    status = load_and_init_target(argv[1]);

    serio_exit();
ERROR_NO_SERIAL_IO:
    FreeTrap(TRAP_NUM);
ERROR_NO_TRAP:
    self->tc_TrapCode = old_exc_handler;
ERROR_WRONG_USAGE:
    return status;
}
