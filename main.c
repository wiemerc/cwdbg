/*
 * main.c - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <dos/dos.h>
#include <exec/types.h>
#include <proto/exec.h>

#include "debugger.h"
#include "m68k.h"
#include "serio.h"
#include "util.h"


// TODO: move files to server/


int main(int argc, char **argv)
{
    int status;

    // setup logging
    // TODO: specify log level via command line switch
    g_loglevel = DEBUG;

    if (argc == 1) {
        LOG(ERROR, "no target specified - usage: cwdebug <target> [args]");
        return RETURN_ERROR;
    }

    LOG(INFO, "initializing...");
    // initialize disassembler routines
    m68k_build_opcode_table();
    LOG(INFO, "initialized disassembler routines");

    // initialize serial IO
    if (serio_init() == DOSFALSE) {
        LOG(ERROR, "could not initialize serial IO");
        return RETURN_ERROR;
    }
    else {
        LOG(INFO, "initialized serial IO");
    }

    // hand over control to load_and_init_target() which does all the work
    // TODO: either run in local or remote mode, specified by command line switch
    status = load_and_init_target(argv[1]);
    serio_exit();
    return status;
}
