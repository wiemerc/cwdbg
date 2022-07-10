#ifndef CWDEBUG_TARGET_H
#define CWDEBUG_TARGET_H
//
// target.h - part of CWDebug, a source-level debugger for the AmigaOS
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include "stdint.h"


//
// error codes
//
typedef enum {
    ERROR_OK                     = 0,
    ERROR_NOT_ENOUGH_MEMORY      = 1,
    ERROR_INVALID_ADDRESS        = 2,
    ERROR_UNKNOWN_BREAKPOINT     = 3,
    ERROR_LOAD_TARGET_FAILED     = 4,
    ERROR_CREATE_PROC_FAILED     = 5,
    ERROR_UNKNOWN_STOP_REASON    = 6,
    ERROR_NO_TRAP                = 7,
    ERROR_RUN_COMMAND_FAILED     = 8
} DbgError;

#define NUM_NEXT_INSTRUCTIONS 8
#define NUM_TOP_STACK_DWORDS  8
#define MAX_INSTR_BYTES       8

//
// target states
// Multiple values are possible (e. g. TS_RUNNING and TS_SINGLE_STEPPING), so we use individual bits.
//
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
// type declarations
//
typedef struct Target Target;
typedef struct TaskContext {
    void     *p_reg_sp;
    uint32_t exc_num;
    uint16_t reg_sr;
    void     *p_reg_pc;
    uint32_t reg_d[8];
    uint32_t reg_a[7];                  // without A7 = SP
} TaskContext;

typedef struct BreakPoint {
    struct Node  node;
    uint32_t     num;
    void         *p_address;            // address in code segment
    uint16_t     opcode;                // original opcode at this address
    uint32_t     hit_count;             // number of times it has been hit
} BreakPoint;

typedef struct TargetInfo {
    TaskContext  task_context;
    uint32_t     state;
    uint32_t     exit_code;
    uint32_t     error_code;
    // instruction bytes for the next n instructions, one instruction can be 8(?) bytes long at the most
    uint8_t      next_instr_bytes[NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES];
    // top n dwords on the stack
    uint32_t     top_stack_dwords[NUM_TOP_STACK_DWORDS];
} TargetInfo;


//
// exported functions
//
Target *create_target();
void destroy_target(Target *p_target);
DbgError load_target(Target *p_target, const char *p_program_path);
void run_target(Target *p_target);
void set_continue_mode(Target *p_target);
void set_single_step_mode(Target *p_target);
DbgError set_breakpoint(Target *p_target, uint32_t offset);
void clear_breakpoint(Target *p_target, BreakPoint *p_bpoint);
BreakPoint *find_bpoint_by_addr(Target *p_target, void *p_baddr);
BreakPoint *find_bpoint_by_num(Target *p_target, uint32_t bp_num);
uint32_t get_target_state(Target *p_target);
void get_target_info(Target *p_target, TargetInfo *p_target_info);
void *get_initial_sp_of_target(Target *p_target);
TaskContext *get_task_context(Target *p_target);
void kill_target(Target *p_target);
void handle_stopped_target(uint32_t stop_reason, TaskContext *p_task_ctx);

#endif  // CWDEBUG_TARGET_H
