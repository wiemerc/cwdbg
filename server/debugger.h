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
#define TRAP_NUM              0
#define TRAP_OPCODE           0x4e40
#define TARGET_STACK_SIZE     8192
#define SIG_TARGET_EXITED     1
#define NUM_NEXT_INSTRUCTIONS 8
#define NUM_TOP_STACK_DWORDS  8
#define MAX_INSTR_BYTES       8

/*
 * target states
 * Multiple values are possible (e. g. TS_RUNNING and TS_SINGLE_STEPPING), so we use individual bits.
 */
#define TS_IDLE                     0l
#define TS_RUNNING                  (1l << 0)
#define TS_SINGLE_STEPPING          (1l << 1)
#define TS_EXITED                   (1l << 2)
#define TS_KILLED                   (1l << 3)
#define TS_STOPPED_BY_BREAKPOINT    (1l << 4)
#define TS_STOPPED_BY_SINGLE_STEP   (1l << 5)
#define TS_STOPPED_BY_EXCEPTION     (1l << 6)

//
// error codes
//
#define ERROR_NOT_ENOUGH_MEMORY 1
#define ERROR_INVALID_ADDRESS   2


/*
 * type definitions
 */
typedef struct {
    void     *p_reg_sp;
    uint32_t exc_num;
    uint16_t reg_sr;
    void     *p_reg_pc;
    uint32_t reg_d[8];
    uint32_t reg_a[7];                  // without A7 = SP
} TaskContext;

typedef struct {
    struct Node  node;
    uint32_t     num;
    void         *p_address;            // address in code segment
    uint16_t     opcode;                // original opcode at this address
    uint32_t     count;                 // number of times it has been hit
} BreakPoint;

typedef struct {
    TaskContext  task_context;          // task context of target
    int          target_state;          // current target state
    int          exit_code;             // exit code if target has exited
    // instruction bytes for the next n instructions, one instruction can be 8(?) bytes long at the most
    uint8_t      next_instr_bytes[NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES];
    // top n dwords on the stack
    uint32_t     top_stack_dwords[NUM_TOP_STACK_DWORDS];
} TargetInfo;

typedef struct {
    struct Task  *p_debugger_task;      // our own task - for the target to signal us
    struct Task  *p_target_task;        // task of target
    BPTR         p_seglist;             // segment list of target
    int          (*p_entry)();          // entry point of target
    int          target_state;          // current target state
    int          exit_code;             // exit code of target
    struct List  bpoints;               // list of breakpoints
    BreakPoint   *p_current_bpoint;     // current breakpoint that needs to be restored
    // function that handles either CLI or remote commands, called by the handle_* functions
    void         (*p_process_commands_func)(TaskContext *);
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
void set_continue_mode(TaskContext *p_task_ctx);
void set_single_step_mode(TaskContext *p_task_ctx);
void quit_debugger(int exit_code);
uint8_t set_breakpoint(uint32_t offset);
void clear_breakpoint(BreakPoint *p_bpoint);
BreakPoint *find_bpoint_by_addr(struct List *p_bpoints, void *p_baddr);
BreakPoint *find_bpoint_by_num(struct List *p_bpoints, uint32_t bp_num);
void get_target_info(TargetInfo *p_target_info, TaskContext *p_task_ctx);
void handle_breakpoint(TaskContext *p_task_ctx);
void handle_single_step(TaskContext *p_task_ctx);
void handle_exception(TaskContext *p_task_ctx);


/*
 * external references
 */
extern DebuggerState g_dstate;    /* global debugger state */

#endif /* CWDEBUG_DEBUGGER_H */
