/*
 * debugger.c - part of CWDebug, a source-level debugger for the AmigaOS
 *              This file contains the core routines of the debugger.
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


#include <memory.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <stdlib.h>

#include "cli.h"
#include "debugger.h"
#include "m68k.h"
#include "stdint.h"
#include "util.h"


//
// pointer to global debugger object
//
Debugger *gp_dbg;


//
// exported functions
//

Debugger *create_debugger(int f_server_mode)
{
    Debugger *p_dbg;

    if ((p_dbg = AllocVec(sizeof(Debugger), MEMF_CLEAR)) == NULL) {
        LOG(ERROR, "Could not allocate memory for debugger object");
        return NULL;
    }
    if ((p_dbg->p_debugger_port = CreatePort("CWDEBUG_DBG", 0)) == NULL) {
        LOG(ERROR, "Could not create message port for debugger");
        return NULL;
    }
    LOG(DEBUG, "Created message port for debugger");
    if (f_server_mode) {
        if ((p_dbg->p_host_conn = create_host_conn()) == NULL) {
            LOG(ERROR, "Could not create host connection object");
            return NULL;
        }
        LOG(DEBUG, "Created host connection object");
        p_dbg->p_process_commands_func = process_remote_commands;
    }
    else {
        m68k_build_opcode_table();
        LOG(DEBUG, "Initialized disassembler routines");
        p_dbg->p_process_commands_func = process_cli_commands;
    }
    if ((p_dbg->p_target = create_target()) == NULL) {
        LOG(ERROR, "Could not create target object");
        return NULL;
    }
    LOG(DEBUG, "Created target object");
    return p_dbg;
}


void destroy_debugger(Debugger *p_dbg)
{
    if (p_dbg->p_debugger_port)
        DeletePort(p_dbg->p_debugger_port);
}


void process_commands(Debugger *p_dbg)
{
    p_dbg->p_process_commands_func();
}


void quit_debugger(Debugger *p_dbg, int exit_code)
{
    LOG(INFO, "Exiting...");
    if (p_dbg->p_target) {
        LOG(DEBUG, "Destroying target object");
        destroy_target(p_dbg->p_target);
    }
    if (p_dbg->p_host_conn) {
        LOG(DEBUG, "Destroying host connection object");
        destroy_host_conn(p_dbg->p_host_conn);
    }
    destroy_debugger(p_dbg);
    exit(exit_code);
}
