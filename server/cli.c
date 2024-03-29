/*
 * cli.c - part of cwdbg, a debugger for the AmigaOS
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
#include "stdint.h"
#include "target.h"
#include "util.h"


static uint8_t parse_args(char *p_cmd, char **pp_args);
static int is_correct_target_state_for_command(uint32_t state, char cmd);
static void print_instr(const TaskContext *p_ctx);
static void print_registers(const TaskContext *p_ctx);
static void print_stack(const TaskContext *ctx, void *p_initial_sp);


//
// exported routines
//
void process_cli_commands()
{
    char                cmd_buffer[64];
    char                *p_args[5];
    uint8_t             nargs;
    uint32_t            bpoint_offset;
    void                *p_mem_addr;
    uint32_t            mem_size;
    uint32_t            bpoint_num;
    Breakpoint          *p_bpoint;
    TargetInfo          target_info;

    LOG(DEBUG, "process_cli_commands() has been called");
    get_target_info(gp_dbg->p_target, &target_info);
    if (target_info.state & TS_RUNNING)
        print_instr(&target_info.task_context);
    while(1) {
        // read command from standard input (and ignore errors and commands >= 64 characters)
        Write(Output(), "> ", 2);
        WaitForChar(Input(), 0xffffffff);
        cmd_buffer[Read(Input(), cmd_buffer, 64)] = 0;
        nargs = parse_args(cmd_buffer, p_args);

        if (!is_correct_target_state_for_command(target_info.state, p_args[0][0]))
            continue;

        switch (p_args[0][0]) {
            case 'r':   // run target
                run_target(gp_dbg->p_target);
                break;

            case 'b':   // set breakpoint
                if (nargs != 2) {
                    LOG(ERROR, "Command 'b' requires an address");
                    break;
                }
                if (sscanf(p_args[1], "%x", &bpoint_offset) == 0) {
                    LOG(ERROR, "Invalid format of breakpoint offset");
                    break;
                }
                set_breakpoint(gp_dbg->p_target, bpoint_offset, 0);
                break;

            case 'd':   // delete breakpoint
                if (nargs != 2) {
                    LOG(ERROR, "Command 'd' requires a breakpoint number");
                    break;
                }
                if (sscanf(p_args[1], "%d", &bpoint_num) == 0) {
                    LOG(ERROR, "Invalid format of breakpoint number");
                    break;
                }
                if ((p_bpoint = find_bpoint_by_num(gp_dbg->p_target, bpoint_num)) == 0) {
                    LOG(ERROR, "Breakpoint #%d not found", bpoint_num);
                    break;
                }
                clear_breakpoint(gp_dbg->p_target, p_bpoint);
                break;

            case 'k':   // kill (abort) target
                kill_target(gp_dbg->p_target);
                // Return to run_target() so it can exit and the outer invocation can take over again
                return;

            case 'q':   // quit debugger
                quit_debugger(gp_dbg, RETURN_OK);

            case 'c':   // continue target
                set_continue_mode(gp_dbg->p_target);
                return;

            case 's':   // single step target
            case '\n':
                set_single_step_mode(gp_dbg->p_target);
                return;

            case 'i':   // inspect ...
                if (nargs != 2) {
                    LOG(ERROR, "Command 'i' requires a subcommand, either 'r' or 's'");
                    break;
                }
                switch (p_args[1][0]) {
                    case 'r':   // ... registers
                        print_registers(&target_info.task_context);
                        break;
                    case 's':   // ... stack
                        print_stack(&target_info.task_context, target_info.p_initial_sp);
                        break;
                    default:
                        LOG(ERROR, "Unknown command 'i %c'", p_args[1][0]);
                }
                break;

            case 'p':   // print memory
                if (nargs != 3) {
                    LOG(ERROR, "Command 'p' requires address and size");
                    break;
                }
                if ((sscanf(p_args[1], "%p", &p_mem_addr) == 0) || (sscanf(p_args[2], "%d", &mem_size) == 0)) {
                    LOG(ERROR, "Invalid format for address / size");
                    break;
                }
                dump_memory(p_mem_addr, mem_size);
                break;

            case 'x':   // disassemble memory
                // TODO: implement disassembling the next n instructions
                // TODO: combine 'p' command with 'x', like 'x' in GDB
                break;

            default:
                LOG(ERROR, "Unknown command '%c'", p_args[0][0]);
                break;
        }
    }
}


//
// local routines
//
static uint8_t parse_args(char *p_cmd, char **pp_args)
{
    char    *p_token;               // pointer to one token
    uint8_t nargs;                  // number of arguments

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


static int is_correct_target_state_for_command(uint32_t state, char cmd)
{
    // keep list of commands (the 1st argument of strchr()) in sync with process_cli_commands()
    if (!(state & TS_RUNNING) && (strchr("cs\nikx", cmd) != NULL)) {
        LOG(ERROR, "incorrect state for command '%c': target is not yet running", cmd);
        return 0;
    }
    if ((state & TS_RUNNING) && (strchr("rq", cmd) != NULL)) {
        LOG(ERROR, "incorrect state for command '%c': target is already / still running", cmd);
        return 0;
    }
    return 1;
}


static void print_instr(const TaskContext *p_ctx)
{
    uint32_t nbytes;
    uint16_t *sp;
    char instr[128], dump[64], *dp;

    nbytes = m68k_disassemble(instr, (uint32_t) p_ctx->p_reg_pc, M68K_CPU_TYPE_68030);
    for (sp = p_ctx->p_reg_pc, dp = dump; nbytes > 0 && dp < dump + 64; nbytes -= 2, ++sp, dp += 5)
        sprintf(dp, "%04x ", *sp);
    printf("PC=0x%08x: %-20s: %s\n", (uint32_t) p_ctx->p_reg_pc, dump, instr);
}


static void print_registers(const TaskContext *p_ctx)
{
    uint8_t i;

    // TODO: pretty-print status register
    for (i = 0; i < 4; i++)
        printf("D%d=0x%08x  ", i, p_ctx->reg_d[i]);
    puts("");
    for (i = 4; i < 8; i++)
        printf("D%d=0x%08x  ", i, p_ctx->reg_d[i]);
    puts("");
    for (i = 0; i < 4; i++)
        printf("A%d=0x%08x  ", i, p_ctx->reg_a[i]);
    puts("");
    for (i = 4; i < 7; i++)
        printf("A%d=0x%08x  ", i, p_ctx->reg_a[i]);
    printf("A7(SP)=0x%08x\n", (uint32_t) p_ctx->p_reg_sp);
}


static void print_stack(const TaskContext *p_ctx, void *p_initial_sp)
{
    uint8_t  i;
    uint32_t sp;

    // TODO: Should we print words instead of dwords?
    printf("initial SP = 0x%08x, current SP = 0x%08x\n", (uint32_t) p_initial_sp, (uint32_t) p_ctx->p_reg_sp);
    for (i = 1, sp = (uint32_t) p_ctx->p_reg_sp; (i <= 10) && (sp <= (uint32_t) p_initial_sp); ++i, sp += 4) {
        printf("0x%08x:\t0x%08x\n", (uint32_t) sp, *((uint32_t *) sp));
    }
}
