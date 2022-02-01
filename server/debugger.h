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

#include "stdint.h"


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
#define TS_RUNNING                  (1l << 0)
#define TS_SINGLE_STEPPING          (1l << 1)
#define TS_EXITED                   (1l << 2)
#define TS_STOPPED_BY_BREAKPOINT    (1l << 3)
#define TS_STOPPED_BY_SINGLE_STEP   (1l << 4)
#define TS_STOPPED_BY_EXCEPTION     (1l << 5)


/*
 * type definitions
 */
typedef struct {
    void     *p_reg_sp;
    uint32_t exc_num;
    uint16_t reg_sr;
    void     *p_reg_pc;
    uint32_t reg_d[8];
    uint32_t reg_a[7];                // without A7 = SP
} TaskContext;

typedef struct {
    struct Node  node;
    uint32_t     num;
    void         *p_address;          // address in code segment
    uint16_t     opcode;              // original opcode at this address
    uint32_t     count;               // number of times it has been hit
} BreakPoint;

typedef struct {
    TaskContext  task_context;        // task context of target
    int          target_state;        // current target state
    int          exit_code;           // exit code if target has exited
} TargetInfo;

typedef struct {
    struct Task  *p_debugger_task;    // our own task - for the target to signal us
    struct Task  *p_target_task;      // task of target
    BPTR         p_seglist;           // segment list of target
    int          (*p_entry)();        // entry point of target
    int          target_state;        // current target state
    int          exit_code;           // exit code of target
    struct List  bpoints;             // list of breakpoints
    BreakPoint   *p_current_bpoint;   // current breakpoint that needs to be restored
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
BreakPoint *set_breakpoint(uint32_t offset);
BreakPoint *find_bpoint_by_addr(struct List *p_bpoints, void *p_baddr);
void handle_breakpoint(TaskContext *p_task_ctx);
void handle_single_step(TaskContext *p_task_ctx);
void handle_exception(TaskContext *p_task_ctx);


/*
 * external references
 */
extern DebuggerState g_dstate;    /* global debugger state */

#endif /* CWDEBUG_DEBUGGER_H */
