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
#include <proto/dos.h>
#include <proto/exec.h>

#include "cli.h"
#include "debugger.h"
#include "m68k.h"
#include "serio.h"
#include "server.h"
#include "util.h"


// TODO: clean up files:
// - sections in the same order with headings
// - header for each routine
// - convert all C to C++ comments
// - start all log messages with an uppercase letter
int main()
{
    struct RDArgs *p_rdargs;
    long args[3] = {0l, 0l, 0l}, f_debug, f_server;
    char *p_target;

    g_loglevel = INFO;
    if ((p_rdargs = ReadArgs("-d=--debug/S,-s=--server/S,target/A", args, NULL)) == NULL) {
        LOG(ERROR, "wrong usage - usage: cwdebug [-d/--debug] [-s/--server] <target>");
        return RETURN_FAIL;
    }
    f_debug  = args[0];
    f_server = args[1];
    p_target = (char *) args[2];
    if (f_debug == DOSTRUE)
        g_loglevel = DEBUG;

    LOG(INFO, "Initializing...");
    if (load_and_init_target(p_target) == DOSFALSE) {
        LOG(ERROR, "Could not load and initialize target")
        FreeArgs(p_rdargs);
        return RETURN_FAIL;
    }
    else {
        LOG(INFO, "Loaded and initialized target");
    }

    if (f_server == DOSTRUE) {
        if (serio_init() == DOSFALSE) {
            LOG(ERROR, "Could not initialize serial IO");
            FreeArgs(p_rdargs);
            return RETURN_FAIL;
        }
        else {
            LOG(INFO, "Initialized serial IO");
        }
        g_dstate.p_process_commands_func = process_remote_commands;
        process_remote_commands(NULL);
    }
    else {
        m68k_build_opcode_table();
        LOG(INFO, "Initialized disassembler routines");
        g_dstate.p_process_commands_func = process_cli_commands;
        process_cli_commands(NULL);
    }
    FreeArgs(p_rdargs);
    return RETURN_OK;
}
