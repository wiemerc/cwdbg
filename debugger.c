/*
 * debugger.c - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


#include "debugger.h"
#include "serio.h"


DebuggerState *gp_dstate;


// TODO: move routines to cli.c
static void print_instr(const TaskContext *ctx)
{
    ULONG nbytes;
    UWORD *sp;
    char instr[128], dump[64], *dp;

    nbytes = m68k_disassemble(instr, (ULONG) ctx->tc_reg_pc, M68K_CPU_TYPE_68030);
    for (sp = ctx->tc_reg_pc, dp = dump; nbytes > 0 && dp < dump + 64; nbytes -= 2, ++sp, dp += 5)
        sprintf(dp, "%04x ", *sp);
    printf("PC=0x%08lx: %-20s: %s\n", (ULONG) ctx->tc_reg_pc, dump, instr);
}


static void print_registers(const TaskContext *ctx)
{
    UBYTE i;

    // TODO: pretty-print status register
    for (i = 0; i < 4; i++)
        printf("D%d=0x%08lx  ", i, ctx->tc_reg_d[i]);
    puts("");
    for (i = 4; i < 8; i++)
        printf("D%d=0x%08lx  ", i, ctx->tc_reg_d[i]);
    puts("");
    for (i = 0; i < 4; i++)
        printf("A%d=0x%08lx  ", i, ctx->tc_reg_a[i]);
    puts("");
    for (i = 4; i < 7; i++)
        printf("A%d=0x%08lx  ", i, ctx->tc_reg_a[i]);
    printf("A7(SP)=0x%08lx\n", (ULONG) ctx->tc_reg_sp);
}


static void print_stack(const TaskContext *ctx, APTR initial_sp)
{
    UBYTE i;
    APTR  sp;

    // TODO: Should we print words instead of dwords?
    printf("initial SP = 0x%08lx, current SP = 0x%08lx\n", (ULONG) initial_sp, (ULONG) ctx->tc_reg_sp);
    for (i = 1, sp = ctx->tc_reg_sp; (i <= 10) && (sp <= initial_sp); ++i, sp += 4) {
        printf("0x%08lx:\t0x%08lx\n", (ULONG) sp, *((ULONG *) sp));
    }
}


static void print_memory(const UBYTE *addr, ULONG size)
{
    ULONG pos = 0, i, nchars;
    char line[256], *p;

    while (pos < size) {
        printf("%04lx: ", pos);
        for (i = pos, p = line, nchars = 0; (i < pos + 16) && (i < size); ++i, ++p, ++nchars) {
            printf("%02x ", addr[i]);
            if (addr[i] >= 0x20 && addr[i] <= 0x7e) {
                sprintf(p, "%c", addr[i]);
            }
            else {
                sprintf(p, ".");
            }
        }
        if (nchars < 16) {
            for (i = 1; i <= (3 * (16 - nchars)); ++i, ++p, ++nchars) {
                sprintf(p, " ");
            }
        }
        *p = '\0';

        printf("\t%s\n", line);
        pos += 16;
    }
}


static BreakPoint *find_bpoint_by_addr(struct List *bpoints, APTR baddr)
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


static UBYTE parse_args(char *p_cmd, char **pp_args)
{
    char    *p_token;               // pointer to one token
    UBYTE   nargs;                  // number of arguments

    p_token = strtok(p_cmd, " \t");
    nargs = 0;
    while ((nargs < 3) && (p_token != NULL)) {
        pp_args[nargs] = p_token;
        p_token = strtok(NULL, " \t");
        ++nargs;
    }
    pp_args[nargs] = NULL;
    return nargs;
}


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


static void run_target()
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
    Wait(SIG_TARGET_EXITED);
    gp_dstate->ds_target_state = TS_EXITED;
    LOG(INFO, "target terminated with exit code %d", gp_dstate->ds_exit_code);
}


static BreakPoint *set_breakpoint(ULONG offset)
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


static void continue_target(TaskContext *p_task_ctx)
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


static void single_step_target(TaskContext *p_task_ctx)
{
    gp_dstate->ds_target_state |= TS_SINGLE_STEPPING;
    // In trace mode, *all* interrupts must be disabled (except for the NMI),
    // otherwise OS code could be executed while the trace bit is still set,
    // which would cause the OS exception handler (an alert) to be executed instead
    // of ours => value 0x8700 is ORed with the SR.
    p_task_ctx->tc_reg_sr &= 0xbfff;    // clear T0
    p_task_ctx->tc_reg_sr |= 0x8700;    // set T1 and interrupt mask
}


static void quit_debugger()
{
    BreakPoint *p_bpoint;

    LOG(INFO, "exiting...");
    while ((p_bpoint = (BreakPoint *) RemHead(&gp_dstate->ds_bpoints)))
        FreeVec(p_bpoint);
    UnLoadSeg(gp_dstate->ds_p_seglist);
}


static int is_correct_target_state_for_command(char cmd)
{
    // keep list of commands (the 1st argument of strchr()) in sync with process_cli_commands()
    if (!(gp_dstate->ds_target_state & TS_RUNNING) && (strchr("cs\nik", cmd) != NULL)) {
        LOG(ERROR, "incorrect state for command '%c': target is not yet running", cmd);
        return 0;
    }
    if ((gp_dstate->ds_target_state & TS_RUNNING) && (strchr("rq", cmd) != NULL)) {
        LOG(ERROR, "incorrect state for command '%c': target is already / still running", cmd);
        return 0;
    }
    return 1;
}


static void process_cli_commands(TaskContext *p_task_ctx)
{
    char                cmd[64];                // command buffer
    char                *p_args[5];             // argument list
    UBYTE               nargs;                  // number of arguments
    ULONG               offset;                 // offset from entry point
    APTR                p_maddr;                // address of memory block to print
    ULONG               msize;                  // size of memory block

    while(1) {
        // read command from standard input (and ignore errors and commands >= 64 characters)
        Write(Output(), "> ", 2);
        WaitForChar(Input(), 0xffffffff);
        cmd[Read(Input(), cmd, 64)] = 0;
        nargs = parse_args(cmd, p_args);

        if (!is_correct_target_state_for_command(p_args[0][0]))
            continue;

        switch (p_args[0][0]) {
            case 'r':   // run target
                run_target();
                break;

            case 'b':   // set breakpoint
                if (nargs != 2) {
                    LOG(ERROR, "command 'b' requires an address");
                    break;
                }
                if (sscanf(p_args[1], "%lx", &offset) == 0) {
                    LOG(ERROR, "invalid format of breakpoint offset");
                    break;
                }
                set_breakpoint(offset);
                break;

            case 'k':   // kill (abort) target
                // TODO: restore breakpoint if necessary
                gp_dstate->ds_target_state = TS_EXITED;
                Signal(gp_dstate->ds_p_debugger_task, SIG_TARGET_EXITED);
                RemTask(NULL);
                return;  // We don't get here anyway...

            case 'q':   // quit debugger
                quit_debugger();
                return;

            case 'c':   // continue target
                continue_target(p_task_ctx);
                return;

            case 's':   // single step target
            case '\n':
                single_step_target(p_task_ctx);
                return;

            case 'i':   // inspect ...
                if (nargs != 2) {
                    LOG(ERROR, "command 'i' requires a subcommand, either 'r' or 's'");
                    break;
                }
                switch (p_args[1][0]) {
                    case 'r':   // ... registers
                        print_registers(p_task_ctx);
                        break;
                    case 's':   // ... stack
                        print_stack(p_task_ctx, gp_dstate->ds_p_target_task->tc_SPUpper - 2);
                        break;
                    default:
                        LOG(ERROR, "unknown command 'i %c'", p_args[1][0]);
                }
                break;

            case 'p':   // print memory
                if (nargs != 3) {
                    LOG(ERROR, "command 'p' requires address and size");
                    break;
                }
                if ((sscanf(p_args[1], "%p", &p_maddr) == 0) || (sscanf(p_args[2], "%ld", &msize) == 0)) {
                    LOG(ERROR, "invalid format for address / size");
                    break;
                }
                print_memory(p_maddr, msize);
                break;

            case 'd':   // disassemble memory
                // TODO: implement disassembling the next n instructions
                break;

            default:
                LOG(ERROR, "unknown command '%c'", p_args[0][0]);
                break;
        }
    }
}


int load_and_init_target(const char *p_program_path)
{
    Buffer              *p_frame;

    // TODO: support arguments for target
    // TODO: move to process_remote_commands()
    p_frame = create_buffer(MAX_BUFFER_SIZE);
    LOG(INFO, "waiting for host to connect...");
    recv_slip_frame(p_frame);

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

    print_instr(p_task_ctx);
    process_cli_commands(p_task_ctx);
    gp_dstate->ds_target_state &= ~TS_STOPPED_BY_BREAKPOINT;
}


void handle_single_step(TaskContext *p_task_ctx)
{
    gp_dstate->ds_target_state |= TS_STOPPED_BY_SINGLE_STEP;
    if (gp_dstate->ds_p_prev_bpoint) {
        // previous breakpoint needs to be restored
        LOG(DEBUG, "restoring breakpoint #%ld at entry + 0x%08lx",
            gp_dstate->ds_p_prev_bpoint->bp_num, ((ULONG) gp_dstate->ds_p_prev_bpoint->bp_addr - (ULONG) gp_dstate->ds_p_entry));
        *((USHORT *) gp_dstate->ds_p_prev_bpoint->bp_addr) = TRAP_OPCODE;
        gp_dstate->ds_p_prev_bpoint = NULL;
    }
    if (gp_dstate->ds_target_state & TS_SINGLE_STEPPING) {
        print_instr(p_task_ctx);
        process_cli_commands(p_task_ctx);
    }
    gp_dstate->ds_target_state &= ~TS_STOPPED_BY_SINGLE_STEP;
}


void handle_exception(TaskContext *p_task_ctx)
{
    // unhandled exception occurred
    gp_dstate->ds_target_state |= TS_STOPPED_BY_EXCEPTION;
    LOG(INFO, "unhandled exception #%ld occurred at entry + 0x%08lx",
        p_task_ctx->tc_exc_num,
        ((ULONG) p_task_ctx->tc_reg_pc - (ULONG) gp_dstate->ds_p_entry));

    print_instr(p_task_ctx);
    process_cli_commands(p_task_ctx);
    gp_dstate->ds_target_state &= ~TS_STOPPED_BY_EXCEPTION;
}
