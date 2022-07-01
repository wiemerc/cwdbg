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
    Debugger dbg;
    long args[3] = {0l, 0l, 0l}, f_debug, f_server;
    char *p_target;

    if (init_debugger(&dbg) == DOSFALSE) {
        LOG(ERROR, "Could not initialize debugger object");
        return RETURN_FAIL;
    }

    g_loglevel = INFO;
    if ((dbg.p_rdargs = ReadArgs("-d=--debug/S,-s=--server/S,target/A", args, NULL)) == NULL) {
        LOG(ERROR, "wrong usage - usage: cwdebug [-d/--debug] [-s/--server] <target>");
        quit_debugger(&dbg, RETURN_FAIL);
    }
    f_debug  = args[0];
    f_server = args[1];
    p_target = (char *) args[2];
    if (f_debug == DOSTRUE)
        g_loglevel = DEBUG;

    if (load_target(&dbg, p_target) == DOSFALSE) {
        LOG(ERROR, "Could not load target")
        quit_debugger(&dbg, RETURN_FAIL);
    }
    LOG(INFO, "Loaded target");

    if (f_server == DOSTRUE) {
        if (serio_init() == DOSFALSE) {
            LOG(ERROR, "Could not initialize serial IO");
            quit_debugger(&dbg, RETURN_FAIL);
        }
        else {
            LOG(INFO, "Initialized serial IO");
        }
        dbg.p_process_commands_func = process_remote_commands;
        process_remote_commands(&dbg, NULL);
    }
    else {
        m68k_build_opcode_table();
        LOG(INFO, "Initialized disassembler routines");
        dbg.p_process_commands_func = process_cli_commands;
        process_cli_commands(&dbg, NULL);
    }
    quit_debugger(&dbg, RETURN_OK);
}
