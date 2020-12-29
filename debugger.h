#ifndef CWDEBUG_DEBUGGER_H
#define CWDEBUG_DEBUGGER_H
/*
 * debugger.h - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <dos/dostags.h>
#include <exec/types.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "util.h"
#include "serio.h"
#include "m68k.h"


/*
 * constants
 */
#define TRAP_NUM            0
#define TRAP_OPCODE         0x4e40
#define TARGET_STACK_SIZE   8192
#define SIG_TARGET_EXITED   1

#define CMD_BREAKPOINT  0
#define CMD_RUN         1
#define CMD_STEP        2
#define CMD_EXCEPTION   3
#define CMD_CONTINUE    4
#define CMD_RESTORE     5
#define CMD_KILL        6
#define CMD_QUIT        7


/*
 * type definitions
 */
typedef struct {
    APTR   tc_reg_sp;
    ULONG  tc_exc_num;
    USHORT tc_reg_sr;
    APTR   tc_reg_pc;
    ULONG  tc_reg_d[8];
    ULONG  tc_reg_a[7];                  // without A7 = SP
} TaskContext;

typedef struct {
    struct Node  bp_node;
    ULONG        bp_num;
    APTR         bp_addr;                // address in code segment
    USHORT       bp_opcode;              // original opcode at this address
    ULONG        bp_count;               // number of times it has been hit
} BreakPoint;

typedef struct {
    struct Task  *ds_p_debugger_task;    // our own task - for the target to signal us
    struct Task  *ds_p_target_task;      // task of target
    BPTR         ds_p_seglist;           // segment list of target
    int          (*ds_p_entry)();        // entry point of target
    int          ds_exit_code;           // exit code of target
    struct List  ds_bpoints;             // list of breakpoints
    BreakPoint   *ds_p_prev_bpoint;      // previous breakpoint that needs to be restored
    int          ds_f_running;           // target running?
    int          ds_f_stepping;          // in single-step mode?
} DebuggerState;


/*
 * external functions
 */
extern void exc_handler();


/*
 * exported functions
 */
int load_and_init_target(const char *p_program_path);
int handle_breakpoint(TaskContext *p_task_ctx);
int handle_single_step(TaskContext *p_task_ctx);
int handle_exception(TaskContext *p_task_ctx);

#endif /* CWDEBUG_DEBUGGER_H */
