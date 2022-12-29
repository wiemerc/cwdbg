//
// main.c - part of cwdbg, a debugger for the AmigaOS
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include <dos/dos.h>
#include <proto/dos.h>
#include <stdlib.h>

#include "cli.h"
#include "debugger.h"
#include "server.h"
#include "util.h"


Debugger *gp_dbg;


// TODO: Clean up files:
// - sections in the same order with headings
// - header for each routine
// - convert all C to C++ comments
// - start all log messages with an uppercase letter
// - consistent log levels for errors
// - run clang-format / clang-tidy on all files
// - make arguments `const` where appropriate

int main()
{
    struct RDArgs *p_rdargs;
    long args[3] = {0l, 0l, 0l};
    int f_debug_mode, f_server_mode;
    const char *p_target_fname;

    g_loglevel = INFO;
    if ((p_rdargs = ReadArgs("-d=--debug/S,-s=--server/S,target/A", args, NULL)) == NULL) {
        LOG(ERROR, "wrong usage - usage: cwdbg [-d/--debug] [-s/--server] <target>");
        exit(RETURN_FAIL);
    }
    f_debug_mode  = args[0] == DOSTRUE ? 1 : 0;
    f_server_mode = args[1] == DOSTRUE ? 1 : 0;
    p_target_fname = (char *) args[2];
    if (f_debug_mode)
        g_loglevel = DEBUG;

    if ((gp_dbg = create_debugger(f_server_mode)) == NULL) {
        LOG(ERROR, "Could not create debugger object");
        FreeArgs(p_rdargs);
        exit(RETURN_FAIL);
    }

    if (load_target(gp_dbg->p_target, p_target_fname) != ERROR_OK) {
        LOG(ERROR, "Could not load target")
        FreeArgs(p_rdargs);
        quit_debugger(gp_dbg, RETURN_FAIL);
    }
    LOG(INFO, "Loaded target");
    FreeArgs(p_rdargs);

    process_commands(gp_dbg);
    quit_debugger(gp_dbg, RETURN_OK);
}
