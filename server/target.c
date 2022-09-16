/*
 * target.c - part of CWDebug, a source-level debugger for the AmigaOS
 *            This file contains the core routines of the debugger.
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


#include <dos/dostags.h>
#include <exec/types.h>
#include <memory.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdlib.h>

#include "debugger.h"
#include "server.h"
#include "stdint.h"
#include "target.h"
#include "util.h"


#define TRAP_NUM_BPOINT       0
#define TRAP_NUM_RESTORE      1
#define TRAP_OPCODE           0x4e40
#define TARGET_STACK_SIZE     8192
#define TARGET_DELAY_TIME     50  // in ticks, 1s = 50 ticks


struct Target {
    struct MsgPort *p_port;
    BPTR           p_seglist;
    uint32_t       (*p_entry_point)();
    struct Task    *p_task;
    TaskContext    *p_task_context;
    uint32_t       state;
    uint32_t       exit_code;
    uint32_t       error_code;
    struct List    bpoints;
    uint32_t       next_bpoint_num;
    Breakpoint     *p_active_bpoint;
};

typedef struct TargetStoppedMsg {
    struct Message  exec_msg;
    uint32_t        stop_reason;
    uint32_t        exit_code;
    uint32_t        error_code;
    TaskContext     *p_task_context;
} TargetStoppedMsg;


extern void exc_handler();


static void init_target_stopped_msg(TargetStoppedMsg *p_msg, struct MsgPort *p_reply_port);
static void wrap_target();
static void handle_breakpoint(Target *p_target);
static void handle_single_step(Target *p_target);
static void handle_exception(Target *p_target);


//
// exported functions
//

Target *create_target()
{
    Target *p_target;

    if ((p_target = AllocVec(sizeof(Target), MEMF_CLEAR)) == NULL) {
        LOG(ERROR, "Could not allocate memory for target object");
        return NULL;
    }
    if ((p_target->p_port = CreatePort("CWDEBUG_TARGET", 0)) == NULL) {
        LOG(ERROR, "Could not create message port for target");
        FreeVec(p_target);
        return NULL;
    }
    p_target->state = TS_IDLE;
    p_target->exit_code = -1;
    NewList(&p_target->bpoints);
    p_target->next_bpoint_num = 1;

    return p_target;
}


void destroy_target(Target *p_target)
{
    Breakpoint *p_bpoint;

    if (p_target->state & TS_RUNNING)
        DeleteTask(p_target->p_task);
    if (p_target->p_seglist);
        UnLoadSeg(p_target->p_seglist);
    while ((p_bpoint = (Breakpoint *) RemHead(&p_target->bpoints)))
        FreeVec(p_bpoint);
    if (p_target->p_port)
        DeletePort(p_target->p_port);
    FreeVec(p_target);
}


DbgError load_target(Target *p_target, const char *p_program_path)
{
    if ((p_target->p_seglist = LoadSeg(p_program_path)) == NULL) {
        LOG(ERROR, "Could not load target: %ld", IoErr());
        return ERROR_LOAD_TARGET_FAILED;
    }
    p_target->p_entry_point = (uint32_t (*)()) BCPL_TO_C_PTR(p_target->p_seglist + 1);
    return ERROR_OK;
}


void run_target(Target *p_target)
{
    Breakpoint *p_bpoint;
    TargetStoppedMsg *p_stopped_msg = NULL;

    // reset breakpoint counters for each run
    if (!IsListEmpty(&p_target->bpoints)) {
        for (
            p_bpoint = (Breakpoint *) p_target->bpoints.lh_Head;
            p_bpoint != (Breakpoint *) p_target->bpoints.lh_Tail;
            p_bpoint = (Breakpoint *) p_bpoint->node.ln_Succ
        )
            p_bpoint->hit_count = 0;
    }

    // TODO: support arguments for target
    LOG(INFO, "Starting target");
    p_target->state = TS_RUNNING;
    if ((p_target->p_task = (struct Task *) CreateNewProcTags(
        NP_Name, (uint32_t) "CWDEBUG_TARGET",
        NP_Entry, (uint32_t) wrap_target,
        NP_StackSize, TARGET_STACK_SIZE,
        NP_Input, Input(),
        NP_Output, Output(),
        NP_CloseInput, FALSE,
        NP_CloseOutput, FALSE,
        // TODO: The libnix startup code used by GCC checks if pr_CLI is NULL and, if so, waits for the Workbench startup message and
        // therefore hangs (see https://github.com/adtools/libnix/blob/e7bfc563a4dda18c2c0d9dd95f863a3bb6c2a356/sources/startup/ncrt0.S#L34).
        // So we have to specify NP_Cli = TRUE and use RunCommand() to run the target. But this causes several problems:
        // - The CLI process, in which the debugger was launched, terminates upon exit (sometimes closing the console windows with it).
        // - In some cases, CreateNewProcTags() itself just seems to hang.
        // - Any C program using the startup code hangs upon exit when it is run a second time.
        NP_Cli, TRUE
    )) == NULL) {
        LOG(CRIT, "Could not create process for target");
        // We can't return the error code directly because in the normal case this function only returns when the
        // target exits. But the server needs to immediately acknowledge the MSG_RUN command. So we pack the state
        // TS_ERROR and the error code into the MSG_TARGET_STOPPED message which is sent when this function returns.
        p_target->state = TS_ERROR;
        p_target->error_code = ERROR_CREATE_PROC_FAILED;
        return;
    }
    // associate target task with the message port so that the task gets signalled when a message arrives
    p_target->p_port->mp_SigTask = p_target->p_task;

    while (TRUE) {
        LOG(DEBUG, "Waiting for messages from target...");
        do {
            WaitPort(gp_dbg->p_debugger_port);
            p_stopped_msg = (TargetStoppedMsg *) GetMsg(gp_dbg->p_debugger_port);
        } while (p_stopped_msg == NULL);
        LOG(DEBUG, "Received message from target process, stop reason = %d", p_stopped_msg->stop_reason);
        p_target->p_task_context = p_stopped_msg->p_task_context;

        // messages from wrap_target()
        if (p_stopped_msg->stop_reason == TS_EXITED) {
            p_target->state = p_stopped_msg->stop_reason;
            p_target->exit_code = p_stopped_msg->exit_code;
            LOG(INFO, "Target terminated with exit code %d", p_stopped_msg->exit_code);
            ReplyMsg((struct Message *) p_stopped_msg);
            return;
        }
        else if (p_stopped_msg->stop_reason == TS_ERROR) {
            p_target->state = p_stopped_msg->stop_reason;
            p_target->error_code = p_stopped_msg->error_code;
            LOG(CRIT, "Running target failed with error code %d", p_stopped_msg->error_code);
            ReplyMsg((struct Message *) p_stopped_msg);
            return;
        }

        // messages from handle_stopped_target()
        else {
            p_target->state |= p_stopped_msg->stop_reason;
            if (p_stopped_msg->stop_reason == TS_STOPPED_BY_BPOINT) {
                handle_breakpoint(p_target);
                process_commands(gp_dbg);
            }
            else if (p_stopped_msg->stop_reason == TS_STOPPED_BY_SINGLE_STEP) {
                handle_single_step(p_target);
                if (p_target->state & TS_SINGLE_STEPPING)
                    process_commands(gp_dbg);
            }
            else if (p_stopped_msg->stop_reason == TS_STOPPED_BY_EXCEPTION) {
                handle_exception(p_target);
                process_commands(gp_dbg);
            }
            else {
                LOG(CRIT, "Internal error: unknown stop reason %d", p_stopped_msg->stop_reason);
                p_target->state = TS_ERROR;
                p_target->error_code = ERROR_UNKNOWN_STOP_REASON;
                return;
            }

            if (p_target->state == TS_KILLED)
                // Target has been killed after it stopped and process no longer exists.
                break;
            else {
                p_target->state &= ~p_stopped_msg->stop_reason;
                ReplyMsg((struct Message *) p_stopped_msg);
            }
        }
    }
}


void set_continue_mode(Target *p_target)
{
    // If we continue from a regular breakpoint which hasn't been deleted (so p_target->p_active_bpoint still points to it),
    // it has to be restored first, so we single-step the original instruction at the breakpoint and remember to
    // restore the breakpoint afterwards (see handle_single_step() below).
    p_target->state &= ~TS_SINGLE_STEPPING;
    if ((p_target->state & TS_STOPPED_BY_BPOINT) && p_target->p_active_bpoint) {
        p_target->p_task_context->reg_sr &= 0xbfff;    // clear T0
        p_target->p_task_context->reg_sr |= 0x8700;    // set T1 and interrupt mask
    }
}


void set_single_step_mode(Target *p_target)
{
    p_target->state |= TS_SINGLE_STEPPING;
    // In trace mode, *all* interrupts must be disabled (except for the NMI), otherwise OS code could be executed while
    // the trace bit is still set, which would cause the OS exception handler (an alert) to be executed instead of ours
    // => value 0x8700 is ORed with the SR.
    p_target->p_task_context->reg_sr &= 0xbfff;    // clear T0
    p_target->p_task_context->reg_sr |= 0x8700;    // set T1 and interrupt mask
}


DbgError set_breakpoint(Target *p_target, uint32_t offset, uint16_t f_is_one_shot)
{
    Breakpoint *p_bpoint;
    void       *p_baddr;

    // TODO: Check if offset is valid
    // TODO: One-shot breakpoints sometimes seem to cause a AN_MemCorrupt guru
    if ((p_bpoint = AllocVec(sizeof(Breakpoint), 0)) == NULL) {
        LOG(ERROR, "Could not allocate memory for breakpoint");
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    p_baddr = (void *) ((uint32_t) p_target->p_entry_point) + offset;
    p_bpoint->num           = p_target->next_bpoint_num++;
    p_bpoint->p_address     = p_baddr;
    p_bpoint->opcode        = *((uint16_t *) p_baddr);
    p_bpoint->f_is_one_shot = f_is_one_shot;
    p_bpoint->hit_count     = 0;
    AddTail(&p_target->bpoints, (struct Node *) p_bpoint);
    *((uint16_t *) p_baddr) = TRAP_OPCODE;
    LOG(DEBUG, "Breakpoint #%ld at entry + 0x%08lx set", p_bpoint->num, offset);
    return ERROR_OK;
}


// TODO: Use breakpoint number to identify breakpoint
void clear_breakpoint(Target *p_target, Breakpoint *p_bpoint)
{
    *((uint16_t *) p_bpoint->p_address) = p_bpoint->opcode;
    Remove((struct Node *) p_bpoint);
    if (p_target->p_active_bpoint == p_bpoint)
        p_target->p_active_bpoint = NULL;
    LOG(
        DEBUG,
        "Breakpoint #%ld at entry + 0x%08lx cleared",
        p_bpoint->num,
        ((uint32_t) p_bpoint->p_address - (uint32_t) p_target->p_entry_point)
    );
    FreeVec(p_bpoint);
}


Breakpoint *find_bpoint_by_addr(Target *p_target, void *p_bp_addr)
{
    Breakpoint *p_bpoint;

    if (IsListEmpty(&p_target->bpoints))
        return NULL;
    for (p_bpoint = (Breakpoint *) p_target->bpoints.lh_Head;
         p_bpoint != (Breakpoint *) p_target->bpoints.lh_Tail;
         p_bpoint = (Breakpoint *) p_bpoint->node.ln_Succ) {
        if (p_bpoint->p_address == p_bp_addr)
            return p_bpoint;
    }
    return NULL;
}


Breakpoint *find_bpoint_by_num(Target *p_target, uint32_t bp_num)
{
    Breakpoint *p_bpoint;

    if (IsListEmpty(&p_target->bpoints))
        return NULL;
    for (p_bpoint = (Breakpoint *) p_target->bpoints.lh_Head;
         p_bpoint != (Breakpoint *) p_target->bpoints.lh_Tail;
         p_bpoint = (Breakpoint *) p_bpoint->node.ln_Succ) {
        if (p_bpoint->num == bp_num)
            return p_bpoint;
    }
    return NULL;
}


void get_target_info(Target *p_target, TargetInfo *p_target_info)
{
    p_target_info->p_initial_pc = p_target->p_entry_point;
    p_target_info->p_initial_sp = p_target->p_task->tc_SPUpper - 2;
    p_target_info->state        = p_target->state;
    p_target_info->exit_code    = p_target->exit_code;
    p_target_info->error_code   = p_target->error_code;
    if (p_target->state & TS_RUNNING) {
        // target is still running, add task context, next n instructions and top n dwords on the stack
        memcpy(&p_target_info->task_context, p_target->p_task_context, sizeof(TaskContext));
        if ((uint32_t) p_target->p_task_context->p_reg_pc <= (0xffffffff - NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES)) {
            memcpy(&p_target_info->next_instr_bytes, p_target->p_task_context->p_reg_pc, NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES);
        }
        if ((uint32_t) p_target->p_task_context->p_reg_sp <= (0xffffffff - NUM_TOP_STACK_DWORDS * 4)) {
            memcpy(&p_target_info->top_stack_dwords, p_target->p_task_context->p_reg_sp, NUM_TOP_STACK_DWORDS * 4);
        }
        if (p_target->state & TS_STOPPED_BY_BPOINT) {
            if (p_target->p_active_bpoint) {
                p_target_info->bpoint.num       = p_target->p_active_bpoint->num;
                p_target_info->bpoint.p_address = p_target->p_active_bpoint->p_address;
                p_target_info->bpoint.opcode    = p_target->p_active_bpoint->opcode;
                p_target_info->bpoint.hit_count = p_target->p_active_bpoint->hit_count;
            }
            else {
                p_target_info->state &= ~TS_STOPPED_BY_BPOINT;
                p_target_info->state |= TS_STOPPED_BY_ONE_SHOT_BPOINT;
            }
        }
    }
}


void kill_target(Target *p_target)
{
    // TODO: restore breakpoint if necessary
    p_target->state = TS_KILLED;
    RemTask(p_target->p_task);
    LOG(INFO, "Target has been killed");
}


// This routine is the entry point into the debugger called by the exception handler in the context of the target process.
// It sends sends a message to the debugger process, informing it that the target has stopped. This message is received
// by run_target() which then calls one of the handle_* routines below (in the context of the debugger process).
// This routine is necessary because an exception handler runs in supervisor mode and therefore can't send and receive
// messages. It has to run in the context of the target process so that WaitPort() blocks the target until the user
// requests to continue it. Note that it accesses the global variable gp_dbg, which is possible because all processes
// share the same address space in AmigaOS (the same goes for wrap_target() below).
void handle_stopped_target(uint32_t stop_reason, TaskContext *p_task_context)
{
    TargetStoppedMsg msg;

    LOG(DEBUG, "handle_stopped_target() has been called, stop reason = %d", stop_reason);
    init_target_stopped_msg(&msg, gp_dbg->p_target->p_port);
    msg.stop_reason = stop_reason;
    msg.p_task_context = p_task_context;
    LOG(DEBUG, "Sending message to debugger process");
    PutMsg(gp_dbg->p_debugger_port, (struct Message *) &msg);
    do {
        WaitPort(gp_dbg->p_target->p_port);
    } while (GetMsg(gp_dbg->p_target->p_port) == NULL);
    LOG(DEBUG, "Received message from debugger process - resuming target");
}


//
// local routines
//

// This routine is the entry point for the target process (used by run_target() when creating the target process).
static void wrap_target()
{
    TargetStoppedMsg stopped_msg;
    int result;

    // TODO: There is a race condition between CreateNewProcTags() returning in run_target() and thus setting
    //       gp_dbg->p_target->p_task and us accessing it here, so we just wait a little. A clean solution would
    //       be to use a semaphore.
    Delay(TARGET_DELAY_TIME);
    init_target_stopped_msg(&stopped_msg, gp_dbg->p_target->p_port);

    // allocate traps and install exception handler
    gp_dbg->p_target->p_task->tc_TrapCode = exc_handler;
    if (AllocTrap(TRAP_NUM_BPOINT) == -1) {
        LOG(CRIT, "Internal error: could not allocate trap for breakpoints");
        stopped_msg.stop_reason = TS_ERROR;
        stopped_msg.error_code = ERROR_NO_TRAP;
        goto send_msg;
    }
    if (AllocTrap(TRAP_NUM_RESTORE) == -1) {
        LOG(CRIT, "Internal error: could not allocate trap for restoring the task context");
        stopped_msg.stop_reason = TS_ERROR;
        stopped_msg.error_code = ERROR_NO_TRAP;
        goto send_msg;
    }

    LOG(
        DEBUG,
        "Running target, initial PC = 0x%08lx, initial SP = 0x%08lx",
        gp_dbg->p_target->p_entry_point,
        (uint32_t) gp_dbg->p_target->p_task->tc_SPUpper - 2
    );
    // We need to use RunCommand() instead of just calling the entry point if we specify NP_Cli in CreateNewProcTags(),
    // otherwise we get a crash. The argument string (3rd argument) has to be terminated by a newline character.
    if ((result = RunCommand(gp_dbg->p_target->p_seglist, TARGET_STACK_SIZE, "\n", 1)) == -1) {
        LOG(CRIT, "Running target with RunCommand() failed");
        stopped_msg.stop_reason = TS_ERROR;
        stopped_msg.error_code = ERROR_RUN_COMMAND_FAILED;
        goto send_msg;
    }
    else {
        stopped_msg.stop_reason = TS_EXITED;
        stopped_msg.exit_code = (uint32_t) result;
    }

    // Send message to debugger to inform it that target has finished or an error occurred
    send_msg:
        LOG(DEBUG, "Sending message to debugger process");
        PutMsg(gp_dbg->p_debugger_port, (struct Message *) &stopped_msg);
        do {
            WaitPort(gp_dbg->p_target->p_port);
        } while (GetMsg(gp_dbg->p_target->p_port) == NULL);
        LOG(DEBUG, "Received message from debugger process - exiting target");
}


static void init_target_stopped_msg(TargetStoppedMsg *p_msg, struct MsgPort *p_reply_port)
{
    memset(p_msg, 0, sizeof(TargetStoppedMsg));
    p_msg->exec_msg.mn_Length = sizeof(TargetStoppedMsg);
    p_msg->exec_msg.mn_ReplyPort = p_reply_port;
}


static void handle_breakpoint(Target *p_target)
{
    Breakpoint *p_bpoint;
    void       *p_baddr;

    p_baddr = p_target->p_task_context->p_reg_pc - 2;
    if ((p_bpoint = find_bpoint_by_addr(p_target, p_baddr)) != NULL) {
        if (!p_bpoint->f_is_one_shot)
            // set pointer to active breakpoint only if the hit breakpoint is a regular one
            // to indicate that it needs to be restored (see set_continue_mode() and handle_single_step())
            p_target->p_active_bpoint = p_bpoint;

        // rewind PC by 2 bytes and replace trap instruction with original instruction
        p_target->p_task_context->p_reg_pc = p_baddr;
        *((uint16_t *) p_baddr) = p_bpoint->opcode;
        ++p_bpoint->hit_count;
        LOG(
            INFO,
            "Target has hit breakpoint #%ld at entry + 0x%08lx, hit count = %ld", 
            p_bpoint->num,
            ((uint32_t) p_baddr - (uint32_t) p_target->p_entry_point),
            p_bpoint->hit_count
        );
    }
    else {
        LOG(
            WARN,
            "Target has hit unknown breakpoint at entry + 0x%08lx",
            ((uint32_t) p_baddr - (uint32_t) p_target->p_entry_point)
        );
    }
}


static void handle_single_step(Target *p_target)
{
    if (p_target->p_active_bpoint) {
        // breakpoint needs to be restored
        LOG(
            DEBUG,
            "Restoring breakpoint #%ld at entry + 0x%08lx",
            p_target->p_active_bpoint->num,
            ((uint32_t) p_target->p_active_bpoint->p_address - (uint32_t) p_target->p_entry_point)
        );
        *((uint16_t *) p_target->p_active_bpoint->p_address) = TRAP_OPCODE;
        p_target->p_active_bpoint = NULL;
    }
    if (p_target->state & TS_SINGLE_STEPPING) {
        LOG(INFO, "Target has stopped after single step");
    }
}


static void handle_exception(Target *p_target)
{
    // unhandled exception occurred
    LOG(
        INFO,
        "Unhandled exception #%ld occurred at entry + 0x%08lx",
        p_target->p_task_context->exc_num,
        ((uint32_t) p_target->p_task_context->p_reg_pc - (uint32_t) p_target->p_entry_point)
    );
}
