#ifndef CWDEBUG_DEBUGGER_H
#define CWDEBUG_DEBUGGER_H
//
// debugger.h - part of CWDebug, a source-level debugger for the AmigaOS
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include "server.h"
#include "target.h"
#include "stdint.h"


//
// type declarations
//
typedef struct Debugger {
    struct MsgPort *p_debugger_port;
    HostConnection *p_host_conn;
    Target         *p_target;
    // function that handles either CLI or remote commands, called by run_target()
    void           (*p_process_commands_func)();
} Debugger;


//
// pointer to global debugger object
//
extern Debugger *gp_dbg;


//
// exported functions
//
Debugger *create_debugger(int f_server_mode);
void destroy_debugger(Debugger *p_dbg);
void process_commands(Debugger *p_dbg);
void quit_debugger(Debugger *p_dbg, int exit_code);

#endif  // CWDEBUG_DEBUGGER_H
