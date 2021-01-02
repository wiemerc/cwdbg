/*
 * debugger.c - part of CWDebug, a source-level debugger for the AmigaOS
 *              This file contains the core routines of the debugger.
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


#include <dos/dostags.h>
#include <exec/types.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "cli.h"
#include "debugger.h"
#include "util.h"


DebuggerState *gp_dstate;


static void wrap_target();


//
// exported routines
//
int load_and_init_target(const char *p_program_path)
{
    // TODO: support arguments for target

    // initialize state
    if ((gp_dstate = AllocVec(sizeof(DebuggerState), 0)) == NULL) {
        LOG(ERROR, "could not allocate memory for debugger state");
        return RETURN_ERROR;
    }
    gp_dstate->ds_p_debugger_task = FindTask(NULL);
    gp_dstate->ds_target_state = TS_IDLE;
    gp_dstate->ds_p_prev_bpoint = NULL;

    // load target
    if ((gp_dstate->ds_p_seglist = LoadSeg(p_program_path)) == NULL) {
        LOG(ERROR, "could not load target: %ld", IoErr());
        return RETURN_ERROR;
    }
    // seglist points to (first) code segment, code starts one long word behind pointer
    gp_dstate->ds_p_entry = BCPL_TO_C_PTR(gp_dstate->ds_p_seglist + 1);

    // initialize list of breakpoints, lh_Type is used as number of breakpoints
    NewList(&gp_dstate->ds_bpoints);
    gp_dstate->ds_bpoints.lh_Type = 0;

    process_cli_commands(NULL);
    return RETURN_OK;
}


void run_target()
{
    BreakPoint *p_bpoint;

    // reset breakpoint counters for each run
    if (!IsListEmpty(&gp_dstate->ds_bpoints)) {
        for (p_bpoint = (BreakPoint *) gp_dstate->ds_bpoints.lh_Head;
            p_bpoint != (BreakPoint *) gp_dstate->ds_bpoints.lh_Tail;
            p_bpoint = (BreakPoint *) p_bpoint->bp_node.ln_Succ)
            p_bpoint->bp_count = 0;
    }

    LOG(INFO, "starting target");
    gp_dstate->ds_target_state = TS_RUNNING;
    if ((gp_dstate->ds_p_target_task = (struct Task *) CreateNewProcTags(
        NP_Name, (ULONG) "debugme",
        NP_Entry, (ULONG) wrap_target,
        NP_StackSize, TARGET_STACK_SIZE,
        NP_Input, Input(),
        NP_Output, Output(),
        NP_CloseInput, FALSE,
        NP_CloseOutput, FALSE
        )) == NULL) {
        LOG(ERROR, "could not start target as process");
        return;
    }
//    LOG(DEBUG, "waiting for signal from target...");
    Wait(SIG_TARGET_EXITED);
    gp_dstate->ds_target_state = TS_EXITED;
    LOG(INFO, "target terminated with exit code %d", gp_dstate->ds_exit_code);
}


void continue_target(TaskContext *p_task_ctx)
{
    // If we continue from a breakpoint, it has to be restored first, so we
    // single-step the original instruction at the breakpoint and remember
    // to restore the breakpoint afterwards (see handle_single_step() below).
    // TODO: just continue in case of a deleted breakpoint
    gp_dstate->ds_target_state &= ~TS_SINGLE_STEPPING;
    if (gp_dstate->ds_target_state & TS_STOPPED_BY_BREAKPOINT) {
        p_task_ctx->tc_reg_sr &= 0xbfff;    // clear T0
        p_task_ctx->tc_reg_sr |= 0x8700;    // set T1 and interrupt mask
    }
}


void single_step_target(TaskContext *p_task_ctx)
{
    gp_dstate->ds_target_state |= TS_SINGLE_STEPPING;
    // In trace mode, *all* interrupts must be disabled (except for the NMI),
    // otherwise OS code could be executed while the trace bit is still set,
    // which would cause the OS exception handler (an alert) to be executed instead
    // of ours => value 0x8700 is ORed with the SR.
    p_task_ctx->tc_reg_sr &= 0xbfff;    // clear T0
    p_task_ctx->tc_reg_sr |= 0x8700;    // set T1 and interrupt mask
}


void quit_debugger()
{
    BreakPoint *p_bpoint;

    LOG(INFO, "exiting...");
    while ((p_bpoint = (BreakPoint *) RemHead(&gp_dstate->ds_bpoints)))
        FreeVec(p_bpoint);
    UnLoadSeg(gp_dstate->ds_p_seglist);
}


BreakPoint *set_breakpoint(ULONG offset)
{
    BreakPoint  *p_bpoint;
    APTR        p_baddr;

    if ((p_bpoint = AllocVec(sizeof(BreakPoint), 0)) == NULL) {
        LOG(ERROR, "could not allocate memory for breakpoint");
        return NULL;
    }
    p_baddr = (APTR) ((ULONG) gp_dstate->ds_p_entry) + offset;
    p_bpoint->bp_num          = ++gp_dstate->ds_bpoints.lh_Type;
    p_bpoint->bp_addr         = p_baddr;
    p_bpoint->bp_opcode       = *((USHORT *) p_baddr);
    p_bpoint->bp_count        = 0;
    AddTail(&gp_dstate->ds_bpoints, (struct Node *) p_bpoint);
    *((USHORT *) p_baddr) = TRAP_OPCODE;
    LOG(INFO, "breakpoint set at entry + 0x%08lx", offset);
    return p_bpoint;
}


BreakPoint *find_bpoint_by_addr(struct List *bpoints, APTR baddr)
{
    BreakPoint *bpoint;

    if (IsListEmpty(bpoints))
        return NULL;
    for (bpoint = (BreakPoint *) bpoints->lh_Head;
         bpoint != (BreakPoint *) bpoints->lh_Tail;
         bpoint = (BreakPoint *) bpoint->bp_node.ln_Succ) {
        if (bpoint->bp_addr == baddr)
            return bpoint;
    }
    return NULL;
}


//
// routines called by the exception handler
//

void handle_breakpoint(TaskContext *p_task_ctx)
{
    BreakPoint          *p_bpoint;
    APTR                p_baddr;

    gp_dstate->ds_target_state |= TS_STOPPED_BY_BREAKPOINT;
    p_baddr = p_task_ctx->tc_reg_pc - 2;
    if ((p_bpoint = find_bpoint_by_addr(&gp_dstate->ds_bpoints, p_baddr)) != NULL) {
        gp_dstate->ds_p_prev_bpoint = p_bpoint;
        // rewind PC by 2 bytes and replace trap instruction with original instruction
        p_task_ctx->tc_reg_pc = p_baddr;
        *((USHORT *) p_baddr) = p_bpoint->bp_opcode;
        ++p_bpoint->bp_count;
        LOG(INFO, "target has hit breakpoint #%ld at entry + 0x%08lx, hit count = %ld", 
            p_bpoint->bp_num, ((ULONG) p_baddr - (ULONG) gp_dstate->ds_p_entry), p_bpoint->bp_count);
    }
    else {
        LOG(CRIT, "INTERNAL ERROR: target has hit unknown breakpoint at entry + 0x%08lx", ((ULONG) p_baddr - (ULONG) gp_dstate->ds_p_entry));
        return;
    }

    process_cli_commands(p_task_ctx);
    gp_dstate->ds_target_state &= ~TS_STOPPED_BY_BREAKPOINT;
}


void handle_single_step(TaskContext *p_task_ctx)
{
    gp_dstate->ds_target_state |= TS_STOPPED_BY_SINGLE_STEP;
    if (gp_dstate->ds_p_prev_bpoint) {
        // previous breakpoint needs to be restored
//        LOG(
//            DEBUG,
//            "restoring breakpoint #%ld at entry + 0x%08lx",
//            gp_dstate->ds_p_prev_bpoint->bp_num,
//            ((ULONG) gp_dstate->ds_p_prev_bpoint->bp_addr - (ULONG) gp_dstate->ds_p_entry)
//        );
        *((USHORT *) gp_dstate->ds_p_prev_bpoint->bp_addr) = TRAP_OPCODE;
        gp_dstate->ds_p_prev_bpoint = NULL;
    }
    if (gp_dstate->ds_target_state & TS_SINGLE_STEPPING) {
        LOG(INFO, "target has stopped after single step");
        process_cli_commands(p_task_ctx);
    }
    gp_dstate->ds_target_state &= ~TS_STOPPED_BY_SINGLE_STEP;
}


void handle_exception(TaskContext *p_task_ctx)
{
    // unhandled exception occurred
    gp_dstate->ds_target_state |= TS_STOPPED_BY_EXCEPTION;
    LOG(
        INFO,
        "unhandled exception #%ld occurred at entry + 0x%08lx",
        p_task_ctx->tc_exc_num,
        ((ULONG) p_task_ctx->tc_reg_pc - (ULONG) gp_dstate->ds_p_entry)
    );

    process_cli_commands(p_task_ctx);
    gp_dstate->ds_target_state &= ~TS_STOPPED_BY_EXCEPTION;
}


//
// local routines
//

static void wrap_target()
{
    // allocate trap and install exception handler
    gp_dstate->ds_p_target_task->tc_TrapCode = exc_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        return;
    }

    LOG(
        DEBUG,
        "calling entry point of target, initial PC = 0x%08lx, initial SP = 0x%08lx",
        (ULONG) gp_dstate->ds_p_entry,
        (ULONG) gp_dstate->ds_p_target_task->tc_SPUpper - 2
    );
    gp_dstate->ds_exit_code = gp_dstate->ds_p_entry();

    // signal debugger that target has finished
    Signal(gp_dstate->ds_p_debugger_task, SIG_TARGET_EXITED);
}
