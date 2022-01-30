/*
 * cli.c - part of CWDebug, a source-level debugger for the AmigaOS
 *         This files contains the routines for the CLI on AmigaOS.
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "debugger.h"
#include "m68k.h"
#include "util.h"


static UBYTE parse_args(char *p_cmd, char **pp_args);
static int is_correct_target_state_for_command(char cmd);
static void print_instr(const TaskContext *ctx);
static void print_registers(const TaskContext *ctx);
static void print_stack(const TaskContext *ctx, APTR initial_sp);
static void print_memory(const UBYTE *addr, ULONG size);


//
// exported routines
//
void process_cli_commands(TaskContext *p_task_ctx)
{
    char                cmd[64];                // command buffer
    char                *p_args[5];             // argument list
    UBYTE               nargs;                  // number of arguments
    ULONG               offset;                 // offset from entry point
    APTR                p_maddr;                // address of memory block to print
    ULONG               msize;                  // size of memory block

    if (g_dstate.target_state & TS_RUNNING)
        print_instr(p_task_ctx);
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
                g_dstate.target_state = TS_EXITED;
                Signal(g_dstate.p_debugger_task, SIG_TARGET_EXITED);
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
                        print_stack(p_task_ctx, g_dstate.p_target_task->tc_SPUpper - 2);
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


//
// local routines
//
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


static int is_correct_target_state_for_command(char cmd)
{
    // keep list of commands (the 1st argument of strchr()) in sync with process_cli_commands()
    if (!(g_dstate.target_state & TS_RUNNING) && (strchr("cs\nik", cmd) != NULL)) {
        LOG(ERROR, "incorrect state for command '%c': target is not yet running", cmd);
        return 0;
    }
    if ((g_dstate.target_state & TS_RUNNING) && (strchr("rq", cmd) != NULL)) {
        LOG(ERROR, "incorrect state for command '%c': target is already / still running", cmd);
        return 0;
    }
    return 1;
}


static void print_instr(const TaskContext *ctx)
{
    ULONG nbytes;
    UWORD *sp;
    char instr[128], dump[64], *dp;

    nbytes = m68k_disassemble(instr, (ULONG) ctx->p_reg_pc, M68K_CPU_TYPE_68030);
    for (sp = ctx->p_reg_pc, dp = dump; nbytes > 0 && dp < dump + 64; nbytes -= 2, ++sp, dp += 5)
        sprintf(dp, "%04x ", *sp);
    printf("PC=0x%08lx: %-20s: %s\n", (ULONG) ctx->p_reg_pc, dump, instr);
}


static void print_registers(const TaskContext *ctx)
{
    UBYTE i;

    // TODO: pretty-print status register
    for (i = 0; i < 4; i++)
        printf("D%d=0x%08lx  ", i, ctx->reg_d[i]);
    puts("");
    for (i = 4; i < 8; i++)
        printf("D%d=0x%08lx  ", i, ctx->reg_d[i]);
    puts("");
    for (i = 0; i < 4; i++)
        printf("A%d=0x%08lx  ", i, ctx->reg_a[i]);
    puts("");
    for (i = 4; i < 7; i++)
        printf("A%d=0x%08lx  ", i, ctx->reg_a[i]);
    printf("A7(SP)=0x%08lx\n", (ULONG) ctx->p_reg_sp);
}


static void print_stack(const TaskContext *ctx, APTR initial_sp)
{
    UBYTE i;
    APTR  sp;

    // TODO: Should we print words instead of dwords?
    printf("initial SP = 0x%08lx, current SP = 0x%08lx\n", (ULONG) initial_sp, (ULONG) ctx->p_reg_sp);
    for (i = 1, sp = ctx->p_reg_sp; (i <= 10) && (sp <= initial_sp); ++i, sp += 4) {
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
