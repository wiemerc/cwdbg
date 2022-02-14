/*
 * debugger.c - part of CWDebug, a source-level debugger for the AmigaOS
 *              This file contains the core routines of the debugger.
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


#include <dos/dostags.h>
#include <exec/types.h>
#include <memory.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "cli.h"
#include "debugger.h"
#include "serio.h"
#include "stdint.h"
#include "util.h"


DebuggerState g_dstate;


static void wrap_target();


//
// exported routines
//

int load_and_init_target(const char *p_program_path)
{
    // TODO: support arguments for target

    g_dstate.p_debugger_task = FindTask(NULL);
    g_dstate.target_state = TS_IDLE;
    g_dstate.p_current_bpoint = NULL;

    // load target
    if ((g_dstate.p_seglist = LoadSeg(p_program_path)) == NULL) {
        LOG(ERROR, "could not load target: %ld", IoErr());
        return DOSFALSE;
    }
    // seglist points to (first) code segment, code starts one long word behind pointer
    g_dstate.p_entry = BCPL_TO_C_PTR(g_dstate.p_seglist + 1);

    // initialize list of breakpoints, lh_Type is used as number of breakpoints
    NewList(&g_dstate.bpoints);
    g_dstate.bpoints.lh_Type = 0;

    return DOSTRUE;
}


void run_target()
{
    BreakPoint *p_bpoint;

    // reset breakpoint counters for each run
    if (!IsListEmpty(&g_dstate.bpoints)) {
        for (p_bpoint = (BreakPoint *) g_dstate.bpoints.lh_Head;
            p_bpoint != (BreakPoint *) g_dstate.bpoints.lh_Tail;
            p_bpoint = (BreakPoint *) p_bpoint->node.ln_Succ)
            p_bpoint->count = 0;
    }

    LOG(INFO, "starting target");
    g_dstate.target_state = TS_RUNNING;
    if ((g_dstate.p_target_task = (struct Task *) CreateNewProcTags(
        NP_Name, (uint32_t) "debugme",
        NP_Entry, (uint32_t) wrap_target,
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
    g_dstate.target_state = TS_EXITED;
    LOG(INFO, "target terminated with exit code %d", g_dstate.exit_code);
}


void continue_target(TaskContext *p_task_ctx)
{
    // If we continue from a breakpoint, it has to be restored first, so we
    // single-step the original instruction at the breakpoint and remember
    // to restore the breakpoint afterwards (see handle_single_step() below).
    // TODO: just continue in case of a deleted breakpoint
    g_dstate.target_state &= ~TS_SINGLE_STEPPING;
    if (g_dstate.target_state & TS_STOPPED_BY_BREAKPOINT) {
        p_task_ctx->reg_sr &= 0xbfff;    // clear T0
        p_task_ctx->reg_sr |= 0x8700;    // set T1 and interrupt mask
    }
}


void single_step_target(TaskContext *p_task_ctx)
{
    g_dstate.target_state |= TS_SINGLE_STEPPING;
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
    while ((p_bpoint = (BreakPoint *) RemHead(&g_dstate.bpoints)))
        FreeVec(p_bpoint);
    UnLoadSeg(g_dstate.p_seglist);
    serio_exit();
    Exit(exit_code);
}


BreakPoint *set_breakpoint(uint32_t offset)
{
    BreakPoint *p_bpoint;
    void       *p_baddr;

    // TODO: Check if offset is valid
    if ((p_bpoint = AllocVec(sizeof(BreakPoint), 0)) == NULL) {
        LOG(ERROR, "Could not allocate memory for breakpoint");
        return NULL;
    }
    p_baddr = (void *) ((uint32_t) g_dstate.p_entry) + offset;
    p_bpoint->num       = ++g_dstate.bpoints.lh_Type;
    p_bpoint->p_address = p_baddr;
    p_bpoint->opcode    = *((uint16_t *) p_baddr);
    p_bpoint->count     = 0;
    AddTail(&g_dstate.bpoints, (struct Node *) p_bpoint);
    *((uint16_t *) p_baddr) = TRAP_OPCODE;
    LOG(DEBUG, "Breakpoint set at entry + 0x%08lx", offset);
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


void get_target_info(TargetInfo *p_target_info, TaskContext *p_task_ctx)
{
    p_target_info->target_state = g_dstate.target_state;
    p_target_info->exit_code    = g_dstate.exit_code;
    if (p_task_ctx) {
        // target is still running, add task context, next instruction and top 8 dwords on the stack
        memcpy(&p_target_info->task_context, p_task_ctx, sizeof(TaskContext));
        if (VALID_ADDRESS(p_task_ctx->p_reg_pc) && VALID_ADDRESS(((uint8_t *) p_task_ctx->p_reg_pc) + 8)) {
            memcpy(&p_target_info->next_instr_bytes, p_task_ctx->p_reg_pc, 8);
        }
        if (VALID_ADDRESS(p_task_ctx->p_reg_sp) && VALID_ADDRESS(((uint8_t *) p_task_ctx->p_reg_pc) + 32)) {
            memcpy(&p_target_info->top_stack_dwords, ((uint8_t *) p_task_ctx->p_reg_pc) + 32, 32);
        }
    }
}


//
// routines called by the exception handler
//

void handle_breakpoint(TaskContext *p_task_ctx)
{
    BreakPoint *p_bpoint;
    void       *p_baddr;

    g_dstate.target_state |= TS_STOPPED_BY_BREAKPOINT;
    p_baddr = p_task_ctx->p_reg_pc - 2;
    if ((p_bpoint = find_bpoint_by_addr(&g_dstate.bpoints, p_baddr)) != NULL) {
        g_dstate.p_current_bpoint = p_bpoint;
        // rewind PC by 2 bytes and replace trap instruction with original instruction
        p_task_ctx->p_reg_pc = p_baddr;
        *((uint16_t *) p_baddr) = p_bpoint->opcode;
        ++p_bpoint->count;
        LOG(INFO, "Target has hit breakpoint #%ld at entry + 0x%08lx, hit count = %ld", 
            p_bpoint->num, ((uint32_t) p_baddr - (uint32_t) g_dstate.p_entry), p_bpoint->count);
    }
    else {
        LOG(CRIT, "Internal error: target has hit unknown breakpoint at entry + 0x%08lx", ((uint32_t) p_baddr - (uint32_t) g_dstate.p_entry));
        return;
    }

    g_dstate.p_process_commands_func(p_task_ctx);
    g_dstate.target_state &= ~TS_STOPPED_BY_BREAKPOINT;
}


void handle_single_step(TaskContext *p_task_ctx)
{
    g_dstate.target_state |= TS_STOPPED_BY_SINGLE_STEP;
    if (g_dstate.p_current_bpoint) {
        // breakpoint needs to be restored
        LOG(
            DEBUG,
            "Restoring breakpoint #%ld at entry + 0x%08lx",
            g_dstate.p_current_bpoint->num,
            ((uint32_t) g_dstate.p_current_bpoint->p_address - (uint32_t) g_dstate.p_entry)
        );
        *((uint16_t *) g_dstate.p_current_bpoint->p_address) = TRAP_OPCODE;
        g_dstate.p_current_bpoint = NULL;
    }
    if (g_dstate.target_state & TS_SINGLE_STEPPING) {
        LOG(INFO, "Target has stopped after single step");
        g_dstate.p_process_commands_func(p_task_ctx);
    }
    g_dstate.target_state &= ~TS_STOPPED_BY_SINGLE_STEP;
}


void handle_exception(TaskContext *p_task_ctx)
{
    // unhandled exception occurred
    g_dstate.target_state |= TS_STOPPED_BY_EXCEPTION;
    LOG(
        INFO,
        "Unhandled exception #%ld occurred at entry + 0x%08lx",
        p_task_ctx->exc_num,
        ((uint32_t) p_task_ctx->p_reg_pc - (uint32_t) g_dstate.p_entry)
    );

    g_dstate.p_process_commands_func(p_task_ctx);
    g_dstate.target_state &= ~TS_STOPPED_BY_EXCEPTION;
}


//
// local routines
//

static void wrap_target()
{
    // allocate trap and install exception handler
    g_dstate.p_target_task->tc_TrapCode = exc_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        return;
    }

    LOG(
        DEBUG,
        "calling entry point of target, initial PC = 0x%08lx, initial SP = 0x%08lx",
        (uint32_t) g_dstate.p_entry,
        (uint32_t) g_dstate.p_target_task->tc_SPUpper - 2
    );
    g_dstate.exit_code = g_dstate.p_entry();

    // signal debugger that target has finished
    Signal(g_dstate.p_debugger_task, SIG_TARGET_EXITED);
}
