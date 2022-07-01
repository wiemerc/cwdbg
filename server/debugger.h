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
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/types.h>

#include "stdint.h"


/*
 * constants
 */
#define TRAP_NUM_BP           0
#define TRAP_NUM_RESTORE      1
#define TRAP_OPCODE           0x4e40
#define TARGET_STACK_SIZE     8192
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
#define TS_ERROR                    (1l << 16)

//
// error codes
//
#define ERROR_NOT_ENOUGH_MEMORY  1
#define ERROR_INVALID_ADDRESS    2
#define ERROR_UNKNOWN_BREAKPOINT 3


/*
 * type definitions
 */
typedef struct STaskContext {
    void     *p_reg_sp;
    uint32_t exc_num;
    uint16_t reg_sr;
    void     *p_reg_pc;
    uint32_t reg_d[8];
    uint32_t reg_a[7];                  // without A7 = SP
} TaskContext;

typedef struct SBreakPoint {
    struct Node  node;
    uint32_t     num;
    void         *p_address;            // address in code segment
    uint16_t     opcode;                // original opcode at this address
    uint32_t     count;                 // number of times it has been hit
} BreakPoint;

typedef struct STargetInfo {
    TaskContext  task_context;          // task context of target
    int          target_state;          // current target state
    int          exit_code;             // exit code if target has exited
    // instruction bytes for the next n instructions, one instruction can be 8(?) bytes long at the most
    uint8_t      next_instr_bytes[NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES];
    // top n dwords on the stack
    uint32_t     top_stack_dwords[NUM_TOP_STACK_DWORDS];
} TargetInfo;

typedef struct SDebugger Debugger;
struct SDebugger {
    struct RDArgs  *p_rdargs;             // struct for CLI args, just needed so we can call FreeArgs() in quit_debugger()
    struct MsgPort *p_debugger_port;      // message port of debugger
    struct MsgPort *p_target_port;        // message port of target
    struct Task    *p_target_task;        // task of target
    BPTR           p_seglist;             // segment list of target
    int            (*p_entry)();          // entry point of target
    int            target_state;          // current target state
    int            exit_code;             // exit code of target
    struct List    bpoints;               // list of breakpoints
    BreakPoint     *p_current_bpoint;     // current breakpoint that needs to be restored
    // function that handles either CLI or remote commands, called by the handle_* functions
    void           (*p_process_commands_func)(Debugger *, TaskContext *);
};

typedef struct STargetStartupMsg {
    struct Message  exec_msg;
    BPTR            p_seglist;
} TargetStartupMsg;

typedef struct STargetStoppedMsg {
    struct Message  exec_msg;
    int             stop_reason;
    int             exit_code;
    TaskContext     *p_task_ctx;
} TargetStoppedMsg;


/*
 * external functions
 */
extern void exc_handler();


/*
 * exported functions
 */
int init_debugger(Debugger *p_dbg);
int load_target(Debugger *p_dbg, const char *p_program_path);
void run_target(Debugger *p_dbg);
void set_continue_mode(Debugger *p_dbg, TaskContext *p_task_ctx);
void set_single_step_mode(Debugger *p_dbg, TaskContext *p_task_ctx);
void quit_debugger(Debugger *p_dbg, int exit_code);
uint8_t set_breakpoint(Debugger *p_dbg, uint32_t offset);
void clear_breakpoint(Debugger *p_dbg, BreakPoint *p_bpoint);
BreakPoint *find_bpoint_by_addr(Debugger *p_dbg, void *p_baddr);
BreakPoint *find_bpoint_by_num(Debugger *p_dbg, uint32_t bp_num);
int get_target_state(Debugger *p_dbg);
void get_target_info(Debugger *p_dbg, TargetInfo *p_target_info, TaskContext *p_task_ctx);
void *get_initial_pc_of_target(Debugger *p_dbg);
void kill_target(Debugger *p_dbg);
void handle_stopped_target(int stop_reason, TaskContext *p_task_ctx);

#endif /* CWDEBUG_DEBUGGER_H */
