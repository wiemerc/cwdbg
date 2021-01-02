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
#include <dos/dos.h>
#include <exec/tasks.h>
#include <exec/types.h>


/*
 * constants
 */
#define TRAP_NUM            0
#define TRAP_OPCODE         0x4e40
#define TARGET_STACK_SIZE   8192
#define SIG_TARGET_EXITED   1

/*
 * target states
 * Multiple values are possible (e. g. TS_RUNNING and TS_SINGLE_STEPPING), so we use individual bits.
 */
#define TS_IDLE                     0l
#define TS_RUNNING                  (1l << 1)
#define TS_SINGLE_STEPPING          (1l << 2)
#define TS_EXITED                   (1l << 3)
#define TS_STOPPED_BY_BREAKPOINT    (1l << 4)
#define TS_STOPPED_BY_SINGLE_STEP   (1l << 5)
#define TS_STOPPED_BY_EXCEPTION     (1l << 6)


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
    int          ds_target_state;        // current target state
    int          ds_exit_code;           // exit code of target
    struct List  ds_bpoints;             // list of breakpoints
    BreakPoint   *ds_p_prev_bpoint;      // previous breakpoint that needs to be restored
} DebuggerState;


/*
 * external functions
 */
extern void exc_handler();


/*
 * exported functions
 */
int load_and_init_target(const char *p_program_path);
void run_target();
void continue_target(TaskContext *p_task_ctx);
void single_step_target(TaskContext *p_task_ctx);
void quit_debugger();
BreakPoint *set_breakpoint(ULONG offset);
BreakPoint *find_bpoint_by_addr(struct List *bpoints, APTR baddr);
void handle_breakpoint(TaskContext *p_task_ctx);
void handle_single_step(TaskContext *p_task_ctx);
void handle_exception(TaskContext *p_task_ctx);


/*
 * external references
 */
extern DebuggerState *gp_dstate;    /* global debugger state */

#endif /* CWDEBUG_DEBUGGER_H */
