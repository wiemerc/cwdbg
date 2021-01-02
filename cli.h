#ifndef CWDEBUG_CLI_H
#define CWDEBUG_CLI_H
/*
 * cli.h - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include "debugger.h"


/*
 * exported functions
 */
void process_cli_commands(TaskContext *p_task_ctx);

#endif /* CWDEBUG_CLI_H */
