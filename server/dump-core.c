/*
 * dump-core.c - part of cwdbg, a debugger for the AmigaOS
 *               This file contains the routine dump_core() that can be linked with an executable to write a
 *               "core dump" and terminate the program in case of an exception. It will be called by the
 *               exception handler in catch-exc.s. It was inspired by catch.o and the "tb" utility written by the
 *               Software Distillery and distributed with the SAS C compiler.
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


#include <proto/dos.h>
#include <stdlib.h>

#include "debugger.h"


int dump_core(TaskContext *p_task_ctx)
{
    Printf(
        "Unhandled exception #%ld occurred at address 0x%08lx\n",
        p_task_ctx->exc_num,
        p_task_ctx->p_reg_pc
    );
    
    // TODO: Actually write a core dump. The dump could be in the Hunk format, with additional block types
    //       HUNK_CORE, HUNK_STACK and HUNK_MEMORY

    exit(RETURN_FAIL);
}
