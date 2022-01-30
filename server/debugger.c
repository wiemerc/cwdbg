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


// TODO: make static
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
        return DOSFALSE;
    }
    gp_dstate->p_debugger_task = FindTask(NULL);
    gp_dstate->target_state = TS_IDLE;
    gp_dstate->p_current_bpoint = NULL;

    // load target
    if ((gp_dstate->p_seglist = LoadSeg(p_program_path)) == NULL) {
        LOG(ERROR, "could not load target: %ld", IoErr());
        return DOSFALSE;
    }
    // seglist points to (first) code segment, code starts one long word behind pointer
    gp_dstate->p_entry = BCPL_TO_C_PTR(gp_dstate->p_seglist + 1);

    // initialize list of breakpoints, lh_Type is used as number of breakpoints
    NewList(&gp_dstate->bpoints);
    gp_dstate->bpoints.lh_Type = 0;

    return DOSTRUE;
}


void run_target()
{
    BreakPoint *p_bpoint;

    // reset breakpoint counters for each run
    if (!IsListEmpty(&gp_dstate->bpoints)) {
        for (p_bpoint = (BreakPoint *) gp_dstate->bpoints.lh_Head;
            p_bpoint != (BreakPoint *) gp_dstate->bpoints.lh_Tail;
            p_bpoint = (BreakPoint *) p_bpoint->node.ln_Succ)
            p_bpoint->count = 0;
    }

    LOG(INFO, "starting target");
    gp_dstate->target_state = TS_RUNNING;
    if ((gp_dstate->p_target_task = (struct Task *) CreateNewProcTags(
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
    LOG(DEBUG, "waiting for signal from target...");
    Wait(SIG_TARGET_EXITED);
    gp_dstate->target_state = TS_EXITED;
    LOG(INFO, "target terminated with exit code %d", gp_dstate->exit_code);
}


void continue_target(TaskContext *p_task_ctx)
{
    // If we continue from a breakpoint, it has to be restored first, so we
    // single-step the original instruction at the breakpoint and remember
    // to restore the breakpoint afterwards (see handle_single_step() below).
    // TODO: just continue in case of a deleted breakpoint
    gp_dstate->target_state &= ~TS_SINGLE_STEPPING;
    if (gp_dstate->target_state & TS_STOPPED_BY_BREAKPOINT) {
        p_task_ctx->reg_sr &= 0xbfff;    // clear T0
        p_task_ctx->reg_sr |= 0x8700;    // set T1 and interrupt mask
    }
}


void single_step_target(TaskContext *p_task_ctx)
{
    gp_dstate->target_state |= TS_SINGLE_STEPPING;
    // In trace mode, *all* interrupts must be disabled (except for the NMI),
    // otherwise OS code could be executed while the trace bit is still set,
    // which would cause the OS exception handler (an alert) to be executed instead
    // of ours => value 0x8700 is ORed with the SR.
    p_task_ctx->reg_sr &= 0xbfff;    // clear T0
    p_task_ctx->reg_sr |= 0x8700;    // set T1 and interrupt mask
}


void quit_debugger(int exit_code)
{
    BreakPoint *p_bpoint;

    LOG(INFO, "exiting...");
    // TODO: Can we track all memory allocations and release them here? Maybe by using Allocate()?
    // TODO: Close serial device here
    while ((p_bpoint = (BreakPoint *) RemHead(&gp_dstate->bpoints)))
        FreeVec(p_bpoint);
    UnLoadSeg(gp_dstate->p_seglist);
    Exit(exit_code);
}


BreakPoint *set_breakpoint(ULONG offset)
{
    BreakPoint  *p_bpoint;
    APTR        p_baddr;

    if ((p_bpoint = AllocVec(sizeof(BreakPoint), 0)) == NULL) {
        LOG(ERROR, "could not allocate memory for breakpoint");
        return NULL;
    }
    p_baddr = (APTR) ((ULONG) gp_dstate->p_entry) + offset;
    p_bpoint->num          = ++gp_dstate->bpoints.lh_Type;
    p_bpoint->p_address         = p_baddr;
    p_bpoint->opcode       = *((USHORT *) p_baddr);
    p_bpoint->count        = 0;
    AddTail(&gp_dstate->bpoints, (struct Node *) p_bpoint);
    *((USHORT *) p_baddr) = TRAP_OPCODE;
    LOG(INFO, "breakpoint set at entry + 0x%08lx", offset);
    return p_bpoint;
}


BreakPoint *find_bpoint_by_addr(struct List *p_bpoints, void *p_baddr)
{
    BreakPoint *p_bpoint;

    if (IsListEmpty(p_bpoints))
        return NULL;
    for (p_bpoint = (BreakPoint *) p_bpoints->lh_Head;
         p_bpoint != (BreakPoint *) p_bpoints->lh_Tail;
         p_bpoint = (BreakPoint *) p_bpoint->node.ln_Succ) {
        if (p_bpoint->p_address == p_baddr)
            return p_bpoint;
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

    gp_dstate->target_state |= TS_STOPPED_BY_BREAKPOINT;
    p_baddr = p_task_ctx->p_reg_pc - 2;
    if ((p_bpoint = find_bpoint_by_addr(&gp_dstate->bpoints, p_baddr)) != NULL) {
        gp_dstate->p_current_bpoint = p_bpoint;
        // rewind PC by 2 bytes and replace trap instruction with original instruction
        p_task_ctx->p_reg_pc = p_baddr;
        *((USHORT *) p_baddr) = p_bpoint->opcode;
        ++p_bpoint->count;
        LOG(INFO, "target has hit breakpoint #%ld at entry + 0x%08lx, hit count = %ld", 
            p_bpoint->num, ((ULONG) p_baddr - (ULONG) gp_dstate->p_entry), p_bpoint->count);
    }
    else {
        LOG(CRIT, "INTERNAL ERROR: target has hit unknown breakpoint at entry + 0x%08lx", ((ULONG) p_baddr - (ULONG) gp_dstate->p_entry));
        return;
    }

    // TODO: store mode in global debugger state and call either process_cli_commands() or process_remote_commands()
    process_cli_commands(p_task_ctx);
    gp_dstate->target_state &= ~TS_STOPPED_BY_BREAKPOINT;
}


void handle_single_step(TaskContext *p_task_ctx)
{
    gp_dstate->target_state |= TS_STOPPED_BY_SINGLE_STEP;
    if (gp_dstate->p_current_bpoint) {
        // breakpoint needs to be restored
        LOG(
            DEBUG,
            "restoring breakpoint #%ld at entry + 0x%08lx",
            gp_dstate->p_current_bpoint->num,
            ((ULONG) gp_dstate->p_current_bpoint->p_address - (ULONG) gp_dstate->p_entry)
        );
        *((USHORT *) gp_dstate->p_current_bpoint->p_address) = TRAP_OPCODE;
        gp_dstate->p_current_bpoint = NULL;
    }
    if (gp_dstate->target_state & TS_SINGLE_STEPPING) {
        LOG(INFO, "target has stopped after single step");
        process_cli_commands(p_task_ctx);
    }
    gp_dstate->target_state &= ~TS_STOPPED_BY_SINGLE_STEP;
}


void handle_exception(TaskContext *p_task_ctx)
{
    // unhandled exception occurred
    gp_dstate->target_state |= TS_STOPPED_BY_EXCEPTION;
    LOG(
        INFO,
        "unhandled exception #%ld occurred at entry + 0x%08lx",
        p_task_ctx->exc_num,
        ((ULONG) p_task_ctx->p_reg_pc - (ULONG) gp_dstate->p_entry)
    );

    process_cli_commands(p_task_ctx);
    gp_dstate->target_state &= ~TS_STOPPED_BY_EXCEPTION;
}


//
// local routines
//

static void wrap_target()
{
    // allocate trap and install exception handler
    gp_dstate->p_target_task->tc_TrapCode = exc_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        return;
    }

    LOG(
        DEBUG,
        "calling entry point of target, initial PC = 0x%08lx, initial SP = 0x%08lx",
        (ULONG) gp_dstate->p_entry,
        (ULONG) gp_dstate->p_target_task->tc_SPUpper - 2
    );
    gp_dstate->exit_code = gp_dstate->p_entry();

    // signal debugger that target has finished
    Signal(gp_dstate->p_debugger_task, SIG_TARGET_EXITED);
}
