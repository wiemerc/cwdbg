/*
 * debugger.c - part of CWDebug, a source-level debugger for the AmigaOS
 *              This file contains the core routines of the debugger.
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

#include "cli.h"
#include "debugger.h"
#include "serio.h"
#include "stdint.h"
#include "util.h"


static void init_target_startup_msg(TargetStartupMsg *p_msg, struct MsgPort *p_reply_port);
static void init_target_stopped_msg(TargetStoppedMsg *p_msg, struct MsgPort *p_reply_port);
static void wrap_target();
static void handle_breakpoint(Debugger *p_dbg, TaskContext *p_task_ctx);
static void handle_single_step(Debugger *p_dbg, TaskContext *p_task_ctx);
static void handle_exception(Debugger *p_dbg, TaskContext *p_task_ctx);


//
// exported routines
//
// TODO: Have the routines return an error code instead of calling quit_debugger() so that
//       process_remote_commands() can inform the host

int init_debugger(Debugger *p_dbg)
{
    if ((p_dbg->p_debugger_port = CreatePort("CWDEBUG_DBG", 0)) == NULL) {
        LOG(ERROR, "Could not create message port for debugger");
        return DOSFALSE;
    }
    if ((p_dbg->p_target_port = CreatePort("CWDEBUG_TARGET", 0)) == NULL) {
        LOG(ERROR, "Could not create message port for target");
        return DOSFALSE;
    }
    p_dbg->p_target_task = NULL;
    p_dbg->p_seglist = NULL;
    p_dbg->target_state = TS_IDLE;
    p_dbg->exit_code = -1;
    p_dbg->p_current_bpoint = NULL;

    // initialize list of breakpoints, lh_Type is used as number of breakpoints
    NewList(&p_dbg->bpoints);
    p_dbg->bpoints.lh_Type = 0;

    return DOSTRUE;
}


int load_target(Debugger *p_dbg, const char *p_program_path)
{
    if ((p_dbg->p_seglist = LoadSeg(p_program_path)) == NULL) {
        LOG(ERROR, "Could not load target: %ld", IoErr());
        return DOSFALSE;
    }
    p_dbg->p_entry = (int (*)()) BCPL_TO_C_PTR(p_dbg->p_seglist + 1);
    return DOSTRUE;
}


void run_target(Debugger *p_dbg)
{
    BreakPoint *p_bpoint;
    TargetStartupMsg startup_msg;
    TargetStoppedMsg *p_stopped_msg;

    // reset breakpoint counters for each run
    if (!IsListEmpty(&p_dbg->bpoints)) {
        for (
            p_bpoint = (BreakPoint *) p_dbg->bpoints.lh_Head;
            p_bpoint != (BreakPoint *) p_dbg->bpoints.lh_Tail;
            p_bpoint = (BreakPoint *) p_bpoint->node.ln_Succ
        )
            p_bpoint->count = 0;
    }

    // TODO: support arguments for target
    LOG(INFO, "Starting target");
    p_dbg->target_state = TS_RUNNING;
    if ((p_dbg->p_target_task = (struct Task *) CreateNewProcTags(
        NP_Name, (uint32_t) "CWDEBUG_TARGET",
        NP_Entry, (uint32_t) wrap_target,
        NP_StackSize, TARGET_STACK_SIZE,
        NP_Input, Input(),
        NP_Output, Output(),
        NP_CloseInput, FALSE,
        NP_CloseOutput, FALSE,
        // TODO: The startup code used by GCC checks if pr_CLI is NULL and, if so, waits for the Workbench startup
        // message and therefore hangs. So we have to specify NP_Cli = TRUE, but this causes the CLI process, in which
        // the debugger was launched, to terminate upon exit (sometimes closing the console windows with it).
        // Should we hand-craft our own CLI struct in wrap_target()?
        NP_Cli, TRUE
    )) == NULL) {
        LOG(CRIT, "Could not start target as process");
        quit_debugger(p_dbg, RETURN_FAIL);
    }
    p_dbg->p_target_port->mp_SigTask = p_dbg->p_target_task;

    LOG(DEBUG, "Sending startup message to target");
    init_target_startup_msg(&startup_msg, p_dbg->p_debugger_port);
    startup_msg.p_seglist = p_dbg->p_seglist;
    PutMsg(p_dbg->p_target_port, (struct Message *) &startup_msg);

    LOG(DEBUG, "Waiting for messages from target...");
    while (WaitPort(p_dbg->p_debugger_port)) {
        p_stopped_msg = (TargetStoppedMsg *) GetMsg(p_dbg->p_debugger_port);
        LOG(DEBUG, "Received message from target process, stop reason = %d", p_stopped_msg->stop_reason);
        if (p_stopped_msg->stop_reason == TS_EXITED) {
            // message from wrap_target()
            p_dbg->target_state = p_stopped_msg->stop_reason;
            p_dbg->exit_code = p_stopped_msg->exit_code;
            LOG(INFO, "Target terminated with exit code %d", p_stopped_msg->exit_code);
            ReplyMsg((struct Message *) p_stopped_msg);
            return;
        }
        // TODO: Handle TS_ERROR
        else {
            // message from handle_stopped_target()
            p_dbg->target_state |= p_stopped_msg->stop_reason;
            if (p_stopped_msg->stop_reason == TS_STOPPED_BY_BREAKPOINT) {
                handle_breakpoint(p_dbg, p_stopped_msg->p_task_ctx);
                p_dbg->p_process_commands_func(p_dbg, p_stopped_msg->p_task_ctx);
            }
            else if (p_stopped_msg->stop_reason == TS_STOPPED_BY_SINGLE_STEP) {
                handle_single_step(p_dbg, p_stopped_msg->p_task_ctx);
                if (p_dbg->target_state & TS_SINGLE_STEPPING)
                    p_dbg->p_process_commands_func(p_dbg, p_stopped_msg->p_task_ctx);
            }
            else if (p_stopped_msg->stop_reason == TS_STOPPED_BY_EXCEPTION) {
                handle_exception(p_dbg, p_stopped_msg->p_task_ctx);
                p_dbg->p_process_commands_func(p_dbg, p_stopped_msg->p_task_ctx);
            }
            else {
                LOG(CRIT, "Internal error: unknown stop reason %d", p_stopped_msg->stop_reason);
                quit_debugger(p_dbg, RETURN_FAIL);
            }
            p_dbg->target_state &= ~p_stopped_msg->stop_reason;
            ReplyMsg((struct Message *) p_stopped_msg);
        }
    }
}


void set_continue_mode(Debugger *p_dbg, TaskContext *p_task_ctx)
{
    // If we continue from a breakpoint which hasn't been deleted (so p_dbg->p_current_bpoint still points to it),
    // it has to be restored first, so we single-step the original instruction at the breakpoint and remember to
    // restore the breakpoint afterwards (see handle_single_step() below).
    p_dbg->target_state &= ~TS_SINGLE_STEPPING;
    if ((p_dbg->target_state & TS_STOPPED_BY_BREAKPOINT) && p_dbg->p_current_bpoint) {
        p_task_ctx->reg_sr &= 0xbfff;    // clear T0
        p_task_ctx->reg_sr |= 0x8700;    // set T1 and interrupt mask
    }
}


void set_single_step_mode(Debugger *p_dbg, TaskContext *p_task_ctx)
{
    p_dbg->target_state |= TS_SINGLE_STEPPING;
    // In trace mode, *all* interrupts must be disabled (except for the NMI), otherwise OS code could be executed while
    // the trace bit is still set, which would cause the OS exception handler (an alert) to be executed instead of ours
    // => value 0x8700 is ORed with the SR.
    p_task_ctx->reg_sr &= 0xbfff;    // clear T0
    p_task_ctx->reg_sr |= 0x8700;    // set T1 and interrupt mask
}


void quit_debugger(Debugger *p_dbg, int exit_code)
{
    BreakPoint *p_bpoint;

    LOG(INFO, "Exiting...");
    if (p_dbg->target_state & TS_RUNNING)
        DeleteTask(p_dbg->p_target_task);
    if (p_dbg->p_seglist);
        UnLoadSeg(p_dbg->p_seglist);
    while ((p_bpoint = (BreakPoint *) RemHead(&p_dbg->bpoints)))
        FreeVec(p_bpoint);
    if (p_dbg->p_target_port)
        DeletePort(p_dbg->p_target_port);
    if (p_dbg->p_debugger_port)
        DeletePort(p_dbg->p_debugger_port);
    serio_exit();
    exit(exit_code);
}


uint8_t set_breakpoint(Debugger *p_dbg, uint32_t offset)
{
    BreakPoint *p_bpoint;
    void       *p_baddr;

    // TODO: Check if offset is valid
    // TODO: Use attribute of target / debugger class instead of lh_Type for the next breakpoint number once we've made the code object-oriented
    if ((p_bpoint = AllocVec(sizeof(BreakPoint), 0)) == NULL) {
        LOG(ERROR, "Could not allocate memory for breakpoint");
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    p_baddr = (void *) ((uint32_t) p_dbg->p_entry) + offset;
    p_bpoint->num       = ++p_dbg->bpoints.lh_Type;
    p_bpoint->p_address = p_baddr;
    p_bpoint->opcode    = *((uint16_t *) p_baddr);
    p_bpoint->count     = 0;
    AddTail(&p_dbg->bpoints, (struct Node *) p_bpoint);
    *((uint16_t *) p_baddr) = TRAP_OPCODE;
    LOG(DEBUG, "Breakpoint #%ld at entry + 0x%08lx set", p_bpoint->num, offset);
    return 0;
}


// TODO: Use breakpoint number to identify breakpoint
void clear_breakpoint(Debugger *p_dbg, BreakPoint *p_bpoint)
{
    *((uint16_t *) p_bpoint->p_address) = p_bpoint->opcode;
    Remove((struct Node *) p_bpoint);
    FreeVec(p_bpoint);
    if (p_dbg->p_current_bpoint == p_bpoint)
        p_dbg->p_current_bpoint = NULL;
    LOG(
        DEBUG,
        "Breakpoint #%ld at entry + 0x%08lx cleared",
        p_bpoint->num,
        ((uint32_t) p_bpoint->p_address - (uint32_t) p_dbg->p_entry)
    );
}


BreakPoint *find_bpoint_by_addr(Debugger *p_dbg, void *p_bp_addr)
{
    BreakPoint *p_bpoint;

    if (IsListEmpty(&p_dbg->bpoints))
        return NULL;
    for (p_bpoint = (BreakPoint *) p_dbg->bpoints.lh_Head;
         p_bpoint != (BreakPoint *) p_dbg->bpoints.lh_Tail;
         p_bpoint = (BreakPoint *) p_bpoint->node.ln_Succ) {
        if (p_bpoint->p_address == p_bp_addr)
            return p_bpoint;
    }
    return NULL;
}


BreakPoint *find_bpoint_by_num(Debugger *p_dbg, uint32_t bp_num)
{
    BreakPoint *p_bpoint;

    if (IsListEmpty(&p_dbg->bpoints))
        return NULL;
    for (p_bpoint = (BreakPoint *) p_dbg->bpoints.lh_Head;
         p_bpoint != (BreakPoint *) p_dbg->bpoints.lh_Tail;
         p_bpoint = (BreakPoint *) p_bpoint->node.ln_Succ) {
        if (p_bpoint->num == bp_num)
            return p_bpoint;
    }
    return NULL;
}


int get_target_state(Debugger *p_dbg)
{
    return p_dbg->target_state;
}


void get_target_info(Debugger *p_dbg, TargetInfo *p_target_info, TaskContext *p_task_ctx)
{
    p_target_info->target_state = p_dbg->target_state;
    p_target_info->exit_code    = p_dbg->exit_code;
    if (p_task_ctx) {
        // target is still running, add task context, next n instructions and top n dwords on the stack
        memcpy(&p_target_info->task_context, p_task_ctx, sizeof(TaskContext));
        if ((uint32_t) p_task_ctx->p_reg_pc <= (0xffffffff - NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES)) {
            memcpy(&p_target_info->next_instr_bytes, p_task_ctx->p_reg_pc, NUM_NEXT_INSTRUCTIONS * MAX_INSTR_BYTES);
        }
        if ((uint32_t) p_task_ctx->p_reg_sp <= (0xffffffff - NUM_TOP_STACK_DWORDS * 4)) {
            memcpy(&p_target_info->top_stack_dwords, p_task_ctx->p_reg_sp, NUM_TOP_STACK_DWORDS * 4);
        }
        // TODO: Include breakpoint structure if target has hit breakpoint
    }
}


void *get_initial_pc_of_target(Debugger *p_dbg)
{
    return p_dbg->p_target_task->tc_SPUpper - 2;
}


void kill_target(Debugger *p_dbg)
{
    // TODO: restore breakpoint if necessary
    p_dbg->target_state = TS_KILLED;
    RemTask(p_dbg->p_target_task);
    LOG(INFO, "Target has been killed");
}


// This routine is the entry point into the debugger called by the exception handler in the context of the target process.
// It sends sends a message to the debugger process informing it that the target has stopped. This message is received
// by run_target() which then calls one of the handle_* routines below (in the context of the debugger process).
void handle_stopped_target(int stop_reason, TaskContext *p_task_ctx)
{
    TargetStoppedMsg msg;
    struct MsgPort *p_target_port = FindPort("CWDEBUG_TARGET");
    struct MsgPort *p_debugger_port = FindPort("CWDEBUG_DBG");

    LOG(DEBUG, "handle_stopped_target() has been called, stop reason = %d", stop_reason);
    init_target_stopped_msg(&msg, p_target_port);
    msg.stop_reason = stop_reason;
    msg.p_task_ctx = p_task_ctx;
    LOG(DEBUG, "Sending message to debugger process");
    PutMsg(p_debugger_port, (struct Message *) &msg);
    WaitPort(p_target_port);
    GetMsg(p_target_port);
    LOG(DEBUG, "Received message from debugger process - resuming target");
}


//
// local routines
//

// This routine is the entry point for the target process (used by run_target() when creating the target process).
static void wrap_target()
{
    TargetStartupMsg *p_startup_msg;
    TargetStoppedMsg stopped_msg;
    struct MsgPort *p_target_port = FindPort("CWDEBUG_TARGET");
    struct MsgPort *p_debugger_port = FindPort("CWDEBUG_DBG");

    LOG(DEBUG, "Waiting for startup message...");
    WaitPort(p_target_port);
    p_startup_msg = (TargetStartupMsg *) GetMsg(p_target_port);

    init_target_stopped_msg(&stopped_msg, p_target_port);
    stopped_msg.p_task_ctx = NULL;

    // allocate traps and install exception handler
    FindTask(NULL)->tc_TrapCode = exc_handler;
    if (AllocTrap(TRAP_NUM_BP) == -1) {
        LOG(CRIT, "Internal error: could not allocate trap for breakpoints");
        stopped_msg.stop_reason = TS_ERROR;
        goto send_msg;
    }
    if (AllocTrap(TRAP_NUM_RESTORE) == -1) {
        LOG(CRIT, "Internal error: could not allocate trap for restoring the task context");
        stopped_msg.stop_reason = TS_ERROR;
        goto send_msg;
    }

    LOG(
        DEBUG,
        "Running target, initial PC = 0x%08lx, initial SP = 0x%08lx",
        (uint32_t) BCPL_TO_C_PTR(p_startup_msg->p_seglist + 1),
        (uint32_t) FindTask(NULL)->tc_SPUpper - 2
    );
    // We need to use RunCommand() instead of just calling the entry point if we specify NP_Cli in CreateNewProcTags(),
    // otherwise we get a crash.
    stopped_msg.exit_code = RunCommand(p_startup_msg->p_seglist, TARGET_STACK_SIZE, "", 0);
    stopped_msg.stop_reason = TS_EXITED;

    // Send message to debugger to inform it that target has finished or an error occurred
    send_msg:
        LOG(DEBUG, "Sending message to debugger process");
        PutMsg(p_debugger_port, (struct Message *) &stopped_msg);
        WaitPort(p_target_port);
        GetMsg(p_target_port);
        LOG(DEBUG, "Received message from debugger process - exiting target");
}


static void init_target_startup_msg(TargetStartupMsg *p_msg, struct MsgPort *p_reply_port)
{
    p_msg->exec_msg.mn_Length = sizeof(TargetStartupMsg);
    p_msg->exec_msg.mn_ReplyPort = p_reply_port;
}


static void init_target_stopped_msg(TargetStoppedMsg *p_msg, struct MsgPort *p_reply_port)
{
    p_msg->exec_msg.mn_Length = sizeof(TargetStoppedMsg);
    p_msg->exec_msg.mn_ReplyPort = p_reply_port;
}


static void handle_breakpoint(Debugger *p_dbg, TaskContext *p_task_ctx)
{
    BreakPoint *p_bpoint;
    void       *p_baddr;

    p_baddr = p_task_ctx->p_reg_pc - 2;
    if ((p_bpoint = find_bpoint_by_addr(p_dbg, p_baddr)) != NULL) {
        p_dbg->p_current_bpoint = p_bpoint;
        // rewind PC by 2 bytes and replace trap instruction with original instruction
        p_task_ctx->p_reg_pc = p_baddr;
        *((uint16_t *) p_baddr) = p_bpoint->opcode;
        ++p_bpoint->count;
        LOG(
            INFO,
            "Target has hit breakpoint #%ld at entry + 0x%08lx, hit count = %ld", 
            p_bpoint->num,
            ((uint32_t) p_baddr - (uint32_t) p_dbg->p_entry),
            p_bpoint->count
        );
    }
    else {
        LOG(
            CRIT,
            "Internal error: target has hit unknown breakpoint at entry + 0x%08lx",
            ((uint32_t) p_baddr - (uint32_t) p_dbg->p_entry)
        );
        quit_debugger(p_dbg, RETURN_FAIL);
    }
}


static void handle_single_step(Debugger *p_dbg, TaskContext *p_task_ctx)
{
    if (p_dbg->p_current_bpoint) {
        // breakpoint needs to be restored
        LOG(
            DEBUG,
            "Restoring breakpoint #%ld at entry + 0x%08lx",
            p_dbg->p_current_bpoint->num,
            ((uint32_t) p_dbg->p_current_bpoint->p_address - (uint32_t) p_dbg->p_entry)
        );
        *((uint16_t *) p_dbg->p_current_bpoint->p_address) = TRAP_OPCODE;
        p_dbg->p_current_bpoint = NULL;
    }
    if (p_dbg->target_state & TS_SINGLE_STEPPING) {
        LOG(INFO, "Target has stopped after single step");
    }
}


static void handle_exception(Debugger *p_dbg, TaskContext *p_task_ctx)
{
    // unhandled exception occurred
    LOG(
        INFO,
        "Unhandled exception #%ld occurred at entry + 0x%08lx",
        p_task_ctx->exc_num,
        ((uint32_t) p_task_ctx->p_reg_pc - (uint32_t) p_dbg->p_entry)
    );
}
